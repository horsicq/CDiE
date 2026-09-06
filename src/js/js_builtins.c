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

/* js_builtins.c - standard library: Object, Function, Array, String, Number,
 * Boolean, Math, JSON, RegExp and the global helper functions.             */

#include "js_internal.h"


#define ARG(i) (((i) < nArgc) ? pArgv[i] : js_undefined())

/* Upper bounds on the two places where a nominal array length is turned
 * into a single flat allocation.  Both are defensive: an array that really
 * holds this many elements has already cost far more memory in the property
 * map, so no realistic script is refused, while the multiplication by
 * sizeof(JSVal) can no longer wrap size_t and the count still fits an int. */
#define JS_MAX_SPREAD_ARGS 16777216
#define JS_MAX_SORT_ITEMS  16777216

/* ------------------------------------------------------------- utilities  */

static JSVal this_string(JSCtx *pCtx, JSVal thisVal)
{
    if (thisVal.tag == JT_STR) {
        return js_dup(thisVal);
    }

    if ((thisVal.tag == JT_OBJ) && (thisVal.u.o->cls == JCLASS_STRING)) {
        return js_dup(thisVal.u.o->primitive);
    }

    return js_to_string(pCtx, thisVal);
}

static double this_number(JSCtx *pCtx, JSVal thisVal)
{
    if (thisVal.tag == JT_NUM) {
        return thisVal.u.n;
    }

    if ((thisVal.tag == JT_OBJ) && (thisVal.u.o->cls == JCLASS_NUMBER)) {
        return js_to_number(pCtx, thisVal.u.o->primitive);
    }

    return js_to_number(pCtx, thisVal);
}

static cd_i64 clamp_index(double nValue, cd_i64 nLength)
{
    cd_i64 nResult = 0;

    if (nValue != nValue) {
        return 0;
    }

    /* ToInteger truncates towards zero FIRST; only then is the sign tested.
     * Without this, -0.5 truncates to -0 but still takes the negative
     * branch and yields nLength instead of 0.                             */
    nValue = (nValue < 0) ? (-x_floor(-nValue)) : x_floor(nValue);

    /* The double has to be brought into range before the cast: converting a
     * value that does not fit into cd_i64 is undefined behaviour.          */
    if (nValue < 0) {
        if (nValue < (-(double)nLength)) {
            return 0;
        }

        nResult = nLength + (cd_i64)nValue;

        if (nResult < 0) {
            nResult = 0;
        }
    } else {
        if (nValue > (double)nLength) {
            return nLength;
        }

        nResult = (cd_i64)nValue;

        if (nResult > nLength) {
            nResult = nLength;
        }
    }

    return nResult;
}

/* ---------------------------------------------------------------- Object  */

static JSVal fn_object_ctor(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    (void)thisVal;
    (void)pUser;

    if ((nArgc > 0) && (pArgv[0].tag == JT_OBJ)) {
        return js_dup(pArgv[0]);
    }

    return js_new_object(pCtx);
}

static JSVal fn_object_keys(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    JSVal result = js_new_array(pCtx);
    JSVal object = ARG(0);
    cd_i64 nCount = 0;

    (void)thisVal;
    (void)pUser;

    if (object.tag == JT_OBJ) {
        JSPropMap *pMap = &object.u.o->props;
        size_t i = 0;

        for (i = 0; i < pMap->nSize; i++) {
            if ((!pMap->pEntries[i].bDeleted) && (!pMap->pEntries[i].bDontEnum)) {
                js_set_index(pCtx, result, nCount++, js_str(pCtx, pMap->pEntries[i].pKey));
            }
        }
    }

    return result;
}

static JSVal fn_object_hasownproperty(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    JSVal key = js_to_string(pCtx, ARG(0));
    int bResult = 0;

    (void)pUser;

    if (thisVal.tag == JT_OBJ) {
        int bFound = 0;
        JSVal value = jsobj_get_own(pCtx, thisVal.u.o, js_str_data(key), js_str_len(key), &bFound);

        js_release(pCtx, value);
        bResult = bFound;
    } else if (thisVal.tag == JT_STR) {
        cd_i64 nIndex = 0;

        if ((js_str_len(key) == 6) && (x_memcmp(js_str_data(key), "length", 6) == 0)) {
            bResult = 1;
        } else if (js_is_array_index(js_str_data(key), js_str_len(key), &nIndex)) {
            bResult = ((nIndex >= 0) && ((size_t)nIndex < js_str_len(thisVal))) ? 1 : 0;
        }
    }

    js_release(pCtx, key);

    return js_bool(bResult);
}

static JSVal fn_object_tostring(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    (void)nArgc;
    (void)pArgv;
    (void)pUser;

    if ((thisVal.tag == JT_OBJ) && (thisVal.u.o->cls == JCLASS_ARRAY)) {
        JSVal join = js_get(pCtx, thisVal, "join");

        if (js_is_callable(join)) {
            JSVal result = js_call(pCtx, join, thisVal, 0, NULL);

            js_release(pCtx, join);

            return result;
        }

        js_release(pCtx, join);
    }

    return js_str(pCtx, "[object Object]");
}

static JSVal fn_object_valueof(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    (void)pCtx;
    (void)nArgc;
    (void)pArgv;
    (void)pUser;

    return js_dup(thisVal);
}

static JSVal fn_object_defineproperty(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    JSVal object = ARG(0);
    JSVal key = js_to_string(pCtx, ARG(1));
    JSVal descriptor = ARG(2);

    (void)thisVal;
    (void)pUser;

    if ((object.tag == JT_OBJ) && (descriptor.tag == JT_OBJ)) {
        JSVal value = js_get(pCtx, descriptor, "value");
        JSVal enumerable = js_get(pCtx, descriptor, "enumerable");
        JSProp *pProp = NULL;

        jsobj_put(pCtx, object.u.o, js_str_data(key), js_str_len(key), value);
        pProp = jsprops_find(&object.u.o->props, js_str_data(key), js_str_len(key));

        if (pProp && (!js_to_bool(pCtx, enumerable))) {
            pProp->bDontEnum = 1;
        }

        js_release(pCtx, enumerable);
    }

    js_release(pCtx, key);

    return js_dup(object);
}

/* -------------------------------------------------------------- Function  */

static JSVal fn_function_call(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    JSVal newThis = ARG(0);

    (void)pUser;

    if (!js_is_callable(thisVal)) {
        return js_throw(pCtx, "TypeError: Function.prototype.call on a non-function");
    }

    return js_call_function(pCtx, thisVal.u.o, newThis, (nArgc > 0) ? (nArgc - 1) : 0, (nArgc > 1) ? (pArgv + 1) : NULL, 0);
}

static JSVal fn_function_apply(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    JSVal newThis = ARG(0);
    JSVal args = ARG(1);
    JSVal *pCallArgs = NULL;
    cd_i64 nCount = 0;
    cd_i64 i = 0;
    JSVal result;

    (void)pUser;

    if (!js_is_callable(thisVal)) {
        return js_throw(pCtx, "TypeError: Function.prototype.apply on a non-function");
    }

    if (args.tag == JT_OBJ) {
        nCount = js_array_length(pCtx, args);

        if (nCount < 0) {
            nCount = 0;
        }

        /* Keeps the allocation below from wrapping size_t and the argument
         * count from being truncated by the (int) cast further down.  The
         * bound is far above any real argument list - the reference engine
         * still spreads half a million elements - but low enough that the
         * multiplication cannot wrap and the block stays bounded.          */
        if (nCount > JS_MAX_SPREAD_ARGS) {
            return js_throw(pCtx, "RangeError: too many arguments");
        }

        if (nCount > 0) {
            pCallArgs = (JSVal *)cd_malloc((size_t)nCount * sizeof(JSVal));

            for (i = 0; i < nCount; i++) {
                pCallArgs[i] = js_get_index(pCtx, args, i);
            }
        }
    }

    result = js_call_function(pCtx, thisVal.u.o, newThis, (int)nCount, pCallArgs, 0);

    for (i = 0; i < nCount; i++) {
        js_release(pCtx, pCallArgs[i]);
    }

    cd_free(pCallArgs);

    return result;
}

static JSVal fn_function_bind(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    JSVal result;

    (void)pUser;

    if (!js_is_callable(thisVal)) {
        return js_throw(pCtx, "TypeError: Function.prototype.bind on a non-function");
    }

    result = js_new_native(pCtx, "bound", NULL, 0, NULL);
    result.u.o->pBoundTarget = jsobj_ref(thisVal.u.o);
    result.u.o->boundThis = js_dup(ARG(0));

    /* Everything after thisArg is partially applied and goes in front of the
     * arguments of the eventual call.                                      */
    if (nArgc > 1) {
        int i = 0;

        result.u.o->pBoundArgs = (JSVal *)cd_malloc((size_t)(nArgc - 1) * sizeof(JSVal));
        result.u.o->nBoundArgs = nArgc - 1;

        for (i = 1; i < nArgc; i++) {
            result.u.o->pBoundArgs[i - 1] = js_dup(pArgv[i]);
        }
    }

    return result;
}

static JSVal fn_function_tostring(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    char sBuf[256];

    (void)nArgc;
    (void)pArgv;
    (void)pUser;

    x_snprintf(sBuf, sizeof(sBuf), "function %s() { [code] }", ((thisVal.tag == JT_OBJ) && thisVal.u.o->pFnName) ? thisVal.u.o->pFnName : "");

    return js_str(pCtx, sBuf);
}

/* ----------------------------------------------------------------- Array  */

static JSVal fn_array_ctor(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    JSVal result = js_new_array(pCtx);
    int i = 0;

    (void)thisVal;
    (void)pUser;

    if ((nArgc == 1) && (pArgv[0].tag == JT_NUM)) {
        double nValue = pArgv[0].u.n;

        /* Only a uint32 is a valid array length; anything else is a
         * RangeError and must never reach the (cd_i64) conversion.        */
        if ((nValue != nValue) || (nValue < 0) || (nValue > 4294967295.0) || (nValue != x_floor(nValue))) {
            js_release(pCtx, result);

            /* Word for word what the reference engine reports. */
            return js_throw(pCtx, "RangeError: Array size is not a small enough positive integer.");
        }

        result.u.o->nArrayLen = (cd_i64)nValue;

        return result;
    }

    for (i = 0; i < nArgc; i++) {
        js_set_index(pCtx, result, i, js_dup(pArgv[i]));
    }

    return result;
}

static JSVal fn_array_isarray(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    JSVal value = ARG(0);

    (void)pCtx;
    (void)thisVal;
    (void)pUser;

    return js_bool((value.tag == JT_OBJ) && (value.u.o->cls == JCLASS_ARRAY));
}

static JSVal fn_array_push(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    cd_i64 nLength = js_array_length(pCtx, thisVal);
    int i = 0;

    (void)pUser;

    for (i = 0; i < nArgc; i++) {
        js_set_index(pCtx, thisVal, nLength + i, js_dup(pArgv[i]));
    }

    return js_num((double)(nLength + nArgc));
}

static JSVal fn_array_pop(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    cd_i64 nLength = js_array_length(pCtx, thisVal);
    JSVal result;
    char sKey[32];

    (void)nArgc;
    (void)pArgv;
    (void)pUser;

    if (nLength <= 0) {
        return js_undefined();
    }

    result = js_get_index(pCtx, thisVal, nLength - 1);
    x_snprintf(sKey, sizeof(sKey), "%lld", (long long)(nLength - 1));

    if (thisVal.tag == JT_OBJ) {
        jsprops_del(pCtx, &thisVal.u.o->props, sKey);
        thisVal.u.o->nArrayLen = nLength - 1;
    }

    return result;
}

static JSVal fn_array_shift(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    cd_i64 nLength = js_array_length(pCtx, thisVal);
    JSVal result;
    cd_i64 i = 0;
    char sKey[32];

    (void)nArgc;
    (void)pArgv;
    (void)pUser;

    if (nLength <= 0) {
        return js_undefined();
    }

    result = js_get_index(pCtx, thisVal, 0);

    for (i = 1; i < nLength; i++) {
        js_set_index(pCtx, thisVal, i - 1, js_get_index(pCtx, thisVal, i));
    }

    x_snprintf(sKey, sizeof(sKey), "%lld", (long long)(nLength - 1));

    if (thisVal.tag == JT_OBJ) {
        jsprops_del(pCtx, &thisVal.u.o->props, sKey);
        thisVal.u.o->nArrayLen = nLength - 1;
    }

    return result;
}

static JSVal fn_array_unshift(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    cd_i64 nLength = js_array_length(pCtx, thisVal);
    cd_i64 i = 0;

    (void)pUser;

    for (i = nLength - 1; i >= 0; i--) {
        js_set_index(pCtx, thisVal, i + nArgc, js_get_index(pCtx, thisVal, i));
    }

    for (i = 0; i < nArgc; i++) {
        js_set_index(pCtx, thisVal, i, js_dup(pArgv[i]));
    }

    return js_num((double)(nLength + nArgc));
}

static JSVal fn_array_join(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    cd_i64 nLength = js_array_length(pCtx, thisVal);
    CDBuf buf;
    cd_i64 i = 0;
    JSVal separator;
    JSVal result;

    (void)pUser;

    if ((nArgc > 0) && (pArgv[0].tag != JT_UNDEF)) {
        separator = js_to_string(pCtx, pArgv[0]);
    } else {
        separator = js_str(pCtx, ",");
    }

    cdbuf_init(&buf);

    for (i = 0; i < nLength; i++) {
        JSVal item = js_get_index(pCtx, thisVal, i);

        if (i > 0) {
            cdbuf_append(&buf, js_str_data(separator), js_str_len(separator));
        }

        if ((item.tag != JT_UNDEF) && (item.tag != JT_NULL)) {
            JSVal text = js_to_string(pCtx, item);

            cdbuf_append(&buf, js_str_data(text), js_str_len(text));
            js_release(pCtx, text);
        }

        js_release(pCtx, item);
    }

    result = js_strn(pCtx, buf.pData, buf.nSize);
    cdbuf_free(&buf);
    js_release(pCtx, separator);

    return result;
}

static JSVal fn_array_slice(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    cd_i64 nLength = js_array_length(pCtx, thisVal);
    cd_i64 nStart = (nArgc > 0) ? clamp_index(js_to_number(pCtx, pArgv[0]), nLength) : 0;
    cd_i64 nEnd = ((nArgc > 1) && (pArgv[1].tag != JT_UNDEF)) ? clamp_index(js_to_number(pCtx, pArgv[1]), nLength) : nLength;
    JSVal result = js_new_array(pCtx);
    cd_i64 i = 0;
    cd_i64 nCount = 0;

    (void)pUser;

    for (i = nStart; i < nEnd; i++) {
        js_set_index(pCtx, result, nCount++, js_get_index(pCtx, thisVal, i));
    }

    return result;
}

static JSVal fn_array_splice(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    cd_i64 nLength = js_array_length(pCtx, thisVal);
    cd_i64 nStart = (nArgc > 0) ? clamp_index(js_to_number(pCtx, pArgv[0]), nLength) : 0;
    /* The reference engine narrows the delete count to an int32 before it
     * clamps, so 2^32 + 1 deletes one element and 1e300 deletes none.     */
    cd_i64 nDelete = (nArgc > 1) ? (cd_i64)js_to_int32(pCtx, pArgv[1]) : (nLength - nStart);
    JSVal removed = js_new_array(pCtx);
    JSVal tail = js_new_array(pCtx);
    cd_i64 i = 0;
    cd_i64 nCount = 0;
    cd_i64 nTail = 0;
    cd_i64 nInsert = (nArgc > 2) ? (nArgc - 2) : 0;
    cd_i64 nNewLength = 0;

    (void)pUser;

    if (nDelete < 0) {
        nDelete = 0;
    }

    /* nStart is in [0, nLength], so the subtraction cannot overflow while
     * the addition would for a delete count near INT64_MAX.               */
    if (nDelete > (nLength - nStart)) {
        nDelete = nLength - nStart;
    }

    for (i = 0; i < nDelete; i++) {
        js_set_index(pCtx, removed, nCount++, js_get_index(pCtx, thisVal, nStart + i));
    }

    for (i = nStart + nDelete; i < nLength; i++) {
        js_set_index(pCtx, tail, nTail++, js_get_index(pCtx, thisVal, i));
    }

    nNewLength = nStart;

    for (i = 0; i < nInsert; i++) {
        js_set_index(pCtx, thisVal, nNewLength++, js_dup(pArgv[2 + i]));
    }

    for (i = 0; i < nTail; i++) {
        js_set_index(pCtx, thisVal, nNewLength++, js_get_index(pCtx, tail, i));
    }

    for (i = nNewLength; i < nLength; i++) {
        char sKey[32];

        x_snprintf(sKey, sizeof(sKey), "%lld", (long long)i);

        if (thisVal.tag == JT_OBJ) {
            jsprops_del(pCtx, &thisVal.u.o->props, sKey);
        }
    }

    if (thisVal.tag == JT_OBJ) {
        thisVal.u.o->nArrayLen = nNewLength;
    }

    js_release(pCtx, tail);

    return removed;
}

static JSVal fn_array_concat(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    JSVal result = js_new_array(pCtx);
    cd_i64 nCount = 0;
    cd_i64 i = 0;
    int j = 0;

    (void)pUser;

    {
        cd_i64 nLength = js_array_length(pCtx, thisVal);

        for (i = 0; i < nLength; i++) {
            js_set_index(pCtx, result, nCount++, js_get_index(pCtx, thisVal, i));
        }
    }

    for (j = 0; j < nArgc; j++) {
        if ((pArgv[j].tag == JT_OBJ) && (pArgv[j].u.o->cls == JCLASS_ARRAY)) {
            cd_i64 nLength = js_array_length(pCtx, pArgv[j]);

            for (i = 0; i < nLength; i++) {
                js_set_index(pCtx, result, nCount++, js_get_index(pCtx, pArgv[j], i));
            }
        } else {
            js_set_index(pCtx, result, nCount++, js_dup(pArgv[j]));
        }
    }

    return result;
}

static JSVal fn_array_indexof(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    cd_i64 nLength = js_array_length(pCtx, thisVal);
    JSVal search = ARG(0);
    cd_i64 i = (nArgc > 1) ? js_to_int64(pCtx, pArgv[1]) : 0;

    (void)pUser;

    if (i < 0) {
        i += nLength;

        if (i < 0) {
            i = 0;
        }
    }

    for (; i < nLength; i++) {
        JSVal item = js_get_index(pCtx, thisVal, i);
        int bMatch = js_strict_equals(pCtx, item, search);

        js_release(pCtx, item);

        if (bMatch) {
            return js_num((double)i);
        }
    }

    return js_num(-1);
}

static JSVal fn_array_lastindexof(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    cd_i64 nLength = js_array_length(pCtx, thisVal);
    JSVal search = ARG(0);
    cd_i64 i = nLength - 1;

    (void)pUser;

    if ((nArgc > 1) && (nLength > 0)) {
        cd_i64 nFrom = js_to_int64(pCtx, pArgv[1]);

        if (nFrom < 0) {
            if (nFrom < (-nLength)) {
                return js_num(-1);
            }

            nFrom += nLength;
        } else if (nFrom > (nLength - 1)) {
            nFrom = nLength - 1;
        }

        i = nFrom;
    }

    for (; i >= 0; i--) {
        JSVal item = js_get_index(pCtx, thisVal, i);
        int bMatch = js_strict_equals(pCtx, item, search);

        js_release(pCtx, item);

        if (bMatch) {
            return js_num((double)i);
        }
    }

    return js_num(-1);
}

static JSVal fn_array_reverse(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    cd_i64 nLength = js_array_length(pCtx, thisVal);
    cd_i64 i = 0;

    (void)nArgc;
    (void)pArgv;
    (void)pUser;

    for (i = 0; i < nLength / 2; i++) {
        JSVal a = js_get_index(pCtx, thisVal, i);
        JSVal b = js_get_index(pCtx, thisVal, nLength - 1 - i);

        js_set_index(pCtx, thisVal, i, b);
        js_set_index(pCtx, thisVal, nLength - 1 - i, a);
    }

    return js_dup(thisVal);
}

static int sort_compare(JSCtx *pCtx, JSVal left, JSVal right, JSVal comparator)
{
    if (js_is_callable(comparator)) {
        JSVal args[2];
        JSVal result;
        double nResult = 0;

        args[0] = left;
        args[1] = right;
        result = js_call(pCtx, comparator, js_undefined(), 2, args);
        nResult = js_to_number(pCtx, result);
        js_release(pCtx, result);

        if (nResult < 0) {
            return -1;
        }

        if (nResult > 0) {
            return 1;
        }

        return 0;
    }

    {
        JSVal a = js_to_string(pCtx, left);
        JSVal b = js_to_string(pCtx, right);
        size_t nSizeA = js_str_len(a);
        size_t nSizeB = js_str_len(b);
        size_t nMin = (nSizeA < nSizeB) ? nSizeA : nSizeB;
        int nCmp = x_memcmp(js_str_data(a), js_str_data(b), nMin);

        if (nCmp == 0) {
            nCmp = (nSizeA < nSizeB) ? -1 : ((nSizeA > nSizeB) ? 1 : 0);
        }

        js_release(pCtx, a);
        js_release(pCtx, b);

        return (nCmp < 0) ? -1 : ((nCmp > 0) ? 1 : 0);
    }
}

static JSVal fn_array_sort(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    cd_i64 nLength = js_array_length(pCtx, thisVal);
    JSVal comparator = ARG(0);
    JSVal *pItems = NULL;
    cd_i64 i = 0;
    cd_i64 j = 0;

    (void)pUser;

    if (nLength <= 1) {
        return js_dup(thisVal);
    }

    /* A nominal length far beyond the number of stored elements would wrap
     * the multiplication below; refuse it instead of allocating.          */
    if (nLength > JS_MAX_SORT_ITEMS) {
        return js_throw(pCtx, "RangeError: array is too large to sort");
    }

    pItems = (JSVal *)cd_malloc((size_t)nLength * sizeof(JSVal));

    for (i = 0; i < nLength; i++) {
        pItems[i] = js_get_index(pCtx, thisVal, i);
    }

    /* Insertion sort: stable and adequate for the small arrays used here. */
    for (i = 1; i < nLength; i++) {
        JSVal key = pItems[i];

        j = i - 1;

        while ((j >= 0) && (sort_compare(pCtx, pItems[j], key, comparator) > 0)) {
            pItems[j + 1] = pItems[j];
            j--;
        }

        pItems[j + 1] = key;
    }

    for (i = 0; i < nLength; i++) {
        js_set_index(pCtx, thisVal, i, pItems[i]);
    }

    cd_free(pItems);

    return js_dup(thisVal);
}

typedef enum { ITER_FOREACH, ITER_MAP, ITER_FILTER, ITER_EVERY, ITER_SOME } IterKind;

static JSVal array_iterate(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, IterKind kind)
{
    cd_i64 nLength = js_array_length(pCtx, thisVal);
    JSVal callback = ARG(0);
    JSVal thisArg = ARG(1);
    JSVal result = js_undefined();
    cd_i64 i = 0;
    cd_i64 nCount = 0;

    if (!js_is_callable(callback)) {
        return js_throw(pCtx, "TypeError: callback is not a function");
    }

    if ((kind == ITER_MAP) || (kind == ITER_FILTER)) {
        result = js_new_array(pCtx);
    } else if (kind == ITER_EVERY) {
        result = js_bool(1);
    } else if (kind == ITER_SOME) {
        result = js_bool(0);
    }

    for (i = 0; i < nLength; i++) {
        JSVal args[3];
        JSVal item = js_get_index(pCtx, thisVal, i);
        JSVal callResult;

        args[0] = item;
        args[1] = js_num((double)i);
        args[2] = thisVal;

        callResult = js_call(pCtx, callback, thisArg, 3, args);

        if (pCtx->bException) {
            js_release(pCtx, item);
            js_release(pCtx, callResult);
            js_release(pCtx, result);

            return js_undefined();
        }

        if (kind == ITER_MAP) {
            js_set_index(pCtx, result, i, callResult);
            callResult = js_undefined();
        } else if (kind == ITER_FILTER) {
            if (js_to_bool(pCtx, callResult)) {
                js_set_index(pCtx, result, nCount++, js_dup(item));
            }
        } else if (kind == ITER_EVERY) {
            if (!js_to_bool(pCtx, callResult)) {
                js_release(pCtx, item);
                js_release(pCtx, callResult);
                js_release(pCtx, result);

                return js_bool(0);
            }
        } else if (kind == ITER_SOME) {
            if (js_to_bool(pCtx, callResult)) {
                js_release(pCtx, item);
                js_release(pCtx, callResult);
                js_release(pCtx, result);

                return js_bool(1);
            }
        }

        js_release(pCtx, callResult);
        js_release(pCtx, item);
    }

    return result;
}

static JSVal fn_array_foreach(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    (void)pUser;

    return array_iterate(pCtx, thisVal, nArgc, pArgv, ITER_FOREACH);
}

static JSVal fn_array_map(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    (void)pUser;

    return array_iterate(pCtx, thisVal, nArgc, pArgv, ITER_MAP);
}

static JSVal fn_array_filter(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    (void)pUser;

    return array_iterate(pCtx, thisVal, nArgc, pArgv, ITER_FILTER);
}

static JSVal fn_array_every(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    (void)pUser;

    return array_iterate(pCtx, thisVal, nArgc, pArgv, ITER_EVERY);
}

static JSVal fn_array_some(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    (void)pUser;

    return array_iterate(pCtx, thisVal, nArgc, pArgv, ITER_SOME);
}

static JSVal fn_array_reduce(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    cd_i64 nLength = js_array_length(pCtx, thisVal);
    JSVal callback = ARG(0);
    JSVal accumulator = js_undefined();
    cd_i64 i = 0;

    (void)pUser;

    if (!js_is_callable(callback)) {
        return js_throw(pCtx, "TypeError: callback is not a function");
    }

    if (nArgc > 1) {
        accumulator = js_dup(pArgv[1]);
    } else if (nLength > 0) {
        accumulator = js_get_index(pCtx, thisVal, 0);
        i = 1;
    }

    for (; i < nLength; i++) {
        JSVal args[4];
        JSVal item = js_get_index(pCtx, thisVal, i);
        JSVal result;

        args[0] = accumulator;
        args[1] = item;
        args[2] = js_num((double)i);
        args[3] = thisVal;

        result = js_call(pCtx, callback, js_undefined(), 4, args);
        js_release(pCtx, accumulator);
        js_release(pCtx, item);
        accumulator = result;

        if (pCtx->bException) {
            break;
        }
    }

    return accumulator;
}

/* ---------------------------------------------------------------- String  */

static JSVal fn_string_ctor(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    (void)thisVal;
    (void)pUser;

    if (nArgc == 0) {
        return js_str(pCtx, "");
    }

    return js_to_string(pCtx, pArgv[0]);
}

static JSVal fn_string_fromcharcode(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    CDBuf buf;
    int i = 0;
    JSVal result;

    (void)thisVal;
    (void)pUser;

    cdbuf_init(&buf);

    for (i = 0; i < nArgc; i++) {
        cd_i32 nCode = js_to_int32(pCtx, pArgv[i]);

        cdbuf_append_ch(&buf, (char)(nCode & 0xFF));
    }

    result = js_strn(pCtx, buf.pData, buf.nSize);
    cdbuf_free(&buf);

    return result;
}

static JSVal fn_string_tostring(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    (void)nArgc;
    (void)pArgv;
    (void)pUser;

    return this_string(pCtx, thisVal);
}

static JSVal fn_string_charat(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    JSVal text = this_string(pCtx, thisVal);
    cd_i64 nIndex = (nArgc > 0) ? js_to_int64(pCtx, pArgv[0]) : 0;
    JSVal result;

    (void)pUser;

    if ((nIndex < 0) || ((size_t)nIndex >= js_str_len(text))) {
        result = js_str(pCtx, "");
    } else {
        result = js_strn(pCtx, js_str_data(text) + nIndex, 1);
    }

    js_release(pCtx, text);

    return result;
}

static JSVal fn_string_charcodeat(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    JSVal text = this_string(pCtx, thisVal);
    cd_i64 nIndex = (nArgc > 0) ? js_to_int64(pCtx, pArgv[0]) : 0;
    JSVal result;

    (void)pUser;

    if ((nIndex < 0) || ((size_t)nIndex >= js_str_len(text))) {
        result = js_num(x_nan());
    } else {
        result = js_num((double)(unsigned char)js_str_data(text)[nIndex]);
    }

    js_release(pCtx, text);

    return result;
}

static cd_i64 find_substring(const char *pText, size_t nTextSize, const char *pNeedle, size_t nNeedleSize, size_t nStart)
{
    size_t i = 0;

    if (nNeedleSize == 0) {
        return (cd_i64)((nStart > nTextSize) ? nTextSize : nStart);
    }

    if (nNeedleSize > nTextSize) {
        return -1;
    }

    for (i = nStart; i + nNeedleSize <= nTextSize; i++) {
        if (x_memcmp(pText + i, pNeedle, nNeedleSize) == 0) {
            return (cd_i64)i;
        }
    }

    return -1;
}

static JSVal fn_string_indexof(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    JSVal text = this_string(pCtx, thisVal);
    JSVal needle = js_to_string(pCtx, ARG(0));
    cd_i64 nStart = (nArgc > 1) ? js_to_int64(pCtx, pArgv[1]) : 0;
    cd_i64 nResult = 0;

    (void)pUser;

    if (nStart < 0) {
        nStart = 0;
    }

    nResult = find_substring(js_str_data(text), js_str_len(text), js_str_data(needle), js_str_len(needle), (size_t)nStart);

    js_release(pCtx, text);
    js_release(pCtx, needle);

    return js_num((double)nResult);
}

static JSVal fn_string_lastindexof(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    JSVal text = this_string(pCtx, thisVal);
    JSVal needle = js_to_string(pCtx, ARG(0));
    size_t nTextSize = js_str_len(text);
    size_t nNeedleSize = js_str_len(needle);
    cd_i64 nResult = -1;

    (void)pUser;

    if (nNeedleSize <= nTextSize) {
        size_t nStart = nTextSize - nNeedleSize;
        size_t i = 0;

        /* An absent or NaN position means +Infinity, i.e. no upper bound. */
        if (nArgc > 1) {
            double nValue = js_to_number(pCtx, pArgv[1]);

            if (nValue == nValue) {
                size_t nFrom = 0;

                if (nValue > 0) {
                    nFrom = (nValue > (double)nTextSize) ? nTextSize : (size_t)nValue;
                }

                if (nFrom < nStart) {
                    nStart = nFrom;
                }
            }
        }

        for (i = nStart + 1; i > 0; i--) {
            if (x_memcmp(js_str_data(text) + i - 1, js_str_data(needle), nNeedleSize) == 0) {
                nResult = (cd_i64)(i - 1);
                break;
            }
        }
    }

    js_release(pCtx, text);
    js_release(pCtx, needle);

    return js_num((double)nResult);
}

static JSVal fn_string_slice(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    JSVal text = this_string(pCtx, thisVal);
    cd_i64 nLength = (cd_i64)js_str_len(text);
    cd_i64 nStart = (nArgc > 0) ? clamp_index(js_to_number(pCtx, pArgv[0]), nLength) : 0;
    cd_i64 nEnd = ((nArgc > 1) && (pArgv[1].tag != JT_UNDEF)) ? clamp_index(js_to_number(pCtx, pArgv[1]), nLength) : nLength;
    JSVal result;

    (void)pUser;

    if (nEnd < nStart) {
        nEnd = nStart;
    }

    result = js_strn(pCtx, js_str_data(text) + nStart, (size_t)(nEnd - nStart));
    js_release(pCtx, text);

    return result;
}

static JSVal fn_string_substring(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    JSVal text = this_string(pCtx, thisVal);
    cd_i64 nLength = (cd_i64)js_str_len(text);
    cd_i64 nStart = 0;
    cd_i64 nEnd = nLength;
    JSVal result;

    (void)pUser;

    if (nArgc > 0) {
        double nValue = js_to_number(pCtx, pArgv[0]);

        nStart = (nValue != nValue) ? 0 : (cd_i64)nValue;
    }

    if ((nArgc > 1) && (pArgv[1].tag != JT_UNDEF)) {
        double nValue = js_to_number(pCtx, pArgv[1]);

        nEnd = (nValue != nValue) ? 0 : (cd_i64)nValue;
    }

    if (nStart < 0) {
        nStart = 0;
    }

    if (nStart > nLength) {
        nStart = nLength;
    }

    if (nEnd < 0) {
        nEnd = 0;
    }

    if (nEnd > nLength) {
        nEnd = nLength;
    }

    if (nStart > nEnd) {
        cd_i64 nTemp = nStart;

        nStart = nEnd;
        nEnd = nTemp;
    }

    result = js_strn(pCtx, js_str_data(text) + nStart, (size_t)(nEnd - nStart));
    js_release(pCtx, text);

    return result;
}

static JSVal fn_string_substr(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    JSVal text = this_string(pCtx, thisVal);
    cd_i64 nLength = (cd_i64)js_str_len(text);
    cd_i64 nStart = (nArgc > 0) ? js_to_int64(pCtx, pArgv[0]) : 0;
    cd_i64 nCount = ((nArgc > 1) && (pArgv[1].tag != JT_UNDEF)) ? js_to_int64(pCtx, pArgv[1]) : nLength;
    JSVal result;

    (void)pUser;

    if (nStart < 0) {
        nStart = nLength + nStart;

        if (nStart < 0) {
            nStart = 0;
        }
    }

    if (nStart > nLength) {
        nStart = nLength;
    }

    if (nCount < 0) {
        nCount = 0;
    }

    /* nStart is in [0, nLength] here, so clamping by subtraction avoids the
     * overflow that a count near INT64_MAX would cause in the addition.   */
    if (nCount > (nLength - nStart)) {
        nCount = nLength - nStart;
    }

    result = js_strn(pCtx, js_str_data(text) + nStart, (size_t)nCount);
    js_release(pCtx, text);

    return result;
}

static JSVal string_case(JSCtx *pCtx, JSVal thisVal, int bUpper)
{
    JSVal text = this_string(pCtx, thisVal);
    size_t nSize = js_str_len(text);
    JSStr *pStr = jsstr_new(pCtx, js_str_data(text), nSize);
    size_t i = 0;

    for (i = 0; i < nSize; i++) {
        char nChar = pStr->pData[i];

        if (bUpper) {
            if ((nChar >= 'a') && (nChar <= 'z')) {
                pStr->pData[i] = (char)(nChar - 'a' + 'A');
            }
        } else {
            if ((nChar >= 'A') && (nChar <= 'Z')) {
                pStr->pData[i] = (char)(nChar - 'A' + 'a');
            }
        }
    }

    js_release(pCtx, text);

    return jsval_str(pStr);
}

static JSVal fn_string_toupper(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    (void)nArgc;
    (void)pArgv;
    (void)pUser;

    return string_case(pCtx, thisVal, 1);
}

static JSVal fn_string_tolower(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    (void)nArgc;
    (void)pArgv;
    (void)pUser;

    return string_case(pCtx, thisVal, 0);
}

static JSVal fn_string_trim(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    JSVal text = this_string(pCtx, thisVal);
    const char *pData = js_str_data(text);
    size_t nStart = 0;
    size_t nEnd = js_str_len(text);
    JSVal result;

    (void)nArgc;
    (void)pArgv;
    (void)pUser;

    while ((nStart < nEnd) && ((unsigned char)pData[nStart] <= ' ')) {
        nStart++;
    }

    while ((nEnd > nStart) && ((unsigned char)pData[nEnd - 1] <= ' ')) {
        nEnd--;
    }

    result = js_strn(pCtx, pData + nStart, nEnd - nStart);
    js_release(pCtx, text);

    return result;
}

static JSVal fn_string_concat(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    JSVal result = this_string(pCtx, thisVal);
    int i = 0;

    (void)pUser;

    for (i = 0; i < nArgc; i++) {
        JSVal next = js_concat_str(pCtx, result, pArgv[i]);

        js_release(pCtx, result);
        result = next;
    }

    return result;
}

static JSVal fn_string_split(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    JSVal text = this_string(pCtx, thisVal);
    JSVal result = js_new_array(pCtx);
    cd_i64 nCount = 0;
    cd_i64 nLimit = ((nArgc > 1) && (pArgv[1].tag != JT_UNDEF)) ? js_to_int64(pCtx, pArgv[1]) : 0x7FFFFFFF;

    (void)pUser;

    if ((nArgc == 0) || (pArgv[0].tag == JT_UNDEF)) {
        js_set_index(pCtx, result, 0, js_dup(text));
        js_release(pCtx, text);

        return result;
    }

    if (js_get_regexp(ARG(0))) {
        JSRegExp *pRegExp = js_get_regexp(pArgv[0]);
        const char *pData = js_str_data(text);
        size_t nSize = js_str_len(text);
        size_t nPos = 0;
        size_t nLast = 0;
        cd_i32 *pCaps = (cd_i32 *)cd_malloc((size_t)(jsregexp_ngroups(pRegExp) + 1) * 2 * sizeof(cd_i32));

        while ((nPos <= nSize) && (nCount < nLimit)) {
            if (!jsregexp_exec(pRegExp, pData, nSize, nPos, pCaps)) {
                break;
            }

            if ((size_t)pCaps[1] == nLast && pCaps[0] == pCaps[1]) {
                nPos++;
                continue;
            }

            js_set_index(pCtx, result, nCount++, js_strn(pCtx, pData + nLast, (size_t)pCaps[0] - nLast));

            {
                int g = 0;

                for (g = 1; (g <= jsregexp_ngroups(pRegExp)) && (nCount < nLimit); g++) {
                    if (pCaps[g * 2] >= 0) {
                        js_set_index(pCtx, result, nCount++, js_strn(pCtx, pData + pCaps[g * 2], (size_t)(pCaps[g * 2 + 1] - pCaps[g * 2])));
                    } else {
                        js_set_index(pCtx, result, nCount++, js_undefined());
                    }
                }
            }

            nLast = (size_t)pCaps[1];
            nPos = (pCaps[1] > pCaps[0]) ? (size_t)pCaps[1] : (size_t)(pCaps[1] + 1);
        }

        if (nCount < nLimit) {
            js_set_index(pCtx, result, nCount++, js_strn(pCtx, pData + nLast, nSize - nLast));
        }

        cd_free(pCaps);
        js_release(pCtx, text);

        return result;
    }

    {
        JSVal separator = js_to_string(pCtx, pArgv[0]);
        const char *pData = js_str_data(text);
        size_t nSize = js_str_len(text);
        size_t nSepSize = js_str_len(separator);
        size_t nPos = 0;

        if (nSepSize == 0) {
            size_t i = 0;

            for (i = 0; (i < nSize) && (nCount < nLimit); i++) {
                js_set_index(pCtx, result, nCount++, js_strn(pCtx, pData + i, 1));
            }
        } else {
            for (;;) {
                cd_i64 nFound = find_substring(pData, nSize, js_str_data(separator), nSepSize, nPos);

                if ((nFound < 0) || (nCount >= nLimit)) {
                    break;
                }

                js_set_index(pCtx, result, nCount++, js_strn(pCtx, pData + nPos, (size_t)nFound - nPos));
                nPos = (size_t)nFound + nSepSize;
            }

            if (nCount < nLimit) {
                js_set_index(pCtx, result, nCount++, js_strn(pCtx, pData + nPos, nSize - nPos));
            }
        }

        js_release(pCtx, separator);
    }

    js_release(pCtx, text);

    return result;
}

/* Builds the array returned by String.match / RegExp.exec. */
static JSVal build_match_array(JSCtx *pCtx, JSRegExp *pRegExp, const char *pData, cd_i32 *pCaps, JSVal input)
{
    JSVal result = js_new_array(pCtx);
    int g = 0;
    int nGroups = jsregexp_ngroups(pRegExp);

    for (g = 0; g <= nGroups; g++) {
        if (pCaps[g * 2] >= 0) {
            js_set_index(pCtx, result, g, js_strn(pCtx, pData + pCaps[g * 2], (size_t)(pCaps[g * 2 + 1] - pCaps[g * 2])));
        } else {
            js_set_index(pCtx, result, g, js_undefined());
        }
    }

    result.u.o->nArrayLen = nGroups + 1;

    jsobj_put_hidden(pCtx, result.u.o, "index", js_num((double)pCaps[0]));
    jsobj_put_hidden(pCtx, result.u.o, "input", js_dup(input));

    return result;
}

static JSRegExp *coerce_regexp(JSCtx *pCtx, JSVal value, JSVal *pOwned)
{
    JSRegExp *pRegExp = js_get_regexp(value);

    *pOwned = js_undefined();

    if (pRegExp) {
        return pRegExp;
    }

    {
        JSVal text = js_to_string(pCtx, value);
        JSVal created = js_new_regexp_val(pCtx, js_str_data(text), "");

        js_release(pCtx, text);
        *pOwned = created;

        return js_get_regexp(created);
    }
}

static JSVal fn_string_match(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    JSVal text = this_string(pCtx, thisVal);
    JSVal owned;
    JSRegExp *pRegExp = coerce_regexp(pCtx, ARG(0), &owned);
    const char *pData = js_str_data(text);
    size_t nSize = js_str_len(text);
    cd_i32 *pCaps = NULL;
    JSVal result = js_null();

    (void)pUser;

    if (pRegExp == NULL) {
        js_release(pCtx, text);
        js_release(pCtx, owned);

        return js_null();
    }

    pCaps = (cd_i32 *)cd_malloc((size_t)(jsregexp_ngroups(pRegExp) + 1) * 2 * sizeof(cd_i32));

    if (jsregexp_global(pRegExp)) {
        size_t nPos = 0;
        cd_i64 nCount = 0;

        result = js_new_array(pCtx);

        while ((nPos <= nSize) && jsregexp_exec(pRegExp, pData, nSize, nPos, pCaps)) {
            js_set_index(pCtx, result, nCount++, js_strn(pCtx, pData + pCaps[0], (size_t)(pCaps[1] - pCaps[0])));
            nPos = (pCaps[1] > pCaps[0]) ? (size_t)pCaps[1] : (size_t)(pCaps[1] + 1);
        }

        if (nCount == 0) {
            js_release(pCtx, result);
            result = js_null();
        }
    } else if (jsregexp_exec(pRegExp, pData, nSize, 0, pCaps)) {
        result = build_match_array(pCtx, pRegExp, pData, pCaps, text);
    }

    cd_free(pCaps);
    js_release(pCtx, text);
    js_release(pCtx, owned);

    return result;
}

static JSVal fn_string_search(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    JSVal text = this_string(pCtx, thisVal);
    JSVal owned;
    JSRegExp *pRegExp = coerce_regexp(pCtx, ARG(0), &owned);
    cd_i32 *pCaps = NULL;
    JSVal result = js_num(-1);

    (void)pUser;

    if (pRegExp) {
        pCaps = (cd_i32 *)cd_malloc((size_t)(jsregexp_ngroups(pRegExp) + 1) * 2 * sizeof(cd_i32));

        if (jsregexp_exec(pRegExp, js_str_data(text), js_str_len(text), 0, pCaps)) {
            result = js_num((double)pCaps[0]);
        }

        cd_free(pCaps);
    }

    js_release(pCtx, text);
    js_release(pCtx, owned);

    return result;
}

/* Expands $1..$9, $&, $` and $' inside a replacement template. */
static void expand_replacement(JSCtx *pCtx, CDBuf *pBuf, const char *pTemplate, size_t nTemplateSize, const char *pData, size_t nDataSize, cd_i32 *pCaps, int nGroups)
{
    size_t i = 0;

    for (i = 0; i < nTemplateSize; i++) {
        if ((pTemplate[i] == '$') && (i + 1 < nTemplateSize)) {
            char nNext = pTemplate[i + 1];

            if (nNext == '$') {
                cdbuf_append_ch(pBuf, '$');
                i++;
                continue;
            }

            if (nNext == '&') {
                cdbuf_append(pBuf, pData + pCaps[0], (size_t)(pCaps[1] - pCaps[0]));
                i++;
                continue;
            }

            if (nNext == '`') {
                cdbuf_append(pBuf, pData, (size_t)pCaps[0]);
                i++;
                continue;
            }

            if (nNext == '\'') {
                cdbuf_append(pBuf, pData + pCaps[1], nDataSize - (size_t)pCaps[1]);
                i++;
                continue;
            }

            if ((nNext >= '0') && (nNext <= '9')) {
                int nIndex = nNext - '0';
                size_t nSkip = 1;

                if ((i + 2 < nTemplateSize) && (pTemplate[i + 2] >= '0') && (pTemplate[i + 2] <= '9')) {
                    int nTwo = nIndex * 10 + (pTemplate[i + 2] - '0');

                    if (nTwo <= nGroups) {
                        nIndex = nTwo;
                        nSkip = 2;
                    }
                }

                if ((nIndex >= 1) && (nIndex <= nGroups)) {
                    if (pCaps[nIndex * 2] >= 0) {
                        cdbuf_append(pBuf, pData + pCaps[nIndex * 2], (size_t)(pCaps[nIndex * 2 + 1] - pCaps[nIndex * 2]));
                    }

                    i += nSkip;
                    continue;
                }
            }
        }

        cdbuf_append_ch(pBuf, pTemplate[i]);
    }

    (void)pCtx;
}

static JSVal fn_string_replace(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    JSVal text = this_string(pCtx, thisVal);
    JSVal pattern = ARG(0);
    JSVal replacement = ARG(1);
    const char *pData = js_str_data(text);
    size_t nSize = js_str_len(text);
    CDBuf buf;
    JSVal result;
    JSRegExp *pRegExp = js_get_regexp(pattern);

    (void)pUser;

    cdbuf_init(&buf);

    if (pRegExp) {
        cd_i32 *pCaps = (cd_i32 *)cd_malloc((size_t)(jsregexp_ngroups(pRegExp) + 1) * 2 * sizeof(cd_i32));
        size_t nPos = 0;
        size_t nLast = 0;
        int bGlobal = jsregexp_global(pRegExp);

        while ((nPos <= nSize) && jsregexp_exec(pRegExp, pData, nSize, nPos, pCaps)) {
            cdbuf_append(&buf, pData + nLast, (size_t)pCaps[0] - nLast);

            if (js_is_callable(replacement)) {
                int nGroups = jsregexp_ngroups(pRegExp);
                JSVal *pCallArgs = (JSVal *)cd_malloc((size_t)(nGroups + 3) * sizeof(JSVal));
                JSVal callResult;
                JSVal callText;
                int g = 0;

                for (g = 0; g <= nGroups; g++) {
                    if (pCaps[g * 2] >= 0) {
                        pCallArgs[g] = js_strn(pCtx, pData + pCaps[g * 2], (size_t)(pCaps[g * 2 + 1] - pCaps[g * 2]));
                    } else {
                        pCallArgs[g] = js_undefined();
                    }
                }

                pCallArgs[nGroups + 1] = js_num((double)pCaps[0]);
                pCallArgs[nGroups + 2] = js_dup(text);

                callResult = js_call(pCtx, replacement, js_undefined(), nGroups + 3, pCallArgs);
                callText = js_to_string(pCtx, callResult);
                cdbuf_append(&buf, js_str_data(callText), js_str_len(callText));

                for (g = 0; g < nGroups + 3; g++) {
                    js_release(pCtx, pCallArgs[g]);
                }

                cd_free(pCallArgs);
                js_release(pCtx, callResult);
                js_release(pCtx, callText);
            } else {
                JSVal template_ = js_to_string(pCtx, replacement);

                expand_replacement(pCtx, &buf, js_str_data(template_), js_str_len(template_), pData, nSize, pCaps, jsregexp_ngroups(pRegExp));
                js_release(pCtx, template_);
            }

            nLast = (size_t)pCaps[1];
            nPos = (pCaps[1] > pCaps[0]) ? (size_t)pCaps[1] : (size_t)(pCaps[1] + 1);

            if (!bGlobal) {
                break;
            }
        }

        cdbuf_append(&buf, pData + nLast, nSize - nLast);
        cd_free(pCaps);
    } else {
        JSVal needle = js_to_string(pCtx, pattern);
        cd_i64 nFound = find_substring(pData, nSize, js_str_data(needle), js_str_len(needle), 0);

        if (nFound < 0) {
            cdbuf_append(&buf, pData, nSize);
        } else {
            cdbuf_append(&buf, pData, (size_t)nFound);

            if (js_is_callable(replacement)) {
                JSVal callArgs[3];
                JSVal callResult;
                JSVal callText;

                callArgs[0] = js_dup(needle);
                callArgs[1] = js_num((double)nFound);
                callArgs[2] = js_dup(text);
                callResult = js_call(pCtx, replacement, js_undefined(), 3, callArgs);
                callText = js_to_string(pCtx, callResult);
                cdbuf_append(&buf, js_str_data(callText), js_str_len(callText));
                js_release(pCtx, callArgs[0]);
                js_release(pCtx, callArgs[2]);
                js_release(pCtx, callResult);
                js_release(pCtx, callText);
            } else {
                JSVal template_ = js_to_string(pCtx, replacement);
                cd_i32 caps[2];

                caps[0] = (cd_i32)nFound;
                caps[1] = (cd_i32)(nFound + (cd_i64)js_str_len(needle));
                expand_replacement(pCtx, &buf, js_str_data(template_), js_str_len(template_), pData, nSize, caps, 0);
                js_release(pCtx, template_);
            }

            cdbuf_append(&buf, pData + nFound + js_str_len(needle), nSize - (size_t)nFound - js_str_len(needle));
        }

        js_release(pCtx, needle);
    }

    result = js_strn(pCtx, buf.pData, buf.nSize);
    cdbuf_free(&buf);
    js_release(pCtx, text);

    return result;
}

/* ---------------------------------------------------------------- Number  */

static JSVal fn_number_ctor(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    (void)thisVal;
    (void)pUser;

    if (nArgc == 0) {
        return js_num(0);
    }

    return js_num(js_to_number(pCtx, pArgv[0]));
}

static JSVal fn_number_tostring(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    double nValue = this_number(pCtx, thisVal);
    int nRadix = (nArgc > 0) ? (int)js_to_int64(pCtx, pArgv[0]) : 10;

    (void)pUser;

    if ((nRadix < 2) || (nRadix > 36)) {
        nRadix = 10;
    }

    return js_number_to_string(pCtx, nValue, nRadix);
}

static JSVal fn_number_valueof(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    (void)nArgc;
    (void)pArgv;
    (void)pUser;

    return js_num(this_number(pCtx, thisVal));
}

static JSVal fn_number_tofixed(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    double nValue = this_number(pCtx, thisVal);
    int nDigits = (nArgc > 0) ? (int)js_to_int64(pCtx, pArgv[0]) : 0;
    char sBuf[512];

    (void)pUser;

    if ((nDigits < 0) || (nDigits > 100)) {
        nDigits = 0;
    }

    if (nValue != nValue) {
        return js_str(pCtx, "NaN");
    }

    x_dtoa_fixed(nValue, nDigits, sBuf, sizeof(sBuf));

    return js_str(pCtx, sBuf);
}

static JSVal fn_number_toprecision(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    double nValue = this_number(pCtx, thisVal);
    int nDigits = (nArgc > 0) ? (int)js_to_int64(pCtx, pArgv[0]) : 6;
    char sBuf[512];

    (void)pUser;

    if ((nArgc == 0) || (pArgv[0].tag == JT_UNDEF)) {
        return js_number_to_string(pCtx, nValue, 10);
    }

    if ((nDigits < 1) || (nDigits > 100)) {
        nDigits = 6;
    }

    x_dtoa_precision(nValue, nDigits, sBuf, sizeof(sBuf));

    return js_str(pCtx, sBuf);
}

/* --------------------------------------------------------------- Boolean  */

static JSVal fn_boolean_ctor(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    (void)thisVal;
    (void)pUser;

    return js_bool((nArgc > 0) ? js_to_bool(pCtx, pArgv[0]) : 0);
}

static JSVal fn_boolean_tostring(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    (void)nArgc;
    (void)pArgv;
    (void)pUser;

    return js_str(pCtx, js_to_bool(pCtx, thisVal) ? "true" : "false");
}

/* ------------------------------------------------------------------ Date  */

/* Pure integer civil-calendar arithmetic: the CRT-free build has no clock
 * and no timezone database, so local time is UTC and a Date built without
 * arguments (and Date.now) reports the epoch. Everything that the signature
 * database actually evaluates - a time value, a broken-down UTC field or an
 * ISO string - is exact.                                                   */

#define DATE_MS_PER_DAY 86400000.0

typedef enum {
    DATE_TIME, DATE_YEAR, DATE_MONTH, DATE_DATE, DATE_DAY, DATE_HOURS,
    DATE_MINUTES, DATE_SECONDS, DATE_MS, DATE_TZOFFSET
} DateField;

static int date_is_finite(double nValue)
{
    return ((nValue == nValue) && (nValue != x_inf()) && (nValue != (-x_inf()))) ? 1 : 0;
}

static double date_trunc(double nValue)
{
    return (nValue < 0) ? (-x_floor(-nValue)) : x_floor(nValue);
}

/* Days since 1970-01-01 for a proleptic Gregorian date, nMonth in [1, 12]. */
static double date_days_from_civil(cd_i64 nYear, cd_i64 nMonth, cd_i64 nDay)
{
    cd_i64 y = nYear - ((nMonth <= 2) ? 1 : 0);
    cd_i64 nEra = ((y >= 0) ? y : (y - 399)) / 400;
    cd_i64 nYoe = y - nEra * 400;
    cd_i64 nDoy = (153 * (nMonth + ((nMonth > 2) ? (-3) : 9)) + 2) / 5 + nDay - 1;
    cd_i64 nDoe = nYoe * 365 + nYoe / 4 - nYoe / 100 + nDoy;

    return (double)(nEra * 146097 + nDoe - 719468);
}

static void date_civil_from_days(cd_i64 nDays, cd_i64 *pnYear, cd_i64 *pnMonth, cd_i64 *pnDay)
{
    cd_i64 z = nDays + 719468;
    cd_i64 nEra = ((z >= 0) ? z : (z - 146096)) / 146097;
    cd_i64 nDoe = z - nEra * 146097;
    cd_i64 nYoe = (nDoe - nDoe / 1460 + nDoe / 36524 - nDoe / 146096) / 365;
    cd_i64 nDoy = nDoe - (365 * nYoe + nYoe / 4 - nYoe / 100);
    cd_i64 nMp = (5 * nDoy + 2) / 153;
    cd_i64 nDay = nDoy - (153 * nMp + 2) / 5 + 1;
    cd_i64 nMonth = nMp + ((nMp < 10) ? 3 : (-9));
    cd_i64 nYear = nYoe + nEra * 400 + ((nMonth <= 2) ? 1 : 0);

    *pnYear = nYear;
    *pnMonth = nMonth;
    *pnDay = nDay;
}

static double date_make_day(double nYear, double nMonth, double nDate)
{
    double nYm = 0;
    double nMn = 0;

    if ((!date_is_finite(nYear)) || (!date_is_finite(nMonth)) || (!date_is_finite(nDate))) {
        return x_nan();
    }

    nYear = date_trunc(nYear);
    nMonth = date_trunc(nMonth);
    nDate = date_trunc(nDate);

    nYm = nYear + x_floor(nMonth / 12.0);
    nMn = nMonth - x_floor(nMonth / 12.0) * 12.0;

    /* A time value is capped at +-8.64e15 ms, i.e. roughly +-274000 years;
     * anything outside that cannot survive the clip and would overflow the
     * integer arithmetic below.                                           */
    if ((nYm < (-400000.0)) || (nYm > 400000.0) || (nDate < (-1.0e9)) || (nDate > 1.0e9)) {
        return x_nan();
    }

    return date_days_from_civil((cd_i64)nYm, (cd_i64)nMn + 1, 1) + (nDate - 1.0);
}

static double date_make_time(double nHours, double nMinutes, double nSeconds, double nMs)
{
    if ((!date_is_finite(nHours)) || (!date_is_finite(nMinutes)) || (!date_is_finite(nSeconds)) || (!date_is_finite(nMs))) {
        return x_nan();
    }

    /* Hours, minutes and seconds arrive already narrowed to an int32; the
     * millisecond field is the one component the reference engine keeps as
     * a full double, and it floors it rather than truncating towards zero
     * (a ms of -1.9 moves the time value back by 2, not by 1).            */
    return date_trunc(nHours) * 3600000.0 + date_trunc(nMinutes) * 60000.0 + date_trunc(nSeconds) * 1000.0 + x_floor(nMs);
}

static double date_time_clip(double nTime)
{
    if ((!date_is_finite(nTime)) || (x_fabs(nTime) > 8.64e15)) {
        return x_nan();
    }

    return date_trunc(nTime);
}

/* The reference engine rejects a NaN component but narrows every other one
 * to an int32, so Infinity and 1e300 both behave as zero.                  */
static double date_component(JSCtx *pCtx, JSVal value, int *pbNaN)
{
    double nValue = js_to_number(pCtx, value);

    if (nValue != nValue) {
        *pbNaN = 1;

        return 0;
    }

    return (double)js_to_int32(pCtx, js_num(nValue));
}

static double date_from_parts(JSCtx *pCtx, int nArgc, JSVal *pArgv)
{
    int bNaN = 0;
    double nYear = date_component(pCtx, pArgv[0], &bNaN);
    double nMonth = (nArgc > 1) ? date_component(pCtx, pArgv[1], &bNaN) : 0;
    double nDate = (nArgc > 2) ? date_component(pCtx, pArgv[2], &bNaN) : 1;
    double nHours = (nArgc > 3) ? date_component(pCtx, pArgv[3], &bNaN) : 0;
    double nMinutes = (nArgc > 4) ? date_component(pCtx, pArgv[4], &bNaN) : 0;
    double nSeconds = (nArgc > 5) ? date_component(pCtx, pArgv[5], &bNaN) : 0;
    /* Deliberately NOT narrowed: a millisecond count of 1e16 has to push the
     * time value out of the clip range and give NaN, which an int32 wrap
     * would hide.                                                         */
    double nMs = (nArgc > 6) ? js_to_number(pCtx, pArgv[6]) : 0;

    if (bNaN) {
        return x_nan();
    }

    if ((nYear >= 0) && (nYear <= 99)) {
        nYear = 1900 + nYear;
    }

    return date_time_clip(date_make_day(nYear, nMonth, nDate) * DATE_MS_PER_DAY + date_make_time(nHours, nMinutes, nSeconds, nMs));
}

/* The time value lives in the object's primitive slot; a value that is not a
 * number means the receiver is not a Date.                                 */
static double date_this_time(JSVal thisVal)
{
    if ((thisVal.tag == JT_OBJ) && (thisVal.u.o->primitive.tag == JT_NUM)) {
        return thisVal.u.o->primitive.u.n;
    }

    return x_nan();
}

static JSVal fn_date_ctor(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    JSObj *pProto = (JSObj *)pUser;
    double nTime = 0;

    if (nArgc == 1) {
        nTime = date_time_clip(js_to_number(pCtx, pArgv[0]));
    } else if (nArgc > 1) {
        nTime = date_from_parts(pCtx, nArgc, pArgv);
    }

    if ((thisVal.tag == JT_OBJ) && (thisVal.u.o->pProto == pProto)) {
        thisVal.u.o->primitive = js_num(nTime);

        return js_undefined();
    }

    {
        JSObj *pObj = jsobj_new(pCtx, JCLASS_OBJECT, pProto);

        pObj->primitive = js_num(nTime);

        return jsval_obj(pObj);
    }
}

static JSVal fn_date_utc(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    (void)thisVal;
    (void)pUser;

    /* The reference engine needs at least the year and the month. */
    if (nArgc < 2) {
        return js_num(x_nan());
    }

    return js_num(date_from_parts(pCtx, nArgc, pArgv));
}

static JSVal fn_date_now(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    (void)pCtx;
    (void)thisVal;
    (void)nArgc;
    (void)pArgv;
    (void)pUser;

    return js_num(0);
}

static JSVal fn_date_get(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    double nTime = date_this_time(thisVal);
    DateField field = (DateField)(size_t)pUser;
    cd_i64 nDays = 0;
    cd_i64 nInDay = 0;
    cd_i64 nYear = 0;
    cd_i64 nMonth = 0;
    cd_i64 nDate = 0;

    (void)pCtx;
    (void)nArgc;
    (void)pArgv;

    if (nTime != nTime) {
        return js_num(x_nan());
    }

    if (field == DATE_TIME) {
        return js_num(nTime);
    }

    if (field == DATE_TZOFFSET) {
        return js_num(0);
    }

    nDays = (cd_i64)x_floor(nTime / DATE_MS_PER_DAY);
    nInDay = (cd_i64)(nTime - (double)nDays * DATE_MS_PER_DAY);
    date_civil_from_days(nDays, &nYear, &nMonth, &nDate);

    switch (field) {
        case DATE_TIME: return js_num(nTime);
        case DATE_TZOFFSET: return js_num(0);
        case DATE_YEAR: return js_num((double)nYear);
        case DATE_MONTH: return js_num((double)(nMonth - 1));
        case DATE_DATE: return js_num((double)nDate);
        case DATE_DAY: return js_num((double)(((nDays % 7) + 11) % 7));
        case DATE_HOURS: return js_num((double)(nInDay / 3600000));
        case DATE_MINUTES: return js_num((double)((nInDay / 60000) % 60));
        case DATE_SECONDS: return js_num((double)((nInDay / 1000) % 60));
        case DATE_MS: return js_num((double)(nInDay % 1000));
        default: break;
    }

    return js_num(x_nan());
}

static JSVal fn_date_toisostring(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    double nTime = date_this_time(thisVal);
    cd_i64 nDays = 0;
    cd_i64 nInDay = 0;
    cd_i64 nYear = 0;
    cd_i64 nMonth = 0;
    cd_i64 nDate = 0;
    /* Exactly the width the reference engine uses: a year that does not fit
     * four digits pushes the trailing 'Z' out of the buffer.              */
    char sBuf[27];

    (void)nArgc;
    (void)pArgv;
    (void)pUser;

    if (nTime != nTime) {
        return js_str(pCtx, "Invalid Date");
    }

    nDays = (cd_i64)x_floor(nTime / DATE_MS_PER_DAY);
    nInDay = (cd_i64)(nTime - (double)nDays * DATE_MS_PER_DAY);
    date_civil_from_days(nDays, &nYear, &nMonth, &nDate);

    /* The millisecond field is truncated towards zero rather than floored,
     * so a negative time prints ".-01"; the reference does the same.      */
    x_snprintf(sBuf, sizeof(sBuf), "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ", (int)nYear, (int)nMonth, (int)nDate,
               (int)(nInDay / 3600000), (int)((nInDay / 60000) % 60), (int)((nInDay / 1000) % 60), (int)x_fmod(nTime, 1000.0));

    return js_str(pCtx, sBuf);
}

static JSVal fn_date_tostring(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    static const char *pWeekDays = "SunMonTueWedThuFriSat";
    static const char *pMonths = "JanFebMarAprMayJunJulAugSepOctNovDec";
    double nTime = date_this_time(thisVal);
    cd_i64 nDays = 0;
    cd_i64 nInDay = 0;
    cd_i64 nYear = 0;
    cd_i64 nMonth = 0;
    cd_i64 nDate = 0;
    char sBuf[64];

    (void)nArgc;
    (void)pArgv;
    (void)pUser;

    if (nTime != nTime) {
        return js_str(pCtx, "Invalid Date");
    }

    nDays = (cd_i64)x_floor(nTime / DATE_MS_PER_DAY);
    nInDay = (cd_i64)(nTime - (double)nDays * DATE_MS_PER_DAY);
    date_civil_from_days(nDays, &nYear, &nMonth, &nDate);

    x_snprintf(sBuf, sizeof(sBuf), "%.3s %.3s %02d %04d %02d:%02d:%02d GMT+0000 (UTC)", pWeekDays + ((((nDays % 7) + 11) % 7) * 3),
               pMonths + (nMonth - 1) * 3, (int)nDate, (int)nYear, (int)(nInDay / 3600000), (int)((nInDay / 60000) % 60),
               (int)((nInDay / 1000) % 60));

    return js_str(pCtx, sBuf);
}

/* ------------------------------------------------------------------ Math  */

typedef enum {
    MATH_ABS, MATH_FLOOR, MATH_CEIL, MATH_ROUND, MATH_SQRT, MATH_LOG, MATH_EXP,
    MATH_SIN, MATH_COS, MATH_TAN, MATH_TRUNC, MATH_SIGN, MATH_LOG2, MATH_LOG10
} MathKind;

static JSVal fn_math_unary(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    double nValue = (nArgc > 0) ? js_to_number(pCtx, pArgv[0]) : x_nan();
    MathKind kind = (MathKind)(size_t)pUser;

    (void)thisVal;

    switch (kind) {
        case MATH_ABS: return js_num(x_fabs(nValue));
        case MATH_FLOOR: return js_num(x_floor(nValue));
        case MATH_CEIL: return js_num(x_ceil(nValue));
        case MATH_ROUND: return js_num(x_floor(nValue + 0.5));
        case MATH_SQRT: return js_num(x_sqrt(nValue));
        case MATH_LOG: return js_num(x_log(nValue));
        case MATH_EXP: return js_num(x_exp(nValue));
        case MATH_SIN: return js_num(x_sin(nValue));
        case MATH_COS: return js_num(x_cos(nValue));
        case MATH_TAN: return js_num(x_tan(nValue));
        case MATH_TRUNC: return js_num((nValue < 0) ? x_ceil(nValue) : x_floor(nValue));
        case MATH_SIGN: return js_num((nValue > 0) ? 1 : ((nValue < 0) ? -1 : nValue));
        case MATH_LOG2: return js_num(x_log(nValue) / x_log(2.0));
        case MATH_LOG10: return js_num(x_log10(nValue));
        default: break;
    }

    return js_num(x_nan());
}

static JSVal fn_math_min(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    double nResult = x_inf();
    int i = 0;

    (void)thisVal;
    (void)pUser;

    for (i = 0; i < nArgc; i++) {
        double nValue = js_to_number(pCtx, pArgv[i]);

        if (nValue != nValue) {
            return js_num(x_nan());
        }

        if (nValue < nResult) {
            nResult = nValue;
        }
    }

    return js_num(nResult);
}

static JSVal fn_math_max(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    double nResult = (-x_inf());
    int i = 0;

    (void)thisVal;
    (void)pUser;

    for (i = 0; i < nArgc; i++) {
        double nValue = js_to_number(pCtx, pArgv[i]);

        if (nValue != nValue) {
            return js_num(x_nan());
        }

        if (nValue > nResult) {
            nResult = nValue;
        }
    }

    return js_num(nResult);
}

static JSVal fn_math_pow(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    (void)thisVal;
    (void)pUser;

    return js_num(x_pow((nArgc > 0) ? js_to_number(pCtx, pArgv[0]) : x_nan(), (nArgc > 1) ? js_to_number(pCtx, pArgv[1]) : x_nan()));
}

static JSVal fn_math_random(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    (void)pCtx;
    (void)thisVal;
    (void)nArgc;
    (void)pArgv;
    (void)pUser;

    return js_num((double)x_rand() / ((double)X_RAND_MAX + 1.0));
}

/* ---------------------------------------------------------------- global  */

static JSVal fn_parseint(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    JSVal text = js_to_string(pCtx, ARG(0));
    const char *pData = js_str_data(text);
    size_t nSize = js_str_len(text);
    size_t i = 0;
    int nRadix = (nArgc > 1) ? (int)js_to_int64(pCtx, pArgv[1]) : 0;
    int bNegative = 0;
    double nResult = 0;
    int bAny = 0;

    (void)thisVal;
    (void)pUser;

    while ((i < nSize) && ((unsigned char)pData[i] <= ' ')) {
        i++;
    }

    if ((i < nSize) && ((pData[i] == '+') || (pData[i] == '-'))) {
        bNegative = (pData[i] == '-');
        i++;
    }

    if ((nRadix == 0) || (nRadix == 16)) {
        if ((i + 1 < nSize) && (pData[i] == '0') && ((pData[i + 1] == 'x') || (pData[i + 1] == 'X'))) {
            i += 2;
            nRadix = 16;
        } else if (nRadix == 0) {
            nRadix = 10;
        }
    }

    if ((nRadix < 2) || (nRadix > 36)) {
        js_release(pCtx, text);

        return js_num(x_nan());
    }

    for (; i < nSize; i++) {
        int nDigit = -1;
        char nChar = pData[i];

        if ((nChar >= '0') && (nChar <= '9')) {
            nDigit = nChar - '0';
        } else if ((nChar >= 'a') && (nChar <= 'z')) {
            nDigit = nChar - 'a' + 10;
        } else if ((nChar >= 'A') && (nChar <= 'Z')) {
            nDigit = nChar - 'A' + 10;
        }

        if ((nDigit < 0) || (nDigit >= nRadix)) {
            break;
        }

        nResult = nResult * nRadix + nDigit;
        bAny = 1;
    }

    js_release(pCtx, text);

    if (!bAny) {
        return js_num(x_nan());
    }

    return js_num(bNegative ? -nResult : nResult);
}

static JSVal fn_parsefloat(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    JSVal text = js_to_string(pCtx, ARG(0));
    const char *pData = js_str_data(text);
    const char *pEnd = NULL;
    double nResult = x_strtod(pData, &pEnd);

    (void)thisVal;
    (void)pUser;

    if (pEnd == pData) {
        nResult = x_nan();
    }

    js_release(pCtx, text);

    return js_num(nResult);
}

static JSVal fn_isnan(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    double nValue = js_to_number(pCtx, ARG(0));

    (void)thisVal;
    (void)pUser;

    return js_bool(nValue != nValue);
}

static JSVal fn_isfinite(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    double nValue = js_to_number(pCtx, ARG(0));

    (void)thisVal;
    (void)pUser;

    return js_bool((nValue == nValue) && (nValue != x_inf()) && (nValue != (-x_inf())));
}

static JSVal fn_error_ctor(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    /* Error.prototype has to be the prototype, otherwise the instance never
     * finds its own toString and stringifies as "[object Object]".        */
    JSVal object = jsval_obj(jsobj_new(pCtx, JCLASS_ERROR, pCtx->pErrorProto));

    (void)thisVal;
    (void)pUser;

    js_set(pCtx, object, "message", (nArgc > 0) ? js_to_string(pCtx, pArgv[0]) : js_str(pCtx, ""));
    js_set(pCtx, object, "name", js_str(pCtx, "Error"));

    return object;
}

static JSVal fn_error_tostring(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    JSVal name = js_get(pCtx, thisVal, "name");
    JSVal message = js_get(pCtx, thisVal, "message");
    CDBuf buf;
    JSVal result;

    (void)nArgc;
    (void)pArgv;
    (void)pUser;

    cdbuf_init(&buf);
    cdbuf_append_str(&buf, (name.tag == JT_STR) ? js_str_data(name) : "Error");

    if (js_str_len(message)) {
        cdbuf_append_str(&buf, ": ");
        cdbuf_append(&buf, js_str_data(message), js_str_len(message));
    }

    result = js_strn(pCtx, buf.pData, buf.nSize);
    cdbuf_free(&buf);
    js_release(pCtx, name);
    js_release(pCtx, message);

    return result;
}

/* ------------------------------------------------------------------ JSON  */

static void json_quote(CDBuf *pBuf, const char *pData, size_t nSize)
{
    size_t i = 0;

    cdbuf_append_ch(pBuf, '"');

    for (i = 0; i < nSize; i++) {
        unsigned char nChar = (unsigned char)pData[i];

        switch (nChar) {
            case '"': cdbuf_append_str(pBuf, "\\\""); break;
            case '\\': cdbuf_append_str(pBuf, "\\\\"); break;
            case '\n': cdbuf_append_str(pBuf, "\\n"); break;
            case '\r': cdbuf_append_str(pBuf, "\\r"); break;
            case '\t': cdbuf_append_str(pBuf, "\\t"); break;
            case '\b': cdbuf_append_str(pBuf, "\\b"); break;
            case '\f': cdbuf_append_str(pBuf, "\\f"); break;
            default:
                if (nChar < 0x20) {
                    cdbuf_appendf(pBuf, "\\u%04x", nChar);
                } else {
                    cdbuf_append_ch(pBuf, (char)nChar);
                }
                break;
        }
    }

    cdbuf_append_ch(pBuf, '"');
}

static void json_stringify_value(JSCtx *pCtx, CDBuf *pBuf, JSVal value, int nDepth);

/* Serialises a value that has already had toJSON applied to it (or that never
 * had one).  Keeping this separate is what stops an object returned BY toJSON
 * from having its own toJSON invoked again, which is both what the spec says
 * and what the reference engine does.                                       */
static void json_stringify_raw(JSCtx *pCtx, CDBuf *pBuf, JSVal value, int nDepth)
{
    if (nDepth > 64) {
        cdbuf_append_str(pBuf, "null");
        return;
    }

    switch (value.tag) {
        case JT_UNDEF:
        case JT_NULL: cdbuf_append_str(pBuf, "null"); break;
        case JT_BOOL: cdbuf_append_str(pBuf, value.u.b ? "true" : "false"); break;
        case JT_NUM: {
            JSVal text = js_number_to_string(pCtx, value.u.n, 10);

            cdbuf_append(pBuf, js_str_data(text), js_str_len(text));
            js_release(pCtx, text);
            break;
        }
        case JT_STR: json_quote(pBuf, js_str_data(value), js_str_len(value)); break;
        case JT_OBJ: {
            if (js_is_callable(value)) {
                cdbuf_append_str(pBuf, "null");
            } else if (value.u.o->cls == JCLASS_ARRAY) {
                cd_i64 nLength = js_array_length(pCtx, value);
                cd_i64 i = 0;

                cdbuf_append_ch(pBuf, '[');

                for (i = 0; i < nLength; i++) {
                    JSVal item = js_get_index(pCtx, value, i);

                    if (i > 0) {
                        cdbuf_append_ch(pBuf, ',');
                    }

                    json_stringify_value(pCtx, pBuf, item, nDepth + 1);
                    js_release(pCtx, item);
                }

                cdbuf_append_ch(pBuf, ']');
            } else {
                JSPropMap *pMap = &value.u.o->props;
                size_t i = 0;
                int bFirst = 1;

                cdbuf_append_ch(pBuf, '{');

                for (i = 0; i < pMap->nSize; i++) {
                    if (pMap->pEntries[i].bDeleted || pMap->pEntries[i].bDontEnum) {
                        continue;
                    }

                    if (!bFirst) {
                        cdbuf_append_ch(pBuf, ',');
                    }

                    bFirst = 0;
                    json_quote(pBuf, pMap->pEntries[i].pKey, x_strlen(pMap->pEntries[i].pKey));
                    cdbuf_append_ch(pBuf, ':');
                    json_stringify_value(pCtx, pBuf, pMap->pEntries[i].value, nDepth + 1);
                }

                cdbuf_append_ch(pBuf, '}');
            }

            break;
        }

        default: break;
    }
}

static void json_stringify_value(JSCtx *pCtx, CDBuf *pBuf, JSVal value, int nDepth)
{
    /* An object that supplies toJSON is serialised through it - the only
     * reason Date.prototype.toJSON exists at all.  The replacement value is
     * then serialised raw, so a toJSON that returns the receiver terminates
     * instead of recursing.                                               */
    if ((value.tag == JT_OBJ) && (!js_is_callable(value))) {
        JSVal toJson = js_get(pCtx, value, "toJSON");

        if (js_is_callable(toJson)) {
            JSVal replaced = js_call(pCtx, toJson, value, 0, NULL);

            js_release(pCtx, toJson);
            json_stringify_raw(pCtx, pBuf, replaced, nDepth);
            js_release(pCtx, replaced);

            return;
        }

        js_release(pCtx, toJson);
    }

    json_stringify_raw(pCtx, pBuf, value, nDepth);
}

static JSVal fn_json_stringify(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    CDBuf buf;
    JSVal result;

    (void)thisVal;
    (void)pUser;

    cdbuf_init(&buf);
    json_stringify_value(pCtx, &buf, ARG(0), 0);
    result = js_strn(pCtx, buf.pData, buf.nSize);
    cdbuf_free(&buf);

    return result;
}

typedef struct {
    const char *p;
    JSCtx *pCtx;
    int bError;
    int nDepth;
} JsonParser;

/* json_parse_value recurses once per nesting level and its frame is about
 * 200 bytes, so this bound costs well under half a megabyte of stack while
 * being orders of magnitude deeper than any real document.  A limit of any
 * size necessarily diverges from the reference engine, which keeps going
 * until its own stack runs out.                                           */
#define JSON_MAX_DEPTH 2048

static JSVal json_parse_value(JsonParser *pParser);

static void json_skip_ws(JsonParser *pParser)
{
    while ((*pParser->p == ' ') || (*pParser->p == '\t') || (*pParser->p == '\n') || (*pParser->p == '\r')) {
        pParser->p++;
    }
}

static JSVal json_parse_value(JsonParser *pParser)
{
    JSCtx *pCtx = pParser->pCtx;

    json_skip_ws(pParser);

    if ((*pParser->p == '{') || (*pParser->p == '[')) {
        if (pParser->nDepth >= JSON_MAX_DEPTH) {
            pParser->bError = 1;

            return js_null();
        }
    }

    if (*pParser->p == '{') {
        JSVal object = js_new_object(pCtx);

        pParser->nDepth++;
        pParser->p++;
        json_skip_ws(pParser);

        if (*pParser->p == '}') {
            pParser->p++;
            pParser->nDepth--;

            return object;
        }

        for (;;) {
            JSVal key;
            JSVal value;

            json_skip_ws(pParser);
            key = json_parse_value(pParser);
            json_skip_ws(pParser);

            if (*pParser->p != ':') {
                pParser->bError = 1;
                js_release(pCtx, key);
                pParser->nDepth--;

                return object;
            }

            pParser->p++;
            value = json_parse_value(pParser);
            js_set(pCtx, object, js_str_data(key), value);
            js_release(pCtx, key);
            json_skip_ws(pParser);

            if (*pParser->p == ',') {
                pParser->p++;
                continue;
            }

            if (*pParser->p == '}') {
                pParser->p++;
            } else {
                pParser->bError = 1;
            }

            break;
        }

        pParser->nDepth--;

        return object;
    }

    if (*pParser->p == '[') {
        JSVal array = js_new_array(pCtx);
        cd_i64 nCount = 0;

        pParser->nDepth++;
        pParser->p++;
        json_skip_ws(pParser);

        if (*pParser->p == ']') {
            pParser->p++;
            pParser->nDepth--;

            return array;
        }

        for (;;) {
            js_set_index(pCtx, array, nCount++, json_parse_value(pParser));
            json_skip_ws(pParser);

            if (*pParser->p == ',') {
                pParser->p++;
                continue;
            }

            if (*pParser->p == ']') {
                pParser->p++;
            } else {
                pParser->bError = 1;
            }

            break;
        }

        pParser->nDepth--;

        return array;
    }

    if (*pParser->p == '"') {
        CDBuf buf;
        JSVal result;

        cdbuf_init(&buf);
        pParser->p++;

        while (*pParser->p && (*pParser->p != '"')) {
            if (*pParser->p == '\\') {
                int bBadEscape = 0;

                pParser->p++;

                /* A backslash immediately before the terminator must not
                 * push the cursor past the end of the string.            */
                if (*pParser->p == 0) {
                    pParser->bError = 1;
                    break;
                }

                switch (*pParser->p) {
                    case 'n': cdbuf_append_ch(&buf, '\n'); break;
                    case 't': cdbuf_append_ch(&buf, '\t'); break;
                    case 'r': cdbuf_append_ch(&buf, '\r'); break;
                    case 'b': cdbuf_append_ch(&buf, '\b'); break;
                    case 'f': cdbuf_append_ch(&buf, '\f'); break;
                    case 'u': {
                        unsigned int nCode = 0;
                        int i = 0;

                        /* Stops at the first byte that is not a hex digit,
                         * so a truncated escape cannot read past the end. */
                        for (i = 1; i <= 4; i++) {
                            char nChar = pParser->p[i];
                            int nDigit = -1;

                            if ((nChar >= '0') && (nChar <= '9')) {
                                nDigit = nChar - '0';
                            } else if ((nChar >= 'a') && (nChar <= 'f')) {
                                nDigit = nChar - 'a' + 10;
                            } else if ((nChar >= 'A') && (nChar <= 'F')) {
                                nDigit = nChar - 'A' + 10;
                            }

                            if (nDigit < 0) {
                                break;
                            }

                            nCode = nCode * 16 + (unsigned int)nDigit;
                        }

                        if (i <= 4) {
                            bBadEscape = 1;
                            break;
                        }

                        if (nCode < 0x80) {
                            cdbuf_append_ch(&buf, (char)nCode);
                        } else if (nCode < 0x800) {
                            cdbuf_append_ch(&buf, (char)(0xC0 | (nCode >> 6)));
                            cdbuf_append_ch(&buf, (char)(0x80 | (nCode & 0x3F)));
                        } else {
                            cdbuf_append_ch(&buf, (char)(0xE0 | (nCode >> 12)));
                            cdbuf_append_ch(&buf, (char)(0x80 | ((nCode >> 6) & 0x3F)));
                            cdbuf_append_ch(&buf, (char)(0x80 | (nCode & 0x3F)));
                        }

                        pParser->p += 4;
                        break;
                    }
                    default: cdbuf_append_ch(&buf, *pParser->p); break;
                }

                if (bBadEscape) {
                    pParser->bError = 1;
                    break;
                }

                pParser->p++;
            } else {
                cdbuf_append_ch(&buf, *pParser->p);
                pParser->p++;
            }
        }

        if (*pParser->p == '"') {
            pParser->p++;
        }

        result = js_strn(pCtx, buf.pData, buf.nSize);
        cdbuf_free(&buf);

        return result;
    }

    if (x_strncmp(pParser->p, "true", 4) == 0) {
        pParser->p += 4;

        return js_bool(1);
    }

    if (x_strncmp(pParser->p, "false", 5) == 0) {
        pParser->p += 5;

        return js_bool(0);
    }

    if (x_strncmp(pParser->p, "null", 4) == 0) {
        pParser->p += 4;

        return js_null();
    }

    {
        const char *pEnd = NULL;
        double nValue = x_strtod(pParser->p, &pEnd);

        if (pEnd == pParser->p) {
            pParser->bError = 1;

            return js_undefined();
        }

        pParser->p = pEnd;

        return js_num(nValue);
    }
}

static JSVal fn_json_parse(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    JSVal text = js_to_string(pCtx, ARG(0));
    JsonParser parser;
    JSVal result;

    (void)thisVal;
    (void)pUser;

    parser.p = js_str_data(text);
    parser.pCtx = pCtx;
    parser.bError = 0;
    parser.nDepth = 0;

    result = json_parse_value(&parser);
    js_release(pCtx, text);

    if (parser.bError) {
        js_release(pCtx, result);

        return js_throw(pCtx, "SyntaxError: invalid JSON");
    }

    return result;
}

/* ------------------------------------------------------------ installer  */

static void def_proto_method(JSCtx *pCtx, JSObj *pProto, const char *pName, JSNativeFn fn, int nArgc, void *pUser)
{
    jsobj_put_hidden(pCtx, pProto, pName, js_new_native(pCtx, pName, fn, nArgc, pUser));
}

static JSVal make_constructor(JSCtx *pCtx, const char *pName, JSNativeFn fn, int nArgc, JSObj *pProto)
{
    JSVal ctor = js_new_native(pCtx, pName, fn, nArgc, NULL);

    jsobj_put_hidden(pCtx, ctor.u.o, "prototype", jsval_obj(jsobj_ref(pProto)));
    jsobj_put_hidden(pCtx, pProto, "constructor", js_dup(ctor));
    jsobj_put_hidden(pCtx, pCtx->pGlobal, pName, js_dup(ctor));

    return ctor;
}

void js_install_builtins(JSCtx *pCtx)
{
    /* Object.prototype */
    def_proto_method(pCtx, pCtx->pObjectProto, "hasOwnProperty", fn_object_hasownproperty, 1, NULL);
    def_proto_method(pCtx, pCtx->pObjectProto, "toString", fn_object_tostring, 0, NULL);
    def_proto_method(pCtx, pCtx->pObjectProto, "toLocaleString", fn_object_tostring, 0, NULL);
    def_proto_method(pCtx, pCtx->pObjectProto, "valueOf", fn_object_valueof, 0, NULL);

    /* Function.prototype */
    def_proto_method(pCtx, pCtx->pFunctionProto, "call", fn_function_call, 1, NULL);
    def_proto_method(pCtx, pCtx->pFunctionProto, "apply", fn_function_apply, 2, NULL);
    def_proto_method(pCtx, pCtx->pFunctionProto, "bind", fn_function_bind, 1, NULL);
    def_proto_method(pCtx, pCtx->pFunctionProto, "toString", fn_function_tostring, 0, NULL);

    /* Array.prototype */
    def_proto_method(pCtx, pCtx->pArrayProto, "push", fn_array_push, 1, NULL);
    def_proto_method(pCtx, pCtx->pArrayProto, "pop", fn_array_pop, 0, NULL);
    def_proto_method(pCtx, pCtx->pArrayProto, "shift", fn_array_shift, 0, NULL);
    def_proto_method(pCtx, pCtx->pArrayProto, "unshift", fn_array_unshift, 1, NULL);
    def_proto_method(pCtx, pCtx->pArrayProto, "join", fn_array_join, 1, NULL);
    def_proto_method(pCtx, pCtx->pArrayProto, "slice", fn_array_slice, 2, NULL);
    def_proto_method(pCtx, pCtx->pArrayProto, "splice", fn_array_splice, 2, NULL);
    def_proto_method(pCtx, pCtx->pArrayProto, "concat", fn_array_concat, 1, NULL);
    def_proto_method(pCtx, pCtx->pArrayProto, "indexOf", fn_array_indexof, 1, NULL);
    def_proto_method(pCtx, pCtx->pArrayProto, "lastIndexOf", fn_array_lastindexof, 1, NULL);
    def_proto_method(pCtx, pCtx->pArrayProto, "reverse", fn_array_reverse, 0, NULL);
    def_proto_method(pCtx, pCtx->pArrayProto, "sort", fn_array_sort, 1, NULL);
    def_proto_method(pCtx, pCtx->pArrayProto, "forEach", fn_array_foreach, 1, NULL);
    def_proto_method(pCtx, pCtx->pArrayProto, "map", fn_array_map, 1, NULL);
    def_proto_method(pCtx, pCtx->pArrayProto, "filter", fn_array_filter, 1, NULL);
    def_proto_method(pCtx, pCtx->pArrayProto, "every", fn_array_every, 1, NULL);
    def_proto_method(pCtx, pCtx->pArrayProto, "some", fn_array_some, 1, NULL);
    def_proto_method(pCtx, pCtx->pArrayProto, "reduce", fn_array_reduce, 1, NULL);
    def_proto_method(pCtx, pCtx->pArrayProto, "toString", fn_object_tostring, 0, NULL);

    /* String.prototype */
    def_proto_method(pCtx, pCtx->pStringProto, "toString", fn_string_tostring, 0, NULL);
    def_proto_method(pCtx, pCtx->pStringProto, "valueOf", fn_string_tostring, 0, NULL);
    def_proto_method(pCtx, pCtx->pStringProto, "charAt", fn_string_charat, 1, NULL);
    def_proto_method(pCtx, pCtx->pStringProto, "charCodeAt", fn_string_charcodeat, 1, NULL);
    def_proto_method(pCtx, pCtx->pStringProto, "indexOf", fn_string_indexof, 1, NULL);
    def_proto_method(pCtx, pCtx->pStringProto, "lastIndexOf", fn_string_lastindexof, 1, NULL);
    def_proto_method(pCtx, pCtx->pStringProto, "slice", fn_string_slice, 2, NULL);
    def_proto_method(pCtx, pCtx->pStringProto, "substring", fn_string_substring, 2, NULL);
    def_proto_method(pCtx, pCtx->pStringProto, "substr", fn_string_substr, 2, NULL);
    def_proto_method(pCtx, pCtx->pStringProto, "toUpperCase", fn_string_toupper, 0, NULL);
    def_proto_method(pCtx, pCtx->pStringProto, "toLowerCase", fn_string_tolower, 0, NULL);
    def_proto_method(pCtx, pCtx->pStringProto, "toLocaleUpperCase", fn_string_toupper, 0, NULL);
    def_proto_method(pCtx, pCtx->pStringProto, "toLocaleLowerCase", fn_string_tolower, 0, NULL);
    def_proto_method(pCtx, pCtx->pStringProto, "trim", fn_string_trim, 0, NULL);
    def_proto_method(pCtx, pCtx->pStringProto, "concat", fn_string_concat, 1, NULL);
    def_proto_method(pCtx, pCtx->pStringProto, "split", fn_string_split, 2, NULL);
    def_proto_method(pCtx, pCtx->pStringProto, "match", fn_string_match, 1, NULL);
    def_proto_method(pCtx, pCtx->pStringProto, "search", fn_string_search, 1, NULL);
    def_proto_method(pCtx, pCtx->pStringProto, "replace", fn_string_replace, 2, NULL);

    /* Number.prototype */
    def_proto_method(pCtx, pCtx->pNumberProto, "toString", fn_number_tostring, 1, NULL);
    def_proto_method(pCtx, pCtx->pNumberProto, "valueOf", fn_number_valueof, 0, NULL);
    def_proto_method(pCtx, pCtx->pNumberProto, "toFixed", fn_number_tofixed, 1, NULL);
    def_proto_method(pCtx, pCtx->pNumberProto, "toPrecision", fn_number_toprecision, 1, NULL);
    def_proto_method(pCtx, pCtx->pNumberProto, "toLocaleString", fn_number_tostring, 0, NULL);

    /* Boolean.prototype */
    def_proto_method(pCtx, pCtx->pBooleanProto, "toString", fn_boolean_tostring, 0, NULL);
    def_proto_method(pCtx, pCtx->pBooleanProto, "valueOf", fn_object_valueof, 0, NULL);

    /* Error.prototype */
    def_proto_method(pCtx, pCtx->pErrorProto, "toString", fn_error_tostring, 0, NULL);

    /* Constructors */
    {
        JSVal objectCtor = make_constructor(pCtx, "Object", fn_object_ctor, 1, pCtx->pObjectProto);
        JSVal arrayCtor = make_constructor(pCtx, "Array", fn_array_ctor, 1, pCtx->pArrayProto);
        JSVal stringCtor = make_constructor(pCtx, "String", fn_string_ctor, 1, pCtx->pStringProto);
        JSVal numberCtor = make_constructor(pCtx, "Number", fn_number_ctor, 1, pCtx->pNumberProto);
        JSVal booleanCtor = make_constructor(pCtx, "Boolean", fn_boolean_ctor, 1, pCtx->pBooleanProto);
        JSVal errorCtor = make_constructor(pCtx, "Error", fn_error_ctor, 1, pCtx->pErrorProto);
        JSVal functionCtor = make_constructor(pCtx, "Function", fn_object_ctor, 1, pCtx->pFunctionProto);

        js_def_method(pCtx, objectCtor, "keys", fn_object_keys, 1, NULL);
        js_def_method(pCtx, objectCtor, "defineProperty", fn_object_defineproperty, 3, NULL);
        js_def_method(pCtx, arrayCtor, "isArray", fn_array_isarray, 1, NULL);
        js_def_method(pCtx, stringCtor, "fromCharCode", fn_string_fromcharcode, 1, NULL);

        jsobj_put_hidden(pCtx, numberCtor.u.o, "MAX_SAFE_INTEGER", js_num(9007199254740991.0));
        jsobj_put_hidden(pCtx, numberCtor.u.o, "MIN_SAFE_INTEGER", js_num(-9007199254740991.0));
        jsobj_put_hidden(pCtx, numberCtor.u.o, "MAX_VALUE", js_num(1.7976931348623157e308));
        jsobj_put_hidden(pCtx, numberCtor.u.o, "MIN_VALUE", js_num(5e-324));
        jsobj_put_hidden(pCtx, numberCtor.u.o, "NaN", js_num(x_nan()));
        jsobj_put_hidden(pCtx, numberCtor.u.o, "POSITIVE_INFINITY", js_num(x_inf()));
        jsobj_put_hidden(pCtx, numberCtor.u.o, "NEGATIVE_INFINITY", js_num((-x_inf())));

        js_release(pCtx, objectCtor);
        js_release(pCtx, arrayCtor);
        js_release(pCtx, stringCtor);
        js_release(pCtx, numberCtor);
        js_release(pCtx, booleanCtor);
        js_release(pCtx, errorCtor);
        js_release(pCtx, functionCtor);
    }

    /* Date - the prototype is not shared, so the constructor carries it as
     * its user pointer instead of living on the context.                  */
    {
        JSObj *pDateProto = jsobj_new(pCtx, JCLASS_OBJECT, pCtx->pObjectProto);
        JSVal dateCtor = js_new_native(pCtx, "Date", fn_date_ctor, 7, pDateProto);

        def_proto_method(pCtx, pDateProto, "getTime", fn_date_get, 0, (void *)(size_t)DATE_TIME);
        def_proto_method(pCtx, pDateProto, "valueOf", fn_date_get, 0, (void *)(size_t)DATE_TIME);
        def_proto_method(pCtx, pDateProto, "getFullYear", fn_date_get, 0, (void *)(size_t)DATE_YEAR);
        def_proto_method(pCtx, pDateProto, "getUTCFullYear", fn_date_get, 0, (void *)(size_t)DATE_YEAR);
        def_proto_method(pCtx, pDateProto, "getMonth", fn_date_get, 0, (void *)(size_t)DATE_MONTH);
        def_proto_method(pCtx, pDateProto, "getUTCMonth", fn_date_get, 0, (void *)(size_t)DATE_MONTH);
        def_proto_method(pCtx, pDateProto, "getDate", fn_date_get, 0, (void *)(size_t)DATE_DATE);
        def_proto_method(pCtx, pDateProto, "getUTCDate", fn_date_get, 0, (void *)(size_t)DATE_DATE);
        def_proto_method(pCtx, pDateProto, "getDay", fn_date_get, 0, (void *)(size_t)DATE_DAY);
        def_proto_method(pCtx, pDateProto, "getUTCDay", fn_date_get, 0, (void *)(size_t)DATE_DAY);
        def_proto_method(pCtx, pDateProto, "getHours", fn_date_get, 0, (void *)(size_t)DATE_HOURS);
        def_proto_method(pCtx, pDateProto, "getUTCHours", fn_date_get, 0, (void *)(size_t)DATE_HOURS);
        def_proto_method(pCtx, pDateProto, "getMinutes", fn_date_get, 0, (void *)(size_t)DATE_MINUTES);
        def_proto_method(pCtx, pDateProto, "getUTCMinutes", fn_date_get, 0, (void *)(size_t)DATE_MINUTES);
        def_proto_method(pCtx, pDateProto, "getSeconds", fn_date_get, 0, (void *)(size_t)DATE_SECONDS);
        def_proto_method(pCtx, pDateProto, "getUTCSeconds", fn_date_get, 0, (void *)(size_t)DATE_SECONDS);
        def_proto_method(pCtx, pDateProto, "getMilliseconds", fn_date_get, 0, (void *)(size_t)DATE_MS);
        def_proto_method(pCtx, pDateProto, "getUTCMilliseconds", fn_date_get, 0, (void *)(size_t)DATE_MS);
        def_proto_method(pCtx, pDateProto, "getTimezoneOffset", fn_date_get, 0, (void *)(size_t)DATE_TZOFFSET);
        def_proto_method(pCtx, pDateProto, "toISOString", fn_date_toisostring, 0, NULL);
        def_proto_method(pCtx, pDateProto, "toJSON", fn_date_toisostring, 1, NULL);
        def_proto_method(pCtx, pDateProto, "toString", fn_date_tostring, 0, NULL);

        jsobj_put_hidden(pCtx, dateCtor.u.o, "prototype", jsval_obj(jsobj_ref(pDateProto)));
        jsobj_put_hidden(pCtx, pDateProto, "constructor", js_dup(dateCtor));
        jsobj_put_hidden(pCtx, pCtx->pGlobal, "Date", js_dup(dateCtor));

        js_def_method(pCtx, dateCtor, "UTC", fn_date_utc, 7, NULL);
        js_def_method(pCtx, dateCtor, "now", fn_date_now, 0, NULL);

        js_release(pCtx, dateCtor);
    }

    /* Math */
    {
        JSVal math = js_new_object(pCtx);

        js_def_method(pCtx, math, "abs", fn_math_unary, 1, (void *)(size_t)MATH_ABS);
        js_def_method(pCtx, math, "floor", fn_math_unary, 1, (void *)(size_t)MATH_FLOOR);
        js_def_method(pCtx, math, "ceil", fn_math_unary, 1, (void *)(size_t)MATH_CEIL);
        js_def_method(pCtx, math, "round", fn_math_unary, 1, (void *)(size_t)MATH_ROUND);
        js_def_method(pCtx, math, "sqrt", fn_math_unary, 1, (void *)(size_t)MATH_SQRT);
        js_def_method(pCtx, math, "log", fn_math_unary, 1, (void *)(size_t)MATH_LOG);
        js_def_method(pCtx, math, "log2", fn_math_unary, 1, (void *)(size_t)MATH_LOG2);
        js_def_method(pCtx, math, "log10", fn_math_unary, 1, (void *)(size_t)MATH_LOG10);
        js_def_method(pCtx, math, "exp", fn_math_unary, 1, (void *)(size_t)MATH_EXP);
        js_def_method(pCtx, math, "sin", fn_math_unary, 1, (void *)(size_t)MATH_SIN);
        js_def_method(pCtx, math, "cos", fn_math_unary, 1, (void *)(size_t)MATH_COS);
        js_def_method(pCtx, math, "tan", fn_math_unary, 1, (void *)(size_t)MATH_TAN);
        js_def_method(pCtx, math, "trunc", fn_math_unary, 1, (void *)(size_t)MATH_TRUNC);
        js_def_method(pCtx, math, "sign", fn_math_unary, 1, (void *)(size_t)MATH_SIGN);
        js_def_method(pCtx, math, "min", fn_math_min, 2, NULL);
        js_def_method(pCtx, math, "max", fn_math_max, 2, NULL);
        js_def_method(pCtx, math, "pow", fn_math_pow, 2, NULL);
        js_def_method(pCtx, math, "random", fn_math_random, 0, NULL);

        jsobj_put_hidden(pCtx, math.u.o, "PI", js_num(3.141592653589793));
        jsobj_put_hidden(pCtx, math.u.o, "E", js_num(2.718281828459045));
        jsobj_put_hidden(pCtx, math.u.o, "LN2", js_num(0.6931471805599453));
        jsobj_put_hidden(pCtx, math.u.o, "LN10", js_num(2.302585092994046));

        jsobj_put_hidden(pCtx, pCtx->pGlobal, "Math", math);
    }

    /* JSON */
    {
        JSVal json = js_new_object(pCtx);

        js_def_method(pCtx, json, "stringify", fn_json_stringify, 3, NULL);
        js_def_method(pCtx, json, "parse", fn_json_parse, 2, NULL);
        jsobj_put_hidden(pCtx, pCtx->pGlobal, "JSON", json);
    }

    /* Global functions and values */
    js_def_fn(pCtx, "parseInt", fn_parseint, 2, NULL);
    js_def_fn(pCtx, "parseFloat", fn_parsefloat, 1, NULL);
    js_def_fn(pCtx, "isNaN", fn_isnan, 1, NULL);
    js_def_fn(pCtx, "isFinite", fn_isfinite, 1, NULL);

    jsobj_put_hidden(pCtx, pCtx->pGlobal, "NaN", js_num(x_nan()));
    jsobj_put_hidden(pCtx, pCtx->pGlobal, "Infinity", js_num(x_inf()));
    jsobj_put_hidden(pCtx, pCtx->pGlobal, "undefined", js_undefined());

    js_install_regexp(pCtx);
}
