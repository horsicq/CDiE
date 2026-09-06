/* Copyright (c) 2026 hors<horsicq@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/* js_value.c - values, strings, property maps and objects. */

#include "js_internal.h"


/* --------------------------------------------------------------- strings  */

JSStr *jsstr_new(JSCtx *pCtx, const char *pData, size_t nSize)
{
    JSStr *pStr = (JSStr *)cd_malloc(sizeof(JSStr));

    pStr->nRef = 1;
    pStr->nSize = nSize;
    pStr->pData = (char *)cd_malloc(nSize + 1);

    if (nSize && pData) {
        x_memcpy(pStr->pData, pData, nSize);
    }

    pStr->pData[nSize] = 0;

    pStr->pPrevAll = NULL;
    pStr->pNextAll = pCtx->pAllStrings;

    if (pCtx->pAllStrings) {
        pCtx->pAllStrings->pPrevAll = pStr;
    }

    pCtx->pAllStrings = pStr;

    return pStr;
}

JSStr *jsstr_ref(JSStr *pStr)
{
    if (pStr) {
        pStr->nRef++;
    }

    return pStr;
}

void jsstr_unref(JSCtx *pCtx, JSStr *pStr)
{
    if (pStr == NULL) {
        return;
    }

    pStr->nRef--;

    if (pStr->nRef > 0) {
        return;
    }

    if (pStr->pPrevAll) {
        pStr->pPrevAll->pNextAll = pStr->pNextAll;
    } else {
        pCtx->pAllStrings = pStr->pNextAll;
    }

    if (pStr->pNextAll) {
        pStr->pNextAll->pPrevAll = pStr->pPrevAll;
    }

    cd_free(pStr->pData);
    cd_free(pStr);
}

/* -------------------------------------------------------------- property  */

void jsprops_init(JSPropMap *pMap)
{
    x_memset(pMap, 0, sizeof(*pMap));
}

/* Puts value into an entry: the entry takes over the caller's reference and
 * the previous one is released. A stored object also carries a count of the
 * property maps that hold it, and jsobj_unref refuses to free anything with
 * a map still pointing at it. A caller that releases one time too many then
 * costs a delayed reclamation - the entry lives until the final sweep, as
 * everything did before - instead of leaving a dangling entry behind.     */
void jsprop_store(JSCtx *pCtx, JSProp *pProp, JSVal value)
{
    JSVal old = pProp->value;

    if (value.tag == JT_OBJ) {
        value.u.o->nMapRefs++;
    }

    pProp->value = value;

    if (old.tag == JT_OBJ) {
        old.u.o->nMapRefs--;
    }

    js_release(pCtx, old);
}

void jsprops_free(JSCtx *pCtx, JSPropMap *pMap)
{
    size_t i = 0;

    for (i = 0; i < pMap->nSize; i++) {
        cd_free(pMap->pEntries[i].pKey);

        if (!pMap->pEntries[i].bDeleted) {
            if (pMap->pEntries[i].value.tag == JT_OBJ) {
                pMap->pEntries[i].value.u.o->nMapRefs--;
            }

            js_release(pCtx, pMap->pEntries[i].value);
        }
    }

    cd_free(pMap->pEntries);
    cd_free(pMap->pIndex);
    jsprops_init(pMap);
}

static void jsprops_rehash(JSPropMap *pMap)
{
    size_t nNew = pMap->nIndexSize ? pMap->nIndexSize * 2 : 16;
    size_t i = 0;

    while (nNew < (pMap->nSize + 1) * 2) {
        nNew *= 2;
    }

    cd_free(pMap->pIndex);
    pMap->pIndex = (cd_i32 *)cd_malloc(nNew * sizeof(cd_i32));
    pMap->nIndexSize = nNew;

    for (i = 0; i < nNew; i++) {
        pMap->pIndex[i] = -1;
    }

    for (i = 0; i < pMap->nSize; i++) {
        size_t nSlot = pMap->pEntries[i].nHash & (nNew - 1);

        while (pMap->pIndex[nSlot] != -1) {
            nSlot = (nSlot + 1) & (nNew - 1);
        }

        pMap->pIndex[nSlot] = (cd_i32)i;
    }
}

JSProp *jsprops_find_hashed(JSPropMap *pMap, const char *pKey, size_t nKeySize, cd_u32 nHash)
{
    size_t nSlot = 0;

    if (pMap->nIndexSize == 0) {
        return NULL;
    }

    nSlot = nHash & (pMap->nIndexSize - 1);

    for (;;) {
        cd_i32 nIndex = pMap->pIndex[nSlot];

        if (nIndex == -1) {
            return NULL;
        }

        {
            JSProp *pProp = &pMap->pEntries[nIndex];

            if ((pProp->nHash == nHash) && (pProp->nKeySize == nKeySize) && (x_memcmp(pProp->pKey, pKey, nKeySize) == 0)) {
                if (pProp->bDeleted) {
                    return NULL;
                }

                return pProp;
            }
        }

        nSlot = (nSlot + 1) & (pMap->nIndexSize - 1);
    }
}

JSProp *jsprops_find(JSPropMap *pMap, const char *pKey, size_t nKeySize)
{
    if (pMap->nIndexSize == 0) {
        return NULL;
    }

    return jsprops_find_hashed(pMap, pKey, nKeySize, cd_hash_str(pKey, nKeySize));
}

JSProp *jsprops_put(JSCtx *pCtx, JSPropMap *pMap, const char *pKey, size_t nKeySize)
{
    cd_u32 nHash = cd_hash_str(pKey, nKeySize);
    size_t nSlot = 0;
    JSProp *pProp = NULL;

    (void)pCtx;

    if (pMap->nIndexSize == 0) {
        jsprops_rehash(pMap);
    }

    nSlot = nHash & (pMap->nIndexSize - 1);

    for (;;) {
        cd_i32 nIndex = pMap->pIndex[nSlot];

        if (nIndex == -1) {
            break;
        }

        pProp = &pMap->pEntries[nIndex];

        if ((pProp->nHash == nHash) && (pProp->nKeySize == nKeySize) && (x_memcmp(pProp->pKey, pKey, nKeySize) == 0)) {
            if (pProp->bDeleted) {
                pProp->bDeleted = 0;
                pProp->bDontEnum = 0;
                pProp->value = js_undefined();
                pMap->nLive++;
            }

            return pProp;
        }

        nSlot = (nSlot + 1) & (pMap->nIndexSize - 1);
    }

    if (pMap->nSize + 1 > pMap->nCapacity) {
        size_t nNew = pMap->nCapacity ? pMap->nCapacity * 2 : 8;

        pMap->pEntries = (JSProp *)cd_realloc(pMap->pEntries, nNew * sizeof(JSProp));
        pMap->nCapacity = nNew;
    }

    pProp = &pMap->pEntries[pMap->nSize];
    pProp->pKey = cd_strndup(pKey, nKeySize);
    pProp->nKeySize = nKeySize;
    pProp->nHash = nHash;
    pProp->value = js_undefined();
    pProp->bDeleted = 0;
    pProp->bDontEnum = 0;

    pMap->pIndex[nSlot] = (cd_i32)pMap->nSize;
    pMap->nSize++;
    pMap->nLive++;

    if ((pMap->nSize + 1) * 2 > pMap->nIndexSize) {
        jsprops_rehash(pMap);
    }

    return pProp;
}

int jsprops_del(JSCtx *pCtx, JSPropMap *pMap, const char *pKey)
{
    JSProp *pProp = jsprops_find(pMap, pKey, x_strlen(pKey));

    if (pProp == NULL) {
        return 0;
    }

    jsprop_store(pCtx, pProp, js_undefined());
    pProp->bDeleted = 1;
    pMap->nLive--;

    return 1;
}

/* --------------------------------------------------------------- objects  */

JSObj *jsobj_new(JSCtx *pCtx, JSClass cls, JSObj *pProto)
{
    JSObj *pObj = (JSObj *)cd_calloc(1, sizeof(JSObj));

    pObj->nRef = 1;
    pObj->cls = cls;
    pObj->pProto = pProto ? jsobj_ref(pProto) : NULL;
    pObj->bExtensible = 1;
    pObj->primitive = js_undefined();
    pObj->boundThis = js_undefined();

    jsprops_init(&pObj->props);

    pObj->pPrevAll = NULL;
    pObj->pNextAll = pCtx->pAllObjects;

    if (pCtx->pAllObjects) {
        pCtx->pAllObjects->pPrevAll = pObj;
    }

    pCtx->pAllObjects = pObj;

    return pObj;
}

JSObj *jsobj_ref(JSObj *pObj)
{
    if (pObj) {
        pObj->nRef++;
    }

    return pObj;
}

/* Deepest chain of nested frees; anything below is handed to the final
 * sweep rather than risking the native stack on a long object chain.     */
#define JS_MAX_FREE_DEPTH 96

void jsobj_unref(JSCtx *pCtx, JSObj *pObj)
{
    int i = 0;

    if (pObj == NULL) {
        return;
    }

    pObj->nRef--;

    if (pObj->nRef > 0) {
        return;
    }

    /* Objects that take part in a reference cycle - a closure and its scope,
     * a constructor and its prototype - never reach zero and are reclaimed
     * by the final sweep in js_free. Everything else goes now.            */
    if (pObj->bSweeping || (pObj->nMapRefs > 0) || (pCtx->nFreeDepth >= JS_MAX_FREE_DEPTH)) {
        return;
    }

    pObj->bSweeping = 1;

    if (pObj->pPrevAll) {
        pObj->pPrevAll->pNextAll = pObj->pNextAll;
    } else {
        pCtx->pAllObjects = pObj->pNextAll;
    }

    if (pObj->pNextAll) {
        pObj->pNextAll->pPrevAll = pObj->pPrevAll;
    }

    pCtx->nFreeDepth++;

    jsprops_free(pCtx, &pObj->props);
    jsobj_unref(pCtx, pObj->pProto);
    jsobj_unref(pCtx, pObj->pBoundTarget);
    jsscope_unref(pCtx, pObj->pScope);
    js_release(pCtx, pObj->primitive);
    js_release(pCtx, pObj->boundThis);

    for (i = 0; i < pObj->nBoundArgs; i++) {
        js_release(pCtx, pObj->pBoundArgs[i]);
    }

    pCtx->nFreeDepth--;

    cd_free(pObj->pBoundArgs);
    cd_free(pObj->pFnName);

    if (pObj->pRegExp) {
        jsregexp_free(pObj->pRegExp);
    }

    cd_free(pObj);
}

JSScope *jsscope_new(JSCtx *pCtx, JSScope *pParent)
{
    JSScope *pScope = (JSScope *)cd_calloc(1, sizeof(JSScope));

    pScope->nRef = 1;
    pScope->pVars = jsobj_new(pCtx, JCLASS_SCOPE, NULL);
    pScope->pParent = pParent ? jsscope_ref(pParent) : NULL;

    pScope->pPrevAll = NULL;
    pScope->pNextAll = pCtx->pAllScopes;

    if (pCtx->pAllScopes) {
        pCtx->pAllScopes->pPrevAll = pScope;
    }

    pCtx->pAllScopes = pScope;

    return pScope;
}

JSScope *jsscope_ref(JSScope *pScope)
{
    if (pScope) {
        pScope->nRef++;
    }

    return pScope;
}

void jsscope_unref(JSCtx *pCtx, JSScope *pScope)
{
    if (pScope == NULL) {
        return;
    }

    pScope->nRef--;

    if (pScope->nRef > 0) {
        return;
    }

    if (pCtx->nFreeDepth >= JS_MAX_FREE_DEPTH) {
        return;
    }

    if (pScope->pPrevAll) {
        pScope->pPrevAll->pNextAll = pScope->pNextAll;
    } else {
        pCtx->pAllScopes = pScope->pNextAll;
    }

    if (pScope->pNextAll) {
        pScope->pNextAll->pPrevAll = pScope->pPrevAll;
    }

    pCtx->nFreeDepth++;
    jsobj_unref(pCtx, pScope->pVars);
    jsscope_unref(pCtx, pScope->pParent);
    pCtx->nFreeDepth--;

    cd_free(pScope);
}

/* ---------------------------------------------------------------- values  */

JSVal js_undefined(void)
{
    JSVal value;

    value.tag = JT_UNDEF;
    value.u.n = 0;

    return value;
}

JSVal js_null(void)
{
    JSVal value;

    value.tag = JT_NULL;
    value.u.n = 0;

    return value;
}

JSVal js_bool(int bValue)
{
    JSVal value;

    value.tag = JT_BOOL;
    value.u.b = bValue ? 1 : 0;

    return value;
}

JSVal js_num(double nValue)
{
    JSVal value;

    value.tag = JT_NUM;
    value.u.n = nValue;

    return value;
}

JSVal js_int(cd_i64 nValue)
{
    return js_num((double)nValue);
}

JSVal jsval_obj(JSObj *pObj)
{
    JSVal value;

    value.tag = JT_OBJ;
    value.u.o = pObj;

    return value;
}

JSVal jsval_str(JSStr *pStr)
{
    JSVal value;

    value.tag = JT_STR;
    value.u.s = pStr;

    return value;
}

JSVal js_str(JSCtx *pCtx, const char *pString)
{
    return js_strn(pCtx, pString, pString ? x_strlen(pString) : 0);
}

JSVal js_strn(JSCtx *pCtx, const char *pString, size_t nSize)
{
    return jsval_str(jsstr_new(pCtx, pString, nSize));
}

JSVal js_dup(JSVal value)
{
    if (value.tag == JT_STR) {
        jsstr_ref(value.u.s);
    } else if (value.tag == JT_OBJ) {
        jsobj_ref(value.u.o);
    }

    return value;
}

void js_release(JSCtx *pCtx, JSVal value)
{
    if (value.tag == JT_STR) {
        jsstr_unref(pCtx, value.u.s);
    } else if (value.tag == JT_OBJ) {
        jsobj_unref(pCtx, value.u.o);
    }
}

int js_is_undefined(JSVal value)
{
    return (value.tag == JT_UNDEF) ? 1 : 0;
}

int js_is_callable(JSVal value)
{
    if (value.tag != JT_OBJ) {
        return 0;
    }

    return ((value.u.o->cls == JCLASS_FUNCTION) || (value.u.o->cls == JCLASS_NATIVE)) ? 1 : 0;
}

const char *js_str_data(JSVal value)
{
    if (value.tag == JT_STR) {
        return value.u.s->pData;
    }

    return "";
}

size_t js_str_len(JSVal value)
{
    if (value.tag == JT_STR) {
        return value.u.s->nSize;
    }

    return 0;
}

/* ------------------------------------------------------------ conversion  */

int js_is_array_index(const char *pKey, size_t nKeySize, cd_i64 *pnIndex)
{
    size_t i = 0;
    cd_i64 nValue = 0;
    int bNegative = 0;

    if (nKeySize == 0) {
        return 0;
    }

    if (pKey[0] == '-') {
        bNegative = 1;
        i = 1;

        if (nKeySize == 1) {
            return 0;
        }
    }

    for (; i < nKeySize; i++) {
        if ((pKey[i] < '0') || (pKey[i] > '9')) {
            return 0;
        }

        nValue = nValue * 10 + (pKey[i] - '0');

        if (nValue > 0x7FFFFFFFll) {
            return 0;
        }
    }

    /* Reject "01" style keys - they are ordinary property names. */
    if ((nKeySize > (size_t)(bNegative ? 2 : 1)) && (pKey[bNegative ? 1 : 0] == '0')) {
        return 0;
    }

    if (pnIndex) {
        *pnIndex = bNegative ? -nValue : nValue;
    }

    return bNegative ? 0 : 1;
}

double js_string_to_number(const char *pData, size_t nSize)
{
    size_t nStart = 0;
    size_t nEnd = nSize;
    char *pCopy = NULL;
    const char *pEndPtr = NULL;
    double nResult = 0;

    while ((nStart < nEnd) && ((unsigned char)pData[nStart] <= ' ')) {
        nStart++;
    }

    while ((nEnd > nStart) && ((unsigned char)pData[nEnd - 1] <= ' ')) {
        nEnd--;
    }

    if (nStart == nEnd) {
        return 0;
    }

    pCopy = cd_strndup(pData + nStart, nEnd - nStart);

    if ((x_strncmp(pCopy, "0x", 2) == 0) || (x_strncmp(pCopy, "0X", 2) == 0)) {
        nResult = (double)x_strtoull(pCopy + 2, &pEndPtr, 16);
    } else if (x_strcmp(pCopy, "Infinity") == 0 || x_strcmp(pCopy, "+Infinity") == 0) {
        cd_free(pCopy);
        return x_inf();
    } else if (x_strcmp(pCopy, "-Infinity") == 0) {
        cd_free(pCopy);
        return (-x_inf());
    } else {
        nResult = x_strtod(pCopy, &pEndPtr);
    }

    if ((pEndPtr == NULL) || (*pEndPtr != 0)) {
        cd_free(pCopy);
        return x_nan();
    }

    cd_free(pCopy);

    return nResult;
}

JSVal js_to_primitive(JSCtx *pCtx, JSVal value, int bPreferString)
{
    static const char *pOrderString[2] = {"toString", "valueOf"};
    static const char *pOrderNumber[2] = {"valueOf", "toString"};
    const char **ppOrder = bPreferString ? pOrderString : pOrderNumber;
    int i = 0;

    if (value.tag != JT_OBJ) {
        return js_dup(value);
    }

    for (i = 0; i < 2; i++) {
        JSVal fn = jsobj_get(pCtx, value.u.o, ppOrder[i], x_strlen(ppOrder[i]));

        if (js_is_callable(fn)) {
            JSVal result = js_call(pCtx, fn, value, 0, NULL);

            js_release(pCtx, fn);

            if (pCtx->bException) {
                js_release(pCtx, result);
                return js_undefined();
            }

            if (result.tag != JT_OBJ) {
                return result;
            }

            js_release(pCtx, result);
        } else {
            js_release(pCtx, fn);
        }
    }

    return js_str(pCtx, "[object Object]");
}

double js_to_number(JSCtx *pCtx, JSVal value)
{
    switch (value.tag) {
        case JT_UNDEF: return x_nan();
        case JT_NULL: return 0;
        case JT_BOOL: return value.u.b ? 1 : 0;
        case JT_NUM: return value.u.n;
        case JT_STR: return js_string_to_number(value.u.s->pData, value.u.s->nSize);
        case JT_OBJ: {
            JSVal prim = js_to_primitive(pCtx, value, 0);
            double nResult = 0;

            if (prim.tag == JT_OBJ) {
                js_release(pCtx, prim);
                return x_nan();
            }

            nResult = js_to_number(pCtx, prim);
            js_release(pCtx, prim);

            return nResult;
        }

        default: break;
    }

    return x_nan();
}

int js_to_bool(JSCtx *pCtx, JSVal value)
{
    (void)pCtx;

    switch (value.tag) {
        case JT_UNDEF:
        case JT_NULL: return 0;
        case JT_BOOL: return value.u.b;
        case JT_NUM: return ((value.u.n != 0) && (!(value.u.n != value.u.n))) ? 1 : 0;
        case JT_STR: return (value.u.s->nSize > 0) ? 1 : 0;
        case JT_OBJ: return 1;
        default: break;
    }

    return 0;
}

/* ECMAScript Number::toString steps 5 onwards collapse to the plain decimal
 * digits when the value is an exact integer of at most 32 bits: the digit
 * count stays far below the exponent thresholds and the shortest round-trip
 * digits are the exact ones, so no dtoa is needed. Writes the terminated
 * text into pBuf (12 bytes are enough) and returns its length, or 0 when
 * the general path has to run.                                             */
int js_int_to_buf(char *pBuf, double nValue)
{
    char sTmp[12];
    cd_i64 nInt = 0;
    cd_u32 nAbs = 0;
    int nTmp = 0;
    int nOut = 0;

    /* Written so that NaN, which compares false either way, falls through. */
    if (!((nValue >= -2147483648.0) && (nValue <= 2147483648.0))) {
        return 0;
    }

    nInt = (cd_i64)nValue;

    if ((double)nInt != nValue) {
        return 0;
    }

    /* -0 takes this path too: it compares equal to 0 and String(-0) is "0". */
    if (nInt < 0) {
        pBuf[nOut++] = '-';
        nAbs = (cd_u32)(-nInt);
    } else {
        nAbs = (cd_u32)nInt;
    }

    do {
        sTmp[nTmp++] = (char)('0' + (int)(nAbs % 10));
        nAbs /= 10;
    } while (nAbs != 0);

    while (nTmp > 0) {
        pBuf[nOut++] = sTmp[--nTmp];
    }

    pBuf[nOut] = 0;

    return nOut;
}

/* ECMAScript Number::toString. x_dtoa_shortest implements the specification
 * directly - shortest round-tripping digits plus the exponent rules - so no
 * printf-style float formatting is involved anywhere in the engine.        */
static void js_double_to_buf(char *pBuf, size_t nBufSize, double nValue)
{
    if ((nBufSize >= 12) && (js_int_to_buf(pBuf, nValue) > 0)) {
        return;
    }

    x_dtoa_shortest(nValue, pBuf, nBufSize);
}

JSVal js_number_to_string(JSCtx *pCtx, double nValue, int nRadix)
{
    char sBuf[64];

    if ((nRadix == 10) || (nRadix == 0)) {
        js_double_to_buf(sBuf, sizeof(sBuf), nValue);

        return js_str(pCtx, sBuf);
    }

    if (nValue != nValue) {
        return js_str(pCtx, "NaN");
    }

    if (nValue == x_inf()) {
        return js_str(pCtx, "Infinity");
    }

    if (nValue == (-x_inf())) {
        return js_str(pCtx, "-Infinity");
    }

    /* Everything below stays in double arithmetic: a value of 2^64 or more
     * cannot be converted to an integer type, and the digit sequence has to
     * match the reference engine, which divides and multiplies doubles.   */
    {
        static const char *pDigits = "0123456789abcdefghijklmnopqrstuvwxyz";
        /* A double needs at most 1075 binary digits on either side of the
         * point, plus the sign and the terminator.                        */
        char sTmp[1088];
        int nPos = (int)sizeof(sTmp);
        int bNegative = (nValue < 0) ? 1 : 0;
        double nAbs = bNegative ? -nValue : nValue;
        double nFrac = nAbs - x_floor(nAbs);
        double nInteger = x_floor(nAbs);

        sTmp[--nPos] = 0;

        do {
            sTmp[--nPos] = pDigits[(int)x_fmod(nInteger, (double)nRadix)];
            nInteger = x_floor(nInteger / nRadix);
        } while ((nInteger != 0) && (nPos > 1));

        if (bNegative) {
            sTmp[--nPos] = '-';
        }

        if (nFrac > 0) {
            CDBuf buf;
            int i = 0;
            JSVal result;

            cdbuf_init(&buf);
            cdbuf_append_str(&buf, sTmp + nPos);
            cdbuf_append_ch(&buf, '.');

            for (i = 0; (i < 20) && (nFrac > 0); i++) {
                int nDigit = 0;

                nFrac *= nRadix;
                nDigit = (int)x_floor(nFrac);
                nFrac -= nDigit;
                cdbuf_append_ch(&buf, pDigits[nDigit]);
            }

            result = js_strn(pCtx, buf.pData, buf.nSize);
            cdbuf_free(&buf);

            return result;
        }

        return js_str(pCtx, sTmp + nPos);
    }
}

JSVal js_to_string(JSCtx *pCtx, JSVal value)
{
    switch (value.tag) {
        case JT_UNDEF: return js_str(pCtx, "undefined");
        case JT_NULL: return js_str(pCtx, "null");
        case JT_BOOL: return js_str(pCtx, value.u.b ? "true" : "false");
        case JT_NUM: return js_number_to_string(pCtx, value.u.n, 10);
        case JT_STR: return js_dup(value);
        case JT_OBJ: {
            JSVal prim = js_to_primitive(pCtx, value, 1);
            JSVal result;

            if (prim.tag == JT_OBJ) {
                js_release(pCtx, prim);
                return js_str(pCtx, "[object Object]");
            }

            result = js_to_string(pCtx, prim);
            js_release(pCtx, prim);

            return result;
        }

        default: break;
    }

    return js_str(pCtx, "");
}

char *js_to_cstr(JSCtx *pCtx, JSVal value)
{
    JSVal str = js_to_string(pCtx, value);
    char *pResult = cd_strndup(js_str_data(str), js_str_len(str));

    js_release(pCtx, str);

    return pResult;
}

cd_i64 js_to_int64(JSCtx *pCtx, JSVal value)
{
    double nValue = js_to_number(pCtx, value);

    if (nValue != nValue) {
        return 0;
    }

    if (nValue >= 9.2233720368547758e18) {
        return 0x7FFFFFFFFFFFFFFFll;
    }

    if (nValue <= -9.2233720368547758e18) {
        return (-0x7FFFFFFFFFFFFFFFll - 1);
    }

    return (cd_i64)nValue;
}

cd_i32 js_to_int32(JSCtx *pCtx, JSVal value)
{
    double nValue = js_to_number(pCtx, value);
    double nTrunc = 0;
    cd_u32 nBits = 0;

    if ((nValue != nValue) || (nValue == x_inf()) || (nValue == (-x_inf()))) {
        return 0;
    }

    nTrunc = (nValue < 0) ? -x_floor(-nValue) : x_floor(nValue);
    nTrunc = x_fmod(nTrunc, 4294967296.0);

    if (nTrunc < 0) {
        nTrunc += 4294967296.0;
    }

    nBits = (cd_u32)nTrunc;

    return (cd_i32)nBits;
}

JSVal js_concat_str(JSCtx *pCtx, JSVal left, JSVal right)
{
    JSVal a = js_to_string(pCtx, left);
    JSVal b = js_to_string(pCtx, right);
    size_t nSizeA = js_str_len(a);
    size_t nSizeB = js_str_len(b);
    JSStr *pStr = jsstr_new(pCtx, NULL, nSizeA + nSizeB);

    x_memcpy(pStr->pData, js_str_data(a), nSizeA);
    x_memcpy(pStr->pData + nSizeA, js_str_data(b), nSizeB);
    pStr->pData[nSizeA + nSizeB] = 0;

    js_release(pCtx, a);
    js_release(pCtx, b);

    return jsval_str(pStr);
}

/* ---------------------------------------------------------- object model  */

static JSVal jsobj_get_own_hashed(JSCtx *pCtx, JSObj *pObj, const char *pKey, size_t nKeySize, cd_u32 nHash, int *pbFound)
{
    JSProp *pProp = NULL;

    if (pbFound) {
        *pbFound = 0;
    }

    if (pObj == NULL) {
        return js_undefined();
    }

    /* Array/String length and string indices are computed. */
    if ((nKeySize == 6) && (x_memcmp(pKey, "length", 6) == 0)) {
        if (pObj->cls == JCLASS_ARRAY) {
            if (pbFound) {
                *pbFound = 1;
            }

            return js_num((double)pObj->nArrayLen);
        }

        if (pObj->cls == JCLASS_STRING) {
            if (pbFound) {
                *pbFound = 1;
            }

            return js_num((double)js_str_len(pObj->primitive));
        }
    }

    if (pObj->cls == JCLASS_STRING) {
        cd_i64 nIndex = 0;

        if (js_is_array_index(pKey, nKeySize, &nIndex)) {
            const char *pData = js_str_data(pObj->primitive);
            size_t nSize = js_str_len(pObj->primitive);

            if ((nIndex >= 0) && ((size_t)nIndex < nSize)) {
                if (pbFound) {
                    *pbFound = 1;
                }

                return js_strn(pCtx, pData + nIndex, 1);
            }
        }
    }

    pProp = jsprops_find_hashed(&pObj->props, pKey, nKeySize, nHash);

    if (pProp) {
        if (pbFound) {
            *pbFound = 1;
        }

        return js_dup(pProp->value);
    }

    return js_undefined();
}

JSVal jsobj_get_own(JSCtx *pCtx, JSObj *pObj, const char *pKey, size_t nKeySize, int *pbFound)
{
    if (pObj == NULL) {
        if (pbFound) {
            *pbFound = 0;
        }

        return js_undefined();
    }

    return jsobj_get_own_hashed(pCtx, pObj, pKey, nKeySize, cd_hash_str(pKey, nKeySize), pbFound);
}

/* The key hash is computed once and reused for the whole prototype walk. */
JSVal jsobj_get(JSCtx *pCtx, JSObj *pObj, const char *pKey, size_t nKeySize)
{
    JSObj *pCurrent = pObj;
    cd_u32 nHash = 0;

    if (pCurrent == NULL) {
        return js_undefined();
    }

    nHash = cd_hash_str(pKey, nKeySize);

    while (pCurrent) {
        int bFound = 0;
        JSVal value = jsobj_get_own_hashed(pCtx, pCurrent, pKey, nKeySize, nHash, &bFound);

        if (bFound) {
            return value;
        }

        js_release(pCtx, value);
        pCurrent = pCurrent->pProto;
    }

    return js_undefined();
}

int jsobj_has(JSCtx *pCtx, JSObj *pObj, const char *pKey, size_t nKeySize)
{
    JSObj *pCurrent = pObj;
    cd_u32 nHash = 0;

    if (pCurrent == NULL) {
        return 0;
    }

    nHash = cd_hash_str(pKey, nKeySize);

    while (pCurrent) {
        int bFound = 0;
        JSVal value = jsobj_get_own_hashed(pCtx, pCurrent, pKey, nKeySize, nHash, &bFound);

        js_release(pCtx, value);

        if (bFound) {
            return 1;
        }

        pCurrent = pCurrent->pProto;
    }

    return 0;
}

void jsobj_put(JSCtx *pCtx, JSObj *pObj, const char *pKey, size_t nKeySize, JSVal value)
{
    JSProp *pProp = NULL;

    if (pObj == NULL) {
        js_release(pCtx, value);
        return;
    }

    if ((pObj->cls == JCLASS_ARRAY) && (nKeySize == 6) && (x_memcmp(pKey, "length", 6) == 0)) {
        cd_i64 nNewLen = js_to_int64(pCtx, value);
        cd_i64 i = 0;

        if (nNewLen < 0) {
            nNewLen = 0;
        }

        /* An array index never exceeds a uint32, so neither may the length. */
        if (nNewLen > 0xFFFFFFFFll) {
            nNewLen = 0xFFFFFFFFll;
        }

        if ((pObj->nArrayLen - nNewLen) > (cd_i64)pObj->props.nSize) {
            /* Far more nominal slots than stored properties: walking the
             * map is bounded by what actually exists.                     */
            size_t j = 0;

            for (j = 0; j < pObj->props.nSize; j++) {
                cd_i64 nIndex = 0;

                if (pObj->props.pEntries[j].bDeleted) {
                    continue;
                }

                if (js_is_array_index(pObj->props.pEntries[j].pKey, x_strlen(pObj->props.pEntries[j].pKey), &nIndex) && (nIndex >= nNewLen)) {
                    jsprops_del(pCtx, &pObj->props, pObj->props.pEntries[j].pKey);
                }
            }
        } else {
            for (i = nNewLen; i < pObj->nArrayLen; i++) {
                char sKey[32];

                x_snprintf(sKey, sizeof(sKey), "%lld", (long long)i);
                jsprops_del(pCtx, &pObj->props, sKey);
            }
        }

        pObj->nArrayLen = nNewLen;
        js_release(pCtx, value);

        return;
    }

    if (pObj->cls == JCLASS_ARRAY) {
        cd_i64 nIndex = 0;

        if (js_is_array_index(pKey, nKeySize, &nIndex)) {
            if (nIndex >= pObj->nArrayLen) {
                pObj->nArrayLen = nIndex + 1;
            }
        }
    }

    pProp = jsprops_put(pCtx, &pObj->props, pKey, nKeySize);
    jsprop_store(pCtx, pProp, value);
}

void jsobj_put_hidden(JSCtx *pCtx, JSObj *pObj, const char *pKey, JSVal value)
{
    JSProp *pProp = jsprops_put(pCtx, &pObj->props, pKey, x_strlen(pKey));

    jsprop_store(pCtx, pProp, value);
    pProp->bDontEnum = 1;
}

/* ------------------------------------------------------------ public API  */

JSVal js_new_object(JSCtx *pCtx)
{
    return jsval_obj(jsobj_new(pCtx, JCLASS_OBJECT, pCtx->pObjectProto));
}

JSVal js_new_array(JSCtx *pCtx)
{
    return jsval_obj(jsobj_new(pCtx, JCLASS_ARRAY, pCtx->pArrayProto));
}

JSVal js_new_native(JSCtx *pCtx, const char *pName, JSNativeFn fn, int nArgc, void *pUser)
{
    JSObj *pObj = jsobj_new(pCtx, JCLASS_NATIVE, pCtx->pFunctionProto);

    pObj->nativeFn = fn;
    pObj->pUser = pUser;
    pObj->nNativeArgc = nArgc;
    pObj->pFnName = cd_strdup(pName ? pName : "");

    jsobj_put_hidden(pCtx, pObj, "length", js_num(nArgc));
    jsobj_put_hidden(pCtx, pObj, "name", js_str(pCtx, pName ? pName : ""));

    return jsval_obj(pObj);
}

JSVal js_get(JSCtx *pCtx, JSVal object, const char *pKey)
{
    if (object.tag != JT_OBJ) {
        return js_undefined();
    }

    return jsobj_get(pCtx, object.u.o, pKey, x_strlen(pKey));
}

void js_set(JSCtx *pCtx, JSVal object, const char *pKey, JSVal value)
{
    if (object.tag != JT_OBJ) {
        js_release(pCtx, value);
        return;
    }

    jsobj_put(pCtx, object.u.o, pKey, x_strlen(pKey), value);
}

int js_has(JSCtx *pCtx, JSVal object, const char *pKey)
{
    if (object.tag != JT_OBJ) {
        return 0;
    }

    return jsobj_has(pCtx, object.u.o, pKey, x_strlen(pKey));
}

JSVal js_get_index(JSCtx *pCtx, JSVal object, cd_i64 nIndex)
{
    char sKey[32];

    x_snprintf(sKey, sizeof(sKey), "%lld", (long long)nIndex);

    return js_get(pCtx, object, sKey);
}

void js_set_index(JSCtx *pCtx, JSVal object, cd_i64 nIndex, JSVal value)
{
    char sKey[32];

    x_snprintf(sKey, sizeof(sKey), "%lld", (long long)nIndex);
    js_set(pCtx, object, sKey, value);
}

cd_i64 js_array_length(JSCtx *pCtx, JSVal array)
{
    if ((array.tag == JT_OBJ) && (array.u.o->cls == JCLASS_ARRAY)) {
        return array.u.o->nArrayLen;
    }

    {
        JSVal length = js_get(pCtx, array, "length");
        cd_i64 nResult = js_to_int64(pCtx, length);

        js_release(pCtx, length);

        return nResult;
    }
}

void js_array_push(JSCtx *pCtx, JSVal array, JSVal value)
{
    if ((array.tag == JT_OBJ) && (array.u.o->cls == JCLASS_ARRAY)) {
        js_set_index(pCtx, array, array.u.o->nArrayLen, value);
    } else {
        js_release(pCtx, value);
    }
}

JSVal js_global(JSCtx *pCtx)
{
    return jsval_obj(jsobj_ref(pCtx->pGlobal));
}

void js_def_fn(JSCtx *pCtx, const char *pName, JSNativeFn fn, int nArgc, void *pUser)
{
    jsobj_put_hidden(pCtx, pCtx->pGlobal, pName, js_new_native(pCtx, pName, fn, nArgc, pUser));
}

void js_def_method(JSCtx *pCtx, JSVal object, const char *pName, JSNativeFn fn, int nArgc, void *pUser)
{
    if (object.tag == JT_OBJ) {
        jsobj_put_hidden(pCtx, object.u.o, pName, js_new_native(pCtx, pName, fn, nArgc, pUser));
    }
}

/* -------------------------------------------------------------- equality  */

int js_strict_equals(JSCtx *pCtx, JSVal left, JSVal right)
{
    (void)pCtx;

    if (left.tag != right.tag) {
        return 0;
    }

    switch (left.tag) {
        case JT_UNDEF:
        case JT_NULL: return 1;
        case JT_BOOL: return (left.u.b == right.u.b) ? 1 : 0;
        case JT_NUM: return (left.u.n == right.u.n) ? 1 : 0;
        case JT_STR:
            if (left.u.s == right.u.s) {
                return 1;
            }

            return ((left.u.s->nSize == right.u.s->nSize) && (x_memcmp(left.u.s->pData, right.u.s->pData, left.u.s->nSize) == 0)) ? 1 : 0;
        case JT_OBJ: return (left.u.o == right.u.o) ? 1 : 0;
        default: break;
    }

    return 0;
}

int js_loose_equals(JSCtx *pCtx, JSVal left, JSVal right)
{
    if (left.tag == right.tag) {
        return js_strict_equals(pCtx, left, right);
    }

    if (((left.tag == JT_NULL) && (right.tag == JT_UNDEF)) || ((left.tag == JT_UNDEF) && (right.tag == JT_NULL))) {
        return 1;
    }

    if ((left.tag == JT_NULL) || (left.tag == JT_UNDEF) || (right.tag == JT_NULL) || (right.tag == JT_UNDEF)) {
        return 0;
    }

    if ((left.tag == JT_NUM) && (right.tag == JT_STR)) {
        return (left.u.n == js_to_number(pCtx, right)) ? 1 : 0;
    }

    if ((left.tag == JT_STR) && (right.tag == JT_NUM)) {
        return (js_to_number(pCtx, left) == right.u.n) ? 1 : 0;
    }

    if (left.tag == JT_BOOL) {
        JSVal converted = js_num(left.u.b ? 1 : 0);

        return js_loose_equals(pCtx, converted, right);
    }

    if (right.tag == JT_BOOL) {
        JSVal converted = js_num(right.u.b ? 1 : 0);

        return js_loose_equals(pCtx, left, converted);
    }

    if (((left.tag == JT_NUM) || (left.tag == JT_STR)) && (right.tag == JT_OBJ)) {
        JSVal prim = js_to_primitive(pCtx, right, 0);
        int bResult = js_loose_equals(pCtx, left, prim);

        js_release(pCtx, prim);

        return bResult;
    }

    if ((left.tag == JT_OBJ) && ((right.tag == JT_NUM) || (right.tag == JT_STR))) {
        JSVal prim = js_to_primitive(pCtx, left, 0);
        int bResult = js_loose_equals(pCtx, prim, right);

        js_release(pCtx, prim);

        return bResult;
    }

    return 0;
}

/* ------------------------------------------------------------ exceptions  */

int js_has_exception(JSCtx *pCtx)
{
    return pCtx->bException;
}

JSVal js_throw_value(JSCtx *pCtx, JSVal value)
{
    if (!pCtx->bException) {
        /* The text has to be produced before the flag is raised: with the
         * flag set js_to_primitive refuses to call the value's toString and
         * every object would report "undefined".                          */
        JSVal text = js_to_string(pCtx, value);

        if (pCtx->bException) {
            /* Stringifying threw; that exception wins. */
            js_release(pCtx, text);
            js_release(pCtx, value);

            return js_undefined();
        }

        pCtx->bException = 1;
        pCtx->exception = value;

        cd_free(pCtx->pErrorText);
        pCtx->pErrorText = cd_strndup(js_str_data(text), js_str_len(text));
        js_release(pCtx, text);
    } else {
        js_release(pCtx, value);
    }

    return js_undefined();
}

X_PRINTF_LIKE(2, 3) JSVal js_throw(JSCtx *pCtx, const char *pFormat, ...)
{
    char sBuf[1024];
    X_VA_LIST args;

    X_VA_START(args, pFormat);
    x_vsnprintf(sBuf, sizeof(sBuf), pFormat, args);
    X_VA_END(args);

    return js_throw_value(pCtx, js_str(pCtx, sBuf));
}

const char *js_error(JSCtx *pCtx)
{
    return pCtx->pErrorText ? pCtx->pErrorText : "";
}

void js_clear_error(JSCtx *pCtx)
{
    if (pCtx->bException) {
        js_release(pCtx, pCtx->exception);
    }

    pCtx->bException = 0;
    pCtx->exception = js_undefined();
    cd_free(pCtx->pErrorText);
    pCtx->pErrorText = NULL;
}

void js_set_user(JSCtx *pCtx, void *pUser)
{
    pCtx->pUser = pUser;
}

void *js_get_user(JSCtx *pCtx)
{
    return pCtx->pUser;
}
