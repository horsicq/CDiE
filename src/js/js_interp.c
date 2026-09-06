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

/* js_interp.c - tree walking evaluator. */

#include "js_ast.h"


typedef enum { CT_NORMAL = 0, CT_BREAK, CT_CONTINUE, CT_RETURN } JSCompletionType;

typedef struct {
    JSCompletionType type;
    JSVal value;
    const char *pLabel;
} JSCompletion;

typedef struct {
    JSScope *pScope;
    JSVal thisVal;
    JSObj *pFunction;
} JSFrame;

static JSVal eval_expr(JSCtx *pCtx, JSNode *pNode, JSFrame *pFrame);
static JSCompletion exec_stmt(JSCtx *pCtx, JSNode *pNode, JSFrame *pFrame);
static void hoist_declarations(JSCtx *pCtx, JSNode *pNode, JSFrame *pFrame, int bTopLevel);

static JSCompletion completion_normal(void)
{
    JSCompletion completion;

    completion.type = CT_NORMAL;
    completion.value = js_undefined();
    completion.pLabel = NULL;

    return completion;
}

/* ------------------------------------------------------------ variables  */

/* Walks the scope chain, then the global object, with a hash the caller has
 * already computed, and hands back the entry itself so that the caller does
 * not have to look it up a second time. Variable access is by far the
 * hottest operation in the engine.                                        */
static JSProp *scope_find_prop(JSCtx *pCtx, JSScope *pScope, const char *pName, size_t nNameSize, cd_u32 nHash, JSObj **ppHolder)
{
    JSScope *pCurrent = pScope;
    JSProp *pProp = NULL;

    while (pCurrent) {
        pProp = jsprops_find_hashed(&pCurrent->pVars->props, pName, nNameSize, nHash);

        if (pProp) {
            *ppHolder = pCurrent->pVars;

            return pProp;
        }

        pCurrent = pCurrent->pParent;
    }

    pProp = jsprops_find_hashed(&pCtx->pGlobal->props, pName, nNameSize, nHash);

    if (pProp) {
        *ppHolder = pCtx->pGlobal;

        return pProp;
    }

    *ppHolder = NULL;

    return NULL;
}

/* Fills in a node's cached key length and hash on first use. */
static void node_key(JSNode *pNode, size_t *pnSize, cd_u32 *pnHash)
{
    if (!pNode->bStrKey) {
        pNode->nStrSize = x_strlen(pNode->pStr);
        pNode->nStrHash = cd_hash_str(pNode->pStr, pNode->nStrSize);
        pNode->bStrKey = 1;
    }

    *pnSize = pNode->nStrSize;
    *pnHash = pNode->nStrHash;
}

static JSVal scope_get(JSCtx *pCtx, JSScope *pScope, const char *pName, size_t nNameSize, cd_u32 nHash, int *pbFound)
{
    JSObj *pHolder = NULL;
    JSProp *pProp = scope_find_prop(pCtx, pScope, pName, nNameSize, nHash, &pHolder);

    if (pProp == NULL) {
        if (pbFound) {
            *pbFound = 0;
        }

        return js_undefined();
    }

    if (pbFound) {
        *pbFound = 1;
    }

    /* jsobj_get_own only deviates from the stored value for arrays and for
     * string wrappers, which a scope record and the global object are not. */
    if ((pHolder->cls == JCLASS_ARRAY) || (pHolder->cls == JCLASS_STRING)) {
        return jsobj_get_own(pCtx, pHolder, pName, nNameSize, NULL);
    }

    return js_dup(pProp->value);
}

static void scope_set(JSCtx *pCtx, JSScope *pScope, const char *pName, size_t nNameSize, cd_u32 nHash, JSVal value)
{
    JSObj *pHolder = NULL;
    JSProp *pProp = scope_find_prop(pCtx, pScope, pName, nNameSize, nHash, &pHolder);

    if (pHolder == NULL) {
        /* Implicit global, as in sloppy-mode ECMAScript. */
        pHolder = pCtx->pGlobal;
    } else if (pHolder->cls != JCLASS_ARRAY) {
        /* Overwriting an existing entry of a plain object is all jsobj_put
         * would do here, and the entry is already in hand.                */
        jsprop_store(pCtx, pProp, value);

        return;
    }

    jsobj_put(pCtx, pHolder, pName, nNameSize, value);
}

static JSVal scope_get_node(JSCtx *pCtx, JSScope *pScope, JSNode *pNode, int *pbFound)
{
    size_t nNameSize = 0;
    cd_u32 nHash = 0;

    node_key(pNode, &nNameSize, &nHash);

    return scope_get(pCtx, pScope, pNode->pStr, nNameSize, nHash, pbFound);
}

static void scope_set_node(JSCtx *pCtx, JSScope *pScope, JSNode *pNode, JSVal value)
{
    size_t nNameSize = 0;
    cd_u32 nHash = 0;

    node_key(pNode, &nNameSize, &nHash);

    scope_set(pCtx, pScope, pNode->pStr, nNameSize, nHash, value);
}

static void scope_declare(JSCtx *pCtx, JSScope *pScope, const char *pName, JSVal value)
{
    JSObj *pVars = pScope ? pScope->pVars : pCtx->pGlobal;

    jsobj_put(pCtx, pVars, pName, x_strlen(pName), value);
}

/* ----------------------------------------------------- property helpers  */

static JSObj *proto_for_primitive(JSCtx *pCtx, JSVal value)
{
    switch (value.tag) {
        case JT_STR: return pCtx->pStringProto;
        case JT_NUM: return pCtx->pNumberProto;
        case JT_BOOL: return pCtx->pBooleanProto;
        default: return NULL;
    }
}

static JSVal get_property(JSCtx *pCtx, JSVal base, const char *pKey, size_t nKeySize)
{
    if ((base.tag == JT_UNDEF) || (base.tag == JT_NULL)) {
        char sKey[128];

        x_snprintf(sKey, sizeof(sKey), "%.*s", (int)nKeySize, pKey);
        js_throw(pCtx, "TypeError: cannot read property '%s' of %s", sKey, (base.tag == JT_NULL) ? "null" : "undefined");

        return js_undefined();
    }

    if (base.tag == JT_STR) {
        if ((nKeySize == 6) && (x_memcmp(pKey, "length", 6) == 0)) {
            return js_num((double)base.u.s->nSize);
        }

        {
            cd_i64 nIndex = 0;

            if (js_is_array_index(pKey, nKeySize, &nIndex)) {
                if ((nIndex >= 0) && ((size_t)nIndex < base.u.s->nSize)) {
                    return js_strn(pCtx, base.u.s->pData + nIndex, 1);
                }

                return js_undefined();
            }
        }
    }

    if (base.tag == JT_OBJ) {
        return jsobj_get(pCtx, base.u.o, pKey, nKeySize);
    }

    return jsobj_get(pCtx, proto_for_primitive(pCtx, base), pKey, nKeySize);
}

static void set_property(JSCtx *pCtx, JSVal base, const char *pKey, size_t nKeySize, JSVal value)
{
    if (base.tag != JT_OBJ) {
        js_release(pCtx, value);
        return;
    }

    jsobj_put(pCtx, base.u.o, pKey, nKeySize, value);
}

/* Converts an arbitrary value into a property key string. Numeric keys -
 * array indices, overwhelmingly - are rendered straight into the caller's
 * scratch buffer, so the common case allocates nothing at all. Release the
 * result with key_release().                                              */
#define KEY_SCRATCH_SIZE 16

static char *key_from_value(JSCtx *pCtx, JSVal value, size_t *pnSize, char *pScratch)
{
    JSVal str;
    char *pResult = NULL;

    if (value.tag == JT_NUM) {
        int nSize = js_int_to_buf(pScratch, value.u.n);

        if (nSize > 0) {
            if (pnSize) {
                *pnSize = (size_t)nSize;
            }

            return pScratch;
        }
    }

    str = js_to_string(pCtx, value);
    pResult = cd_strndup(js_str_data(str), js_str_len(str));

    if (pnSize) {
        *pnSize = js_str_len(str);
    }

    js_release(pCtx, str);

    return pResult;
}

static void key_release(char *pKey, const char *pScratch)
{
    if (pKey != pScratch) {
        cd_free(pKey);
    }
}

/* --------------------------------------------------------------- calling  */

static JSVal make_function(JSCtx *pCtx, JSNode *pNode, JSFrame *pFrame)
{
    JSObj *pFn = jsobj_new(pCtx, JCLASS_FUNCTION, pCtx->pFunctionProto);
    JSVal prototype = js_new_object(pCtx);

    pFn->pFnNode = pNode;
    pFn->pScope = pFrame->pScope ? jsscope_ref(pFrame->pScope) : NULL;
    pFn->pFnName = cd_strdup(pNode->pStr ? pNode->pStr : "");

    jsobj_put_hidden(pCtx, prototype.u.o, "constructor", jsval_obj(jsobj_ref(pFn)));
    jsobj_put_hidden(pCtx, pFn, "prototype", prototype);
    jsobj_put_hidden(pCtx, pFn, "length", js_num((double)(pNode->a ? pNode->a->nList : 0)));
    jsobj_put_hidden(pCtx, pFn, "name", js_str(pCtx, pFn->pFnName));

    return jsval_obj(pFn);
}

JSVal js_call_function(JSCtx *pCtx, JSObj *pFn, JSVal thisVal, int nArgc, JSVal *pArgv, int bConstruct)
{
    JSVal result = js_undefined();

    (void)bConstruct;

    if (pFn == NULL) {
        return js_throw(pCtx, "TypeError: value is not a function");
    }

    if (pCtx->nCallDepth >= pCtx->nMaxCallDepth) {
        return js_throw(pCtx, "RangeError: maximum call stack size exceeded");
    }

    pCtx->nCallDepth++;

    if (pFn->cls == JCLASS_NATIVE) {
        if (pFn->pBoundTarget) {
            /* bound function: the arguments captured by bind come first */
            JSVal target = jsval_obj(jsobj_ref(pFn->pBoundTarget));

            if (pFn->nBoundArgs > 0) {
                int nTotal = pFn->nBoundArgs + nArgc;
                JSVal *pCallArgs = (JSVal *)cd_malloc((size_t)nTotal * sizeof(JSVal));
                int i = 0;

                for (i = 0; i < pFn->nBoundArgs; i++) {
                    pCallArgs[i] = pFn->pBoundArgs[i];
                }

                for (i = 0; i < nArgc; i++) {
                    pCallArgs[pFn->nBoundArgs + i] = pArgv[i];
                }

                result = js_call_function(pCtx, pFn->pBoundTarget, pFn->boundThis, nTotal, pCallArgs, bConstruct);
                cd_free(pCallArgs);
            } else {
                result = js_call_function(pCtx, pFn->pBoundTarget, pFn->boundThis, nArgc, pArgv, bConstruct);
            }

            js_release(pCtx, target);
        } else if (pFn->nativeFn) {
            result = pFn->nativeFn(pCtx, thisVal, nArgc, pArgv, pFn->pUser);
        }

        pCtx->nCallDepth--;

        return result;
    }

    if (pFn->cls != JCLASS_FUNCTION) {
        pCtx->nCallDepth--;

        return js_throw(pCtx, "TypeError: value is not a function");
    }

    {
        JSScope *pScope = jsscope_new(pCtx, pFn->pScope);
        JSFrame frame;
        JSNode *pParams = pFn->pFnNode->a;
        size_t i = 0;
        JSVal arguments = js_new_array(pCtx);
        JSVal globalThis = js_undefined();
        JSCompletion completion;

        frame.pScope = pScope;
        frame.thisVal = thisVal;
        frame.pFunction = pFn;

        /* Sloppy-mode ECMAScript: an undefined or null `this` becomes the
           global object. */
        if ((thisVal.tag == JT_UNDEF) || (thisVal.tag == JT_NULL)) {
            globalThis = jsval_obj(jsobj_ref(pCtx->pGlobal));
            frame.thisVal = globalThis;
        }

        for (i = 0; i < (size_t)nArgc; i++) {
            js_set_index(pCtx, arguments, (cd_i64)i, js_dup(pArgv[i]));
        }

        scope_declare(pCtx, pScope, "arguments", arguments);

        for (i = 0; i < pParams->nList; i++) {
            JSVal value = ((int)i < nArgc) ? js_dup(pArgv[i]) : js_undefined();

            scope_declare(pCtx, pScope, pParams->ppList[i]->pStr, value);
        }

        /* Named function expressions can refer to themselves. */
        if (pFn->pFnName && pFn->pFnName[0] && (pFn->pFnNode->nNum != 0)) {
            scope_declare(pCtx, pScope, pFn->pFnName, jsval_obj(jsobj_ref(pFn)));
        }

        hoist_declarations(pCtx, pFn->pFnNode->b, &frame, 1);

        completion = exec_stmt(pCtx, pFn->pFnNode->b, &frame);

        if (completion.type == CT_RETURN) {
            result = completion.value;
        } else {
            js_release(pCtx, completion.value);
            result = js_undefined();
        }

        js_release(pCtx, globalThis);
        jsscope_unref(pCtx, pScope);
    }

    pCtx->nCallDepth--;

    return result;
}

JSVal js_call(JSCtx *pCtx, JSVal fn, JSVal thisVal, int nArgc, JSVal *pArgv)
{
    if (!js_is_callable(fn)) {
        return js_throw(pCtx, "TypeError: value is not a function");
    }

    return js_call_function(pCtx, fn.u.o, thisVal, nArgc, pArgv, 0);
}

JSVal js_construct(JSCtx *pCtx, JSVal fn, int nArgc, JSVal *pArgv)
{
    JSObj *pObj = NULL;
    JSVal prototype;
    JSVal thisVal;
    JSVal result;

    if (!js_is_callable(fn)) {
        return js_throw(pCtx, "TypeError: value is not a constructor");
    }

    prototype = js_get(pCtx, fn, "prototype");
    pObj = jsobj_new(pCtx, JCLASS_OBJECT, (prototype.tag == JT_OBJ) ? prototype.u.o : pCtx->pObjectProto);
    js_release(pCtx, prototype);

    thisVal = jsval_obj(pObj);
    result = js_call_function(pCtx, fn.u.o, thisVal, nArgc, pArgv, 1);

    if (pCtx->bException) {
        js_release(pCtx, result);
        js_release(pCtx, thisVal);

        return js_undefined();
    }

    if (result.tag == JT_OBJ) {
        js_release(pCtx, thisVal);

        return result;
    }

    js_release(pCtx, result);

    return thisVal;
}

/* --------------------------------------------------------------- hoisting */

/* hoist_declarations, eval_expr and exec_stmt_labeled walk the AST
 * recursively, so like js_call_function they need a ceiling: a deep but
 * perfectly valid tree would otherwise run the native stack out. They share
 * one budget, which bounds mixed statement and expression nesting too. The
 * test is written out at each entry point rather than factored into a helper
 * because these are the hottest functions in the engine.                   */
static void hoist_declarations_node(JSCtx *pCtx, JSNode *pNode, JSFrame *pFrame, int bTopLevel);

static void hoist_declarations(JSCtx *pCtx, JSNode *pNode, JSFrame *pFrame, int bTopLevel)
{
    if (pCtx->nEvalDepth >= pCtx->nMaxEvalDepth) {
        js_throw(pCtx, "RangeError: maximum expression nesting exceeded");

        return;
    }

    pCtx->nEvalDepth++;
    hoist_declarations_node(pCtx, pNode, pFrame, bTopLevel);
    pCtx->nEvalDepth--;
}

static void hoist_declarations_node(JSCtx *pCtx, JSNode *pNode, JSFrame *pFrame, int bTopLevel)
{
    size_t i = 0;

    if (pNode == NULL) {
        return;
    }

    switch (pNode->type) {
        case N_FUNCTION:
            if ((pNode->nNum == 0) && pNode->pStr) {
                scope_declare(pCtx, pFrame->pScope, pNode->pStr, make_function(pCtx, pNode, pFrame));
            }

            return; /* never descend into a nested function body */

        case N_VAR:
            for (i = 0; i < pNode->nList; i++) {
                JSNode *pDecl = pNode->ppList[i];
                JSObj *pVars = pFrame->pScope ? pFrame->pScope->pVars : pCtx->pGlobal;

                if (jsprops_find(&pVars->props, pDecl->pStr, x_strlen(pDecl->pStr)) == NULL) {
                    scope_declare(pCtx, pFrame->pScope, pDecl->pStr, js_undefined());
                }
            }

            return;

        case N_PROGRAM:
        case N_BLOCK:
        case N_CASE:
            for (i = 0; i < pNode->nList; i++) {
                hoist_declarations(pCtx, pNode->ppList[i], pFrame, 0);
            }

            return;

        case N_SWITCH:
            for (i = 0; i < pNode->nList; i++) {
                hoist_declarations(pCtx, pNode->ppList[i], pFrame, 0);
            }

            return;

        case N_IF:
            hoist_declarations(pCtx, pNode->b, pFrame, 0);
            hoist_declarations(pCtx, pNode->c, pFrame, 0);
            return;

        case N_FOR:
            hoist_declarations(pCtx, pNode->a, pFrame, 0);
            hoist_declarations(pCtx, pNode->d, pFrame, 0);
            return;

        case N_FORIN:
            hoist_declarations(pCtx, pNode->a, pFrame, 0);
            hoist_declarations(pCtx, pNode->d, pFrame, 0);
            return;

        case N_WHILE:
        case N_DOWHILE:
            hoist_declarations(pCtx, pNode->d, pFrame, 0);
            return;

        case N_TRY:
            hoist_declarations(pCtx, pNode->a, pFrame, 0);
            hoist_declarations(pCtx, pNode->b, pFrame, 0);
            hoist_declarations(pCtx, pNode->c, pFrame, 0);
            return;

        case N_LABELED:
            hoist_declarations(pCtx, pNode->a, pFrame, 0);
            return;

        case N_EXPRSTMT:
            (void)bTopLevel;
            return;

        default: return;
    }
}

/* ------------------------------------------------------------- operators  */

static double js_mod(double a, double b)
{
    if ((b == 0) || (a != a) || (b != b)) {
        return x_nan();
    }

    return x_fmod(a, b);
}

static int compare_values(JSCtx *pCtx, JSVal left, JSVal right, JSOp op, int *pbUndefined)
{
    JSVal primLeft = js_to_primitive(pCtx, left, 0);
    JSVal primRight = js_to_primitive(pCtx, right, 0);
    int bResult = 0;

    *pbUndefined = 0;

    if ((primLeft.tag == JT_STR) && (primRight.tag == JT_STR)) {
        size_t nSizeA = primLeft.u.s->nSize;
        size_t nSizeB = primRight.u.s->nSize;
        size_t nMin = (nSizeA < nSizeB) ? nSizeA : nSizeB;
        int nCmp = x_memcmp(primLeft.u.s->pData, primRight.u.s->pData, nMin);

        if (nCmp == 0) {
            nCmp = (nSizeA < nSizeB) ? -1 : ((nSizeA > nSizeB) ? 1 : 0);
        }

        switch (op) {
            case OP_LT: bResult = (nCmp < 0); break;
            case OP_GT: bResult = (nCmp > 0); break;
            case OP_LE: bResult = (nCmp <= 0); break;
            case OP_GE: bResult = (nCmp >= 0); break;
            default: break;
        }
    } else {
        double a = js_to_number(pCtx, primLeft);
        double b = js_to_number(pCtx, primRight);

        if ((a != a) || (b != b)) {
            *pbUndefined = 1;
        } else {
            switch (op) {
                case OP_LT: bResult = (a < b); break;
                case OP_GT: bResult = (a > b); break;
                case OP_LE: bResult = (a <= b); break;
                case OP_GE: bResult = (a >= b); break;
                default: break;
            }
        }
    }

    js_release(pCtx, primLeft);
    js_release(pCtx, primRight);

    return bResult;
}

static JSVal apply_binary(JSCtx *pCtx, JSOp op, JSVal left, JSVal right)
{
    switch (op) {
        case OP_ADD: {
            JSVal primLeft = js_to_primitive(pCtx, left, 0);
            JSVal primRight = js_to_primitive(pCtx, right, 0);
            JSVal result;

            if ((primLeft.tag == JT_STR) || (primRight.tag == JT_STR)) {
                result = js_concat_str(pCtx, primLeft, primRight);
            } else {
                result = js_num(js_to_number(pCtx, primLeft) + js_to_number(pCtx, primRight));
            }

            js_release(pCtx, primLeft);
            js_release(pCtx, primRight);

            return result;
        }

        case OP_SUB: return js_num(js_to_number(pCtx, left) - js_to_number(pCtx, right));
        case OP_MUL: return js_num(js_to_number(pCtx, left) * js_to_number(pCtx, right));
        case OP_DIV: return js_num(js_to_number(pCtx, left) / js_to_number(pCtx, right));
        case OP_MOD: return js_num(js_mod(js_to_number(pCtx, left), js_to_number(pCtx, right)));

        case OP_AND: return js_num((double)(js_to_int32(pCtx, left) & js_to_int32(pCtx, right)));
        case OP_OR: return js_num((double)(js_to_int32(pCtx, left) | js_to_int32(pCtx, right)));
        case OP_XOR: return js_num((double)(js_to_int32(pCtx, left) ^ js_to_int32(pCtx, right)));
        /* Shifted as unsigned: a signed left shift of a negative value, or one
           that overflows, is undefined behaviour in C99. */
        case OP_SHL: return js_num((double)(cd_i32)(((cd_u32)js_to_int32(pCtx, left)) << (js_to_int32(pCtx, right) & 31)));
        case OP_SHR: return js_num((double)(js_to_int32(pCtx, left) >> (js_to_int32(pCtx, right) & 31)));
        case OP_USHR: return js_num((double)(((cd_u32)js_to_int32(pCtx, left)) >> (js_to_int32(pCtx, right) & 31)));

        case OP_EQ: return js_bool(js_loose_equals(pCtx, left, right));
        case OP_NE: return js_bool(!js_loose_equals(pCtx, left, right));
        case OP_SEQ: return js_bool(js_strict_equals(pCtx, left, right));
        case OP_SNE: return js_bool(!js_strict_equals(pCtx, left, right));

        case OP_LT:
        case OP_GT:
        case OP_LE:
        case OP_GE: {
            int bUndefined = 0;
            int bResult = compare_values(pCtx, left, right, op, &bUndefined);

            return js_bool(bUndefined ? 0 : bResult);
        }

        case OP_IN: {
            char sScratch[KEY_SCRATCH_SIZE];
            char *pKey = NULL;
            size_t nKeySize = 0;
            int bResult = 0;

            if (right.tag != JT_OBJ) {
                return js_throw(pCtx, "TypeError: 'in' requires an object");
            }

            pKey = key_from_value(pCtx, left, &nKeySize, sScratch);
            bResult = jsobj_has(pCtx, right.u.o, pKey, nKeySize);
            key_release(pKey, sScratch);

            return js_bool(bResult);
        }

        case OP_INSTANCEOF: {
            JSVal prototype;
            JSObj *pCurrent = NULL;

            if (!js_is_callable(right)) {
                return js_throw(pCtx, "TypeError: 'instanceof' requires a function");
            }

            if (left.tag != JT_OBJ) {
                return js_bool(0);
            }

            prototype = js_get(pCtx, right, "prototype");
            pCurrent = left.u.o->pProto;

            while (pCurrent) {
                if ((prototype.tag == JT_OBJ) && (pCurrent == prototype.u.o)) {
                    js_release(pCtx, prototype);

                    return js_bool(1);
                }

                pCurrent = pCurrent->pProto;
            }

            js_release(pCtx, prototype);

            return js_bool(0);
        }

        default: break;
    }

    return js_undefined();
}

static const char *typeof_string(JSVal value)
{
    switch (value.tag) {
        case JT_UNDEF: return "undefined";
        case JT_NULL: return "object";
        case JT_BOOL: return "boolean";
        case JT_NUM: return "number";
        case JT_STR: return "string";
        case JT_OBJ: return js_is_callable(value) ? "function" : "object";
        default: break;
    }

    return "undefined";
}

/* ------------------------------------------------------------ expression  */

static JSVal eval_call(JSCtx *pCtx, JSNode *pNode, JSFrame *pFrame)
{
    JSVal thisVal = js_undefined();
    JSVal fn = js_undefined();
    JSVal *pArgv = NULL;
    JSVal result = js_undefined();
    size_t i = 0;
    JSNode *pCallee = pNode->a;

    if (pCallee->type == N_MEMBER) {
        thisVal = eval_expr(pCtx, pCallee->a, pFrame);

        if (pCtx->bException) {
            return js_undefined();
        }

        fn = get_property(pCtx, thisVal, pCallee->pStr, x_strlen(pCallee->pStr));
    } else if (pCallee->type == N_INDEX) {
        char sScratch[KEY_SCRATCH_SIZE];
        JSVal key;
        char *pKey = NULL;
        size_t nKeySize = 0;

        thisVal = eval_expr(pCtx, pCallee->a, pFrame);

        if (pCtx->bException) {
            return js_undefined();
        }

        key = eval_expr(pCtx, pCallee->b, pFrame);

        if (pCtx->bException) {
            js_release(pCtx, thisVal);
            js_release(pCtx, key);

            return js_undefined();
        }

        pKey = key_from_value(pCtx, key, &nKeySize, sScratch);
        js_release(pCtx, key);
        fn = get_property(pCtx, thisVal, pKey, nKeySize);
        key_release(pKey, sScratch);
    } else {
        fn = eval_expr(pCtx, pCallee, pFrame);
    }

    if (pCtx->bException) {
        js_release(pCtx, thisVal);
        js_release(pCtx, fn);

        return js_undefined();
    }

    if (!js_is_callable(fn)) {
        const char *pName = "expression";

        if (pCallee->type == N_MEMBER) {
            pName = pCallee->pStr;
        } else if (pCallee->type == N_IDENT) {
            pName = pCallee->pStr;
        }

        js_release(pCtx, thisVal);
        js_release(pCtx, fn);

        return js_throw(pCtx, "TypeError: %s is not a function (line %d)", pName, pNode->nLine);
    }

    if (pNode->nList) {
        pArgv = (JSVal *)cd_malloc(pNode->nList * sizeof(JSVal));
    }

    for (i = 0; i < pNode->nList; i++) {
        pArgv[i] = eval_expr(pCtx, pNode->ppList[i], pFrame);

        if (pCtx->bException) {
            size_t j = 0;

            for (j = 0; j <= i; j++) {
                js_release(pCtx, pArgv[j]);
            }

            cd_free(pArgv);
            js_release(pCtx, thisVal);
            js_release(pCtx, fn);

            return js_undefined();
        }
    }

    result = js_call_function(pCtx, fn.u.o, thisVal, (int)pNode->nList, pArgv, 0);

    for (i = 0; i < pNode->nList; i++) {
        js_release(pCtx, pArgv[i]);
    }

    cd_free(pArgv);
    js_release(pCtx, thisVal);
    js_release(pCtx, fn);

    return result;
}

static JSVal eval_new(JSCtx *pCtx, JSNode *pNode, JSFrame *pFrame)
{
    JSVal fn = eval_expr(pCtx, pNode->a, pFrame);
    JSVal *pArgv = NULL;
    JSVal result = js_undefined();
    size_t i = 0;

    if (pCtx->bException) {
        js_release(pCtx, fn);

        return js_undefined();
    }

    if (pNode->nList) {
        pArgv = (JSVal *)cd_malloc(pNode->nList * sizeof(JSVal));
    }

    for (i = 0; i < pNode->nList; i++) {
        pArgv[i] = eval_expr(pCtx, pNode->ppList[i], pFrame);
    }

    if (!pCtx->bException) {
        result = js_construct(pCtx, fn, (int)pNode->nList, pArgv);
    }

    for (i = 0; i < pNode->nList; i++) {
        js_release(pCtx, pArgv[i]);
    }

    cd_free(pArgv);
    js_release(pCtx, fn);

    return result;
}

/* A resolved assignment target. ECMAScript evaluates the left hand side to a
   reference once, before the right hand side runs, and reuses that reference
   for both the read and the store.

   For an indexed target the reference keeps the key *value*, not the converted
   key string: ToPropertyKey is applied again by every read and every store, so
   `o[k] += 1` calls a side effecting k.toString() twice, as the reference
   engine does. Only the key expression itself is evaluated once. */
typedef struct {
    JSNode *pTarget;
    JSVal base;
    JSVal key;
} JSRef;

static int ref_resolve(JSCtx *pCtx, JSNode *pTarget, JSFrame *pFrame, JSRef *pRef)
{
    pRef->pTarget = pTarget;
    pRef->base = js_undefined();
    pRef->key = js_undefined();

    if ((pTarget->type == N_MEMBER) || (pTarget->type == N_INDEX)) {
        pRef->base = eval_expr(pCtx, pTarget->a, pFrame);

        if (pCtx->bException) {
            return 0;
        }
    }

    if (pTarget->type == N_INDEX) {
        pRef->key = eval_expr(pCtx, pTarget->b, pFrame);

        if (pCtx->bException) {
            return 0;
        }
    }

    return 1;
}

static void ref_free(JSCtx *pCtx, JSRef *pRef)
{
    js_release(pCtx, pRef->key);
    js_release(pCtx, pRef->base);
}

static JSVal ref_get(JSCtx *pCtx, JSRef *pRef, JSFrame *pFrame)
{
    JSNode *pTarget = pRef->pTarget;

    if (pTarget->type == N_IDENT) {
        int bFound = 0;
        JSVal value = scope_get_node(pCtx, pFrame->pScope, pTarget, &bFound);

        if (!bFound) {
            js_release(pCtx, value);

            return js_throw(pCtx, "ReferenceError: %s is not defined (line %d)", pTarget->pStr, pTarget->nLine);
        }

        return value;
    }

    if (pTarget->type == N_MEMBER) {
        return get_property(pCtx, pRef->base, pTarget->pStr, x_strlen(pTarget->pStr));
    }

    if (pTarget->type == N_INDEX) {
        char sScratch[KEY_SCRATCH_SIZE];
        size_t nKeySize = 0;
        char *pKey = key_from_value(pCtx, pRef->key, &nKeySize, sScratch);
        JSVal result = get_property(pCtx, pRef->base, pKey, nKeySize);

        key_release(pKey, sScratch);

        return result;
    }

    return eval_expr(pCtx, pTarget, pFrame);
}

static void ref_put(JSCtx *pCtx, JSRef *pRef, JSFrame *pFrame, JSVal value)
{
    JSNode *pTarget = pRef->pTarget;

    if (pTarget->type == N_IDENT) {
        scope_set_node(pCtx, pFrame->pScope, pTarget, value);
    } else if (pTarget->type == N_MEMBER) {
        set_property(pCtx, pRef->base, pTarget->pStr, x_strlen(pTarget->pStr), value);
    } else if (pTarget->type == N_INDEX) {
        char sScratch[KEY_SCRATCH_SIZE];
        size_t nKeySize = 0;
        char *pKey = key_from_value(pCtx, pRef->key, &nKeySize, sScratch);

        set_property(pCtx, pRef->base, pKey, nKeySize, value);
        key_release(pKey, sScratch);
    } else {
        js_release(pCtx, value);
    }
}

static JSVal eval_assign(JSCtx *pCtx, JSNode *pNode, JSFrame *pFrame)
{
    JSVal value = js_undefined();
    JSRef ref;

    if (!ref_resolve(pCtx, pNode->a, pFrame, &ref)) {
        ref_free(pCtx, &ref);

        return js_undefined();
    }

    if (pNode->op == OP_ASSIGN) {
        value = eval_expr(pCtx, pNode->b, pFrame);
    } else {
        JSVal current = ref_get(pCtx, &ref, pFrame);
        JSVal operand;

        if (pCtx->bException) {
            js_release(pCtx, current);
            ref_free(pCtx, &ref);

            return js_undefined();
        }

        operand = eval_expr(pCtx, pNode->b, pFrame);

        if (pCtx->bException) {
            js_release(pCtx, current);
            js_release(pCtx, operand);
            ref_free(pCtx, &ref);

            return js_undefined();
        }

        value = apply_binary(pCtx, pNode->op, current, operand);
        js_release(pCtx, current);
        js_release(pCtx, operand);
    }

    if (pCtx->bException) {
        js_release(pCtx, value);
        ref_free(pCtx, &ref);

        return js_undefined();
    }

    ref_put(pCtx, &ref, pFrame, js_dup(value));
    ref_free(pCtx, &ref);

    return value;
}

static JSVal eval_update(JSCtx *pCtx, JSNode *pNode, JSFrame *pFrame)
{
    JSRef ref;
    JSVal current;
    double nOld = 0;
    double nNew = 0;
    JSVal newValue;

    if (!ref_resolve(pCtx, pNode->a, pFrame, &ref)) {
        ref_free(pCtx, &ref);

        return js_undefined();
    }

    current = ref_get(pCtx, &ref, pFrame);

    if (pCtx->bException) {
        js_release(pCtx, current);
        ref_free(pCtx, &ref);

        return js_undefined();
    }

    nOld = js_to_number(pCtx, current);
    js_release(pCtx, current);
    nNew = (pNode->op == OP_INC) ? (nOld + 1) : (nOld - 1);
    newValue = js_num(nNew);

    ref_put(pCtx, &ref, pFrame, js_dup(newValue));
    ref_free(pCtx, &ref);

    if (pNode->nNum != 0) {
        return newValue;
    }

    js_release(pCtx, newValue);

    return js_num(nOld);
}

static JSVal eval_unary(JSCtx *pCtx, JSNode *pNode, JSFrame *pFrame)
{
    if (pNode->op == OP_TYPEOF) {
        JSVal value;

        if (pNode->a->type == N_IDENT) {
            int bFound = 0;

            value = scope_get_node(pCtx, pFrame->pScope, pNode->a, &bFound);

            if (!bFound) {
                return js_str(pCtx, "undefined");
            }
        } else {
            value = eval_expr(pCtx, pNode->a, pFrame);

            if (pCtx->bException) {
                js_release(pCtx, value);

                return js_undefined();
            }
        }

        {
            JSVal result = js_str(pCtx, typeof_string(value));

            js_release(pCtx, value);

            return result;
        }
    }

    if (pNode->op == OP_DELETE) {
        JSNode *pTarget = pNode->a;

        if (pTarget->type == N_MEMBER) {
            JSVal base = eval_expr(pCtx, pTarget->a, pFrame);

            if ((!pCtx->bException) && (base.tag == JT_OBJ)) {
                jsprops_del(pCtx, &base.u.o->props, pTarget->pStr);
            }

            js_release(pCtx, base);

            /* Sloppy-mode delete reports success even for a property that was
               not there, which is what the index form below does as well. */
            return js_bool(1);
        }

        if (pTarget->type == N_INDEX) {
            JSVal base = eval_expr(pCtx, pTarget->a, pFrame);
            JSVal key = js_undefined();

            if (!pCtx->bException) {
                key = eval_expr(pCtx, pTarget->b, pFrame);
            }

            if ((!pCtx->bException) && (base.tag == JT_OBJ)) {
                char sScratch[KEY_SCRATCH_SIZE];
                char *pKey = key_from_value(pCtx, key, NULL, sScratch);

                jsprops_del(pCtx, &base.u.o->props, pKey);
                key_release(pKey, sScratch);
            }

            js_release(pCtx, key);
            js_release(pCtx, base);

            return js_bool(1);
        }

        return js_bool(1);
    }

    {
        JSVal value = eval_expr(pCtx, pNode->a, pFrame);
        JSVal result = js_undefined();

        if (pCtx->bException) {
            js_release(pCtx, value);

            return js_undefined();
        }

        switch (pNode->op) {
            case OP_NOT: result = js_bool(!js_to_bool(pCtx, value)); break;
            case OP_BNOT: result = js_num((double)(~js_to_int32(pCtx, value))); break;
            case OP_NEG: result = js_num(-js_to_number(pCtx, value)); break;
            case OP_POS: result = js_num(js_to_number(pCtx, value)); break;
            case OP_VOID: result = js_undefined(); break;
            default: break;
        }

        js_release(pCtx, value);

        return result;
    }
}

static JSVal eval_expr_node(JSCtx *pCtx, JSNode *pNode, JSFrame *pFrame);

static JSVal eval_expr(JSCtx *pCtx, JSNode *pNode, JSFrame *pFrame)
{
    JSVal result;

    if (pCtx->nEvalDepth >= pCtx->nMaxEvalDepth) {
        return js_throw(pCtx, "RangeError: maximum expression nesting exceeded");
    }

    pCtx->nEvalDepth++;
    result = eval_expr_node(pCtx, pNode, pFrame);
    pCtx->nEvalDepth--;

    return result;
}

static JSVal eval_expr_node(JSCtx *pCtx, JSNode *pNode, JSFrame *pFrame)
{
    if ((pNode == NULL) || pCtx->bException) {
        return js_undefined();
    }

    switch (pNode->type) {
        case N_NUM: return js_num(pNode->nNum);
        case N_STR: return js_strn(pCtx, pNode->pStr, (size_t)pNode->nNum);
        case N_NULL: return js_null();
        case N_BOOL: return js_bool(pNode->nNum != 0);
        case N_THIS: return js_dup(pFrame->thisVal);
        case N_EMPTY: return js_undefined();

        case N_REGEXP: return js_new_regexp_val(pCtx, pNode->pStr, pNode->pStr2);

        case N_IDENT: {
            int bFound = 0;
            JSVal value = scope_get_node(pCtx, pFrame->pScope, pNode, &bFound);

            if (!bFound) {
                js_release(pCtx, value);

                return js_throw(pCtx, "ReferenceError: %s is not defined (line %d)", pNode->pStr, pNode->nLine);
            }

            return value;
        }

        case N_ARRAY: {
            JSVal array = js_new_array(pCtx);
            size_t i = 0;

            for (i = 0; i < pNode->nList; i++) {
                JSVal item = eval_expr(pCtx, pNode->ppList[i], pFrame);

                if (pCtx->bException) {
                    js_release(pCtx, item);
                    js_release(pCtx, array);

                    return js_undefined();
                }

                js_set_index(pCtx, array, (cd_i64)i, item);
            }

            array.u.o->nArrayLen = (cd_i64)pNode->nList;

            return array;
        }

        case N_OBJECT: {
            JSVal object = js_new_object(pCtx);
            size_t i = 0;

            for (i = 0; i < pNode->nList; i++) {
                JSNode *pProp = pNode->ppList[i];
                JSVal value = eval_expr(pCtx, pProp->a, pFrame);

                if (pCtx->bException) {
                    js_release(pCtx, value);
                    js_release(pCtx, object);

                    return js_undefined();
                }

                jsobj_put(pCtx, object.u.o, pProp->pStr, x_strlen(pProp->pStr), value);
            }

            return object;
        }

        case N_FUNCTION: return make_function(pCtx, pNode, pFrame);

        case N_MEMBER: {
            JSVal base = eval_expr(pCtx, pNode->a, pFrame);
            JSVal result;

            if (pCtx->bException) {
                js_release(pCtx, base);

                return js_undefined();
            }

            result = get_property(pCtx, base, pNode->pStr, x_strlen(pNode->pStr));
            js_release(pCtx, base);

            return result;
        }

        case N_INDEX: {
            JSVal base = eval_expr(pCtx, pNode->a, pFrame);
            JSVal key;
            JSVal result;
            char sScratch[KEY_SCRATCH_SIZE];
            char *pKey = NULL;
            size_t nKeySize = 0;

            if (pCtx->bException) {
                js_release(pCtx, base);

                return js_undefined();
            }

            key = eval_expr(pCtx, pNode->b, pFrame);

            if (pCtx->bException) {
                js_release(pCtx, base);
                js_release(pCtx, key);

                return js_undefined();
            }

            pKey = key_from_value(pCtx, key, &nKeySize, sScratch);
            js_release(pCtx, key);
            result = get_property(pCtx, base, pKey, nKeySize);
            key_release(pKey, sScratch);
            js_release(pCtx, base);

            return result;
        }

        case N_CALL: return eval_call(pCtx, pNode, pFrame);
        case N_NEW: return eval_new(pCtx, pNode, pFrame);
        case N_ASSIGN: return eval_assign(pCtx, pNode, pFrame);
        case N_UPDATE: return eval_update(pCtx, pNode, pFrame);
        case N_UNARY: return eval_unary(pCtx, pNode, pFrame);

        case N_BINARY: {
            JSVal left = eval_expr(pCtx, pNode->a, pFrame);
            JSVal right;
            JSVal result;

            if (pCtx->bException) {
                js_release(pCtx, left);

                return js_undefined();
            }

            right = eval_expr(pCtx, pNode->b, pFrame);

            if (pCtx->bException) {
                js_release(pCtx, left);
                js_release(pCtx, right);

                return js_undefined();
            }

            result = apply_binary(pCtx, pNode->op, left, right);
            js_release(pCtx, left);
            js_release(pCtx, right);

            return result;
        }

        case N_LOGICAL: {
            JSVal left = eval_expr(pCtx, pNode->a, pFrame);
            int bLeft = 0;

            if (pCtx->bException) {
                js_release(pCtx, left);

                return js_undefined();
            }

            bLeft = js_to_bool(pCtx, left);

            if (((pNode->op == OP_LAND) && (!bLeft)) || ((pNode->op == OP_LOR) && bLeft)) {
                return left;
            }

            js_release(pCtx, left);

            return eval_expr(pCtx, pNode->b, pFrame);
        }

        case N_COND: {
            JSVal test = eval_expr(pCtx, pNode->a, pFrame);
            int bTest = 0;

            if (pCtx->bException) {
                js_release(pCtx, test);

                return js_undefined();
            }

            bTest = js_to_bool(pCtx, test);
            js_release(pCtx, test);

            return eval_expr(pCtx, bTest ? pNode->b : pNode->c, pFrame);
        }

        case N_SEQ: {
            JSVal left = eval_expr(pCtx, pNode->a, pFrame);

            js_release(pCtx, left);

            return eval_expr(pCtx, pNode->b, pFrame);
        }

        default: break;
    }

    return js_undefined();
}

/* ------------------------------------------------------------ statements  */

static JSCompletion exec_block_list(JSCtx *pCtx, JSNode *pNode, JSFrame *pFrame)
{
    size_t i = 0;

    for (i = 0; i < pNode->nList; i++) {
        JSCompletion completion = exec_stmt(pCtx, pNode->ppList[i], pFrame);

        if (pCtx->bException) {
            js_release(pCtx, completion.value);

            return completion_normal();
        }

        if (completion.type != CT_NORMAL) {
            return completion;
        }

        js_release(pCtx, completion.value);
    }

    return completion_normal();
}

static int loop_break(JSCompletion *pCompletion, const char *pLabel, JSCompletion *pOut)
{
    if (pCompletion->type == CT_BREAK) {
        if ((pCompletion->pLabel == NULL) || (pLabel && (x_strcmp(pCompletion->pLabel, pLabel) == 0))) {
            *pOut = completion_normal();

            return 1;
        }

        *pOut = *pCompletion;

        return 1;
    }

    if (pCompletion->type == CT_RETURN) {
        *pOut = *pCompletion;

        return 1;
    }

    if (pCompletion->type == CT_CONTINUE) {
        if ((pCompletion->pLabel != NULL) && (!(pLabel && (x_strcmp(pCompletion->pLabel, pLabel) == 0)))) {
            *pOut = *pCompletion;

            return 1;
        }
    }

    return 0;
}

static JSCompletion exec_stmt_labeled(JSCtx *pCtx, JSNode *pNode, JSFrame *pFrame, const char *pLabel);

static JSCompletion exec_stmt(JSCtx *pCtx, JSNode *pNode, JSFrame *pFrame)
{
    return exec_stmt_labeled(pCtx, pNode, pFrame, NULL);
}

static JSCompletion exec_stmt_node(JSCtx *pCtx, JSNode *pNode, JSFrame *pFrame, const char *pLabel);

static JSCompletion exec_stmt_labeled(JSCtx *pCtx, JSNode *pNode, JSFrame *pFrame, const char *pLabel)
{
    JSCompletion completion;

    if (pCtx->nEvalDepth >= pCtx->nMaxEvalDepth) {
        js_throw(pCtx, "RangeError: maximum expression nesting exceeded");

        return completion_normal();
    }

    pCtx->nEvalDepth++;
    completion = exec_stmt_node(pCtx, pNode, pFrame, pLabel);
    pCtx->nEvalDepth--;

    return completion;
}

static JSCompletion exec_stmt_node(JSCtx *pCtx, JSNode *pNode, JSFrame *pFrame, const char *pLabel)
{
    JSCompletion completion = completion_normal();

    if ((pNode == NULL) || pCtx->bException) {
        return completion;
    }

    switch (pNode->type) {
        case N_PROGRAM:
        case N_BLOCK: return exec_block_list(pCtx, pNode, pFrame);

        case N_EMPTY: return completion;

        case N_FUNCTION:
            /* Handled by hoisting. */
            return completion;

        case N_EXPRSTMT: {
            JSVal value = eval_expr(pCtx, pNode->a, pFrame);

            js_release(pCtx, value);

            return completion;
        }

        case N_VAR: {
            size_t i = 0;

            for (i = 0; i < pNode->nList; i++) {
                JSNode *pDecl = pNode->ppList[i];

                if (pDecl->a) {
                    JSVal value = eval_expr(pCtx, pDecl->a, pFrame);

                    if (pCtx->bException) {
                        js_release(pCtx, value);

                        return completion;
                    }

                    scope_set_node(pCtx, pFrame->pScope, pDecl, value);
                } else {
                    JSObj *pVars = pFrame->pScope ? pFrame->pScope->pVars : pCtx->pGlobal;

                    if (jsprops_find(&pVars->props, pDecl->pStr, x_strlen(pDecl->pStr)) == NULL) {
                        scope_declare(pCtx, pFrame->pScope, pDecl->pStr, js_undefined());
                    }
                }
            }

            return completion;
        }

        case N_IF: {
            JSVal test = eval_expr(pCtx, pNode->a, pFrame);
            int bTest = 0;

            if (pCtx->bException) {
                js_release(pCtx, test);

                return completion;
            }

            bTest = js_to_bool(pCtx, test);
            js_release(pCtx, test);

            if (bTest) {
                return exec_stmt(pCtx, pNode->b, pFrame);
            }

            if (pNode->c) {
                return exec_stmt(pCtx, pNode->c, pFrame);
            }

            return completion;
        }

        case N_WHILE: {
            for (;;) {
                JSVal test = eval_expr(pCtx, pNode->a, pFrame);
                int bTest = 0;
                JSCompletion body;
                JSCompletion out;

                if (pCtx->bException) {
                    js_release(pCtx, test);

                    return completion_normal();
                }

                bTest = js_to_bool(pCtx, test);
                js_release(pCtx, test);

                if (!bTest) {
                    break;
                }

                body = exec_stmt(pCtx, pNode->d, pFrame);

                if (pCtx->bException) {
                    js_release(pCtx, body.value);

                    return completion_normal();
                }

                if (loop_break(&body, pLabel, &out)) {
                    return out;
                }

                js_release(pCtx, body.value);
            }

            return completion_normal();
        }

        case N_DOWHILE: {
            for (;;) {
                JSCompletion body = exec_stmt(pCtx, pNode->d, pFrame);
                JSCompletion out;
                JSVal test;
                int bTest = 0;

                if (pCtx->bException) {
                    js_release(pCtx, body.value);

                    return completion_normal();
                }

                if (loop_break(&body, pLabel, &out)) {
                    return out;
                }

                js_release(pCtx, body.value);

                test = eval_expr(pCtx, pNode->a, pFrame);

                if (pCtx->bException) {
                    js_release(pCtx, test);

                    return completion_normal();
                }

                bTest = js_to_bool(pCtx, test);
                js_release(pCtx, test);

                if (!bTest) {
                    break;
                }
            }

            return completion_normal();
        }

        case N_FOR: {
            if (pNode->a) {
                JSCompletion init = exec_stmt(pCtx, pNode->a, pFrame);

                js_release(pCtx, init.value);

                if (pCtx->bException) {
                    return completion_normal();
                }
            }

            for (;;) {
                JSCompletion body;
                JSCompletion out;

                if (pNode->b) {
                    JSVal test = eval_expr(pCtx, pNode->b, pFrame);
                    int bTest = 0;

                    if (pCtx->bException) {
                        js_release(pCtx, test);

                        return completion_normal();
                    }

                    bTest = js_to_bool(pCtx, test);
                    js_release(pCtx, test);

                    if (!bTest) {
                        break;
                    }
                }

                body = exec_stmt(pCtx, pNode->d, pFrame);

                if (pCtx->bException) {
                    js_release(pCtx, body.value);

                    return completion_normal();
                }

                if (loop_break(&body, pLabel, &out)) {
                    return out;
                }

                js_release(pCtx, body.value);

                if (pNode->c) {
                    JSVal update = eval_expr(pCtx, pNode->c, pFrame);

                    js_release(pCtx, update);

                    if (pCtx->bException) {
                        return completion_normal();
                    }
                }
            }

            return completion_normal();
        }

        case N_FORIN: {
            JSVal object = eval_expr(pCtx, pNode->b, pFrame);
            JSNode *pVarDecl = NULL;
            JSNode *pTarget = NULL;
            CDVec vecKeys;
            size_t i = 0;

            if (pCtx->bException) {
                js_release(pCtx, object);

                return completion_normal();
            }

            if (pNode->a->type == N_VAR) {
                pVarDecl = pNode->a->ppList[0];
            } else if (pNode->a->type == N_EXPRSTMT) {
                pTarget = pNode->a->a;
            }

            cdvec_init(&vecKeys);

            if (object.tag == JT_OBJ) {
                JSPropMap *pMap = &object.u.o->props;
                size_t j = 0;

                for (j = 0; j < pMap->nSize; j++) {
                    if ((!pMap->pEntries[j].bDeleted) && (!pMap->pEntries[j].bDontEnum)) {
                        cdvec_push(&vecKeys, cd_strdup(pMap->pEntries[j].pKey));
                    }
                }
            } else if (object.tag == JT_STR) {
                size_t j = 0;

                for (j = 0; j < object.u.s->nSize; j++) {
                    char sBuf[32];

                    x_snprintf(sBuf, sizeof(sBuf), "%llu", (unsigned long long)j);
                    cdvec_push(&vecKeys, cd_strdup(sBuf));
                }
            }

            for (i = 0; i < vecKeys.nSize; i++) {
                char *pKey = (char *)vecKeys.ppData[i];
                JSCompletion body;
                JSCompletion out;

                if (pVarDecl) {
                    scope_set_node(pCtx, pFrame->pScope, pVarDecl, js_str(pCtx, pKey));
                } else if (pTarget && (pTarget->type == N_IDENT)) {
                    scope_set_node(pCtx, pFrame->pScope, pTarget, js_str(pCtx, pKey));
                } else if (pTarget && (pTarget->type == N_MEMBER)) {
                    JSVal base = eval_expr(pCtx, pTarget->a, pFrame);

                    set_property(pCtx, base, pTarget->pStr, x_strlen(pTarget->pStr), js_str(pCtx, pKey));
                    js_release(pCtx, base);
                }

                body = exec_stmt(pCtx, pNode->d, pFrame);

                if (pCtx->bException) {
                    js_release(pCtx, body.value);
                    break;
                }

                if (loop_break(&body, pLabel, &out)) {
                    size_t j = 0;

                    for (j = i; j < vecKeys.nSize; j++) {
                        cd_free(vecKeys.ppData[j]);
                    }

                    cdvec_free(&vecKeys);
                    js_release(pCtx, object);

                    return out;
                }

                js_release(pCtx, body.value);
                cd_free(pKey);
                vecKeys.ppData[i] = NULL;
            }

            for (i = 0; i < vecKeys.nSize; i++) {
                cd_free(vecKeys.ppData[i]);
            }

            cdvec_free(&vecKeys);
            js_release(pCtx, object);

            return completion_normal();
        }

        case N_RETURN: {
            JSCompletion result;

            result.type = CT_RETURN;
            result.pLabel = NULL;
            result.value = pNode->a ? eval_expr(pCtx, pNode->a, pFrame) : js_undefined();

            return result;
        }

        case N_BREAK: {
            JSCompletion result;

            result.type = CT_BREAK;
            result.pLabel = pNode->pStr;
            result.value = js_undefined();

            return result;
        }

        case N_CONTINUE: {
            JSCompletion result;

            result.type = CT_CONTINUE;
            result.pLabel = pNode->pStr;
            result.value = js_undefined();

            return result;
        }

        case N_THROW: {
            JSVal value = eval_expr(pCtx, pNode->a, pFrame);

            if (!pCtx->bException) {
                js_throw_value(pCtx, value);
            } else {
                js_release(pCtx, value);
            }

            return completion_normal();
        }

        case N_TRY: {
            JSCompletion result = exec_stmt(pCtx, pNode->a, pFrame);

            if (pCtx->bException && pNode->b) {
                JSVal exception = pCtx->exception;
                JSScope *pCatchScope = jsscope_new(pCtx, pFrame->pScope);
                JSFrame catchFrame = *pFrame;

                pCtx->bException = 0;
                pCtx->exception = js_undefined();
                cd_free(pCtx->pErrorText);
                pCtx->pErrorText = NULL;

                catchFrame.pScope = pCatchScope;

                if (pNode->pStr) {
                    scope_declare(pCtx, pCatchScope, pNode->pStr, exception);
                } else {
                    js_release(pCtx, exception);
                }

                js_release(pCtx, result.value);
                result = exec_stmt(pCtx, pNode->b, &catchFrame);
                jsscope_unref(pCtx, pCatchScope);
            }

            if (pNode->c) {
                int bSavedException = pCtx->bException;
                JSVal savedValue = pCtx->exception;
                char *pSavedText = pCtx->pErrorText;
                JSCompletion finallyResult;

                pCtx->bException = 0;
                pCtx->exception = js_undefined();
                pCtx->pErrorText = NULL;

                finallyResult = exec_stmt(pCtx, pNode->c, pFrame);

                if (pCtx->bException) {
                    /* An exception in `finally` replaces the pending one. */
                    if (bSavedException) {
                        js_release(pCtx, savedValue);
                        cd_free(pSavedText);
                    }

                    js_release(pCtx, result.value);
                    js_release(pCtx, finallyResult.value);

                    return completion_normal();
                }

                if (finallyResult.type != CT_NORMAL) {
                    /* An abrupt completion in `finally` replaces the try
                       completion, discarding the pending exception. */
                    if (bSavedException) {
                        js_release(pCtx, savedValue);
                        cd_free(pSavedText);
                    }

                    js_release(pCtx, result.value);

                    return finallyResult;
                }

                pCtx->bException = bSavedException;
                pCtx->exception = savedValue;
                pCtx->pErrorText = pSavedText;

                js_release(pCtx, finallyResult.value);
            }

            return result;
        }

        case N_SWITCH: {
            JSVal discriminant = eval_expr(pCtx, pNode->a, pFrame);
            size_t i = 0;
            size_t nStart = (size_t)-1;
            size_t nDefault = (size_t)-1;

            if (pCtx->bException) {
                js_release(pCtx, discriminant);

                return completion_normal();
            }

            for (i = 0; i < pNode->nList; i++) {
                JSNode *pCase = pNode->ppList[i];

                if (pCase->a == NULL) {
                    nDefault = i;
                    continue;
                }

                {
                    JSVal test = eval_expr(pCtx, pCase->a, pFrame);
                    int bMatch = 0;

                    if (pCtx->bException) {
                        js_release(pCtx, test);
                        js_release(pCtx, discriminant);

                        return completion_normal();
                    }

                    bMatch = js_strict_equals(pCtx, discriminant, test);
                    js_release(pCtx, test);

                    if (bMatch) {
                        nStart = i;
                        break;
                    }
                }
            }

            js_release(pCtx, discriminant);

            if (nStart == (size_t)-1) {
                nStart = nDefault;
            }

            if (nStart == (size_t)-1) {
                return completion_normal();
            }

            for (i = nStart; i < pNode->nList; i++) {
                JSCompletion body = exec_block_list(pCtx, pNode->ppList[i], pFrame);

                if (pCtx->bException) {
                    js_release(pCtx, body.value);

                    return completion_normal();
                }

                if (body.type == CT_BREAK) {
                    if (body.pLabel == NULL) {
                        js_release(pCtx, body.value);

                        return completion_normal();
                    }

                    return body;
                }

                if (body.type != CT_NORMAL) {
                    return body;
                }

                js_release(pCtx, body.value);
            }

            return completion_normal();
        }

        case N_LABELED: {
            JSCompletion body = exec_stmt_labeled(pCtx, pNode->a, pFrame, pNode->pStr);

            if ((body.type == CT_BREAK) && body.pLabel && (x_strcmp(body.pLabel, pNode->pStr) == 0)) {
                js_release(pCtx, body.value);

                return completion_normal();
            }

            return body;
        }

        default: {
            JSVal value = eval_expr(pCtx, pNode, pFrame);

            js_release(pCtx, value);

            return completion_normal();
        }
    }
}

/* ---------------------------------------------------------------- driver  */

JSVal js_run_program(JSCtx *pCtx, JSNode *pProgram, JSScope *pScope, JSVal thisVal)
{
    JSFrame frame;
    JSCompletion completion;

    frame.pScope = pScope;
    frame.thisVal = thisVal;
    frame.pFunction = NULL;

    hoist_declarations(pCtx, pProgram, &frame, 1);

    if (pCtx->bException) {
        return js_undefined();
    }

    completion = exec_stmt(pCtx, pProgram, &frame);

    if (completion.type == CT_RETURN) {
        return completion.value;
    }

    js_release(pCtx, completion.value);

    return js_undefined();
}
