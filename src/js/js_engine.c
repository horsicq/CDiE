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

/* js_engine.c - context creation, teardown and program evaluation. */

#include "js_internal.h"
#include "js_ast.h"

JSCtx *js_new(void)
{
    JSCtx *pCtx = (JSCtx *)cd_calloc(1, sizeof(JSCtx));

    /* Deep enough for any database rule, shallow enough that the native
     * stack cannot be exhausted by a runaway recursive script.            */
    pCtx->nMaxCallDepth = 200;
    /* The evaluator recurses once per AST node, and a left-leaning operator
     * chain nests as deeply as it is long without the parser ever recursing,
     * so the evaluator needs a ceiling of its own. Measured overflow of an
     * 8 MB stack sits above 18000 levels; this leaves better than a factor
     * of two in hand.                                                      */
    pCtx->nMaxEvalDepth = 8000;
    pCtx->exception = js_undefined();
    cdvec_init(&pCtx->vecPrograms);

    /* The prototypes have to exist before the global object is populated. */
    pCtx->pObjectProto = jsobj_new(pCtx, JCLASS_OBJECT, NULL);
    pCtx->pFunctionProto = jsobj_new(pCtx, JCLASS_OBJECT, pCtx->pObjectProto);
    pCtx->pArrayProto = jsobj_new(pCtx, JCLASS_OBJECT, pCtx->pObjectProto);
    pCtx->pStringProto = jsobj_new(pCtx, JCLASS_OBJECT, pCtx->pObjectProto);
    pCtx->pNumberProto = jsobj_new(pCtx, JCLASS_OBJECT, pCtx->pObjectProto);
    pCtx->pBooleanProto = jsobj_new(pCtx, JCLASS_OBJECT, pCtx->pObjectProto);
    pCtx->pRegExpProto = jsobj_new(pCtx, JCLASS_OBJECT, pCtx->pObjectProto);
    pCtx->pErrorProto = jsobj_new(pCtx, JCLASS_OBJECT, pCtx->pObjectProto);

    pCtx->pGlobal = jsobj_new(pCtx, JCLASS_OBJECT, pCtx->pObjectProto);
    pCtx->pGlobalScope = NULL;

    js_install_builtins(pCtx);

    jsobj_put_hidden(pCtx, pCtx->pGlobal, "globalThis", jsval_obj(jsobj_ref(pCtx->pGlobal)));

    return pCtx;
}

void js_free(JSCtx *pCtx)
{
    size_t i = 0;

    if (pCtx == NULL) {
        return;
    }

    for (i = 0; i < pCtx->vecPrograms.nSize; i++) {
        js_free_node((JSNode *)pCtx->vecPrograms.ppData[i]);
    }

    cdvec_free(&pCtx->vecPrograms);

    /* Whatever is left is caught in a reference cycle; sweep it flat, without
     * releasing the values, so that nothing can be freed twice.           */
    {
        JSObj *pObj = pCtx->pAllObjects;

        while (pObj) {
            JSObj *pNext = pObj->pNextAll;
            size_t j = 0;

            for (j = 0; j < pObj->props.nSize; j++) {
                cd_free(pObj->props.pEntries[j].pKey);
            }

            cd_free(pObj->props.pEntries);
            cd_free(pObj->props.pIndex);
            cd_free(pObj->pBoundArgs);
            cd_free(pObj->pFnName);

            if (pObj->pRegExp) {
                jsregexp_free(pObj->pRegExp);
            }

            cd_free(pObj);
            pObj = pNext;
        }
    }

    {
        JSScope *pScope = pCtx->pAllScopes;

        while (pScope) {
            JSScope *pNext = pScope->pNextAll;

            cd_free(pScope);
            pScope = pNext;
        }
    }

    {
        JSStr *pStr = pCtx->pAllStrings;

        while (pStr) {
            JSStr *pNext = pStr->pNextAll;

            cd_free(pStr->pData);
            cd_free(pStr);
            pStr = pNext;
        }
    }

    cd_free(pCtx->pErrorText);
    cd_free(pCtx);
}

/* Evaluates a nested program (used by includeScript) without touching the
 * error state of the enclosing evaluation.                                 */
int js_eval_nested(JSCtx *pCtx, const char *pSource, const char *pName)
{
    char *pError = NULL;
    JSNode *pProgram = js_parse_program(pCtx, pSource, pName, &pError);
    JSVal value;

    if (pProgram == NULL) {
        js_throw(pCtx, "%s", pError ? pError : "SyntaxError");
        cd_free(pError);

        return 0;
    }

    cdvec_push(&pCtx->vecPrograms, pProgram);

    value = js_run_program(pCtx, pProgram, pCtx->pGlobalScope, jsval_obj(jsobj_ref(pCtx->pGlobal)));
    js_release(pCtx, value);

    return pCtx->bException ? 0 : 1;
}

int js_eval(JSCtx *pCtx, const char *pSource, const char *pName, JSVal *pResult)
{
    char *pError = NULL;
    JSNode *pProgram = NULL;
    JSVal value;

    if (pResult) {
        *pResult = js_undefined();
    }

    js_clear_error(pCtx);

    pProgram = js_parse_program(pCtx, pSource, pName, &pError);

    if (pProgram == NULL) {
        cd_free(pCtx->pErrorText);
        pCtx->pErrorText = pError ? pError : cd_strdup("SyntaxError");
        pCtx->bException = 1;
        pCtx->exception = js_str(pCtx, pCtx->pErrorText);

        return 0;
    }

    cdvec_push(&pCtx->vecPrograms, pProgram);

    value = js_run_program(pCtx, pProgram, pCtx->pGlobalScope, jsval_obj(jsobj_ref(pCtx->pGlobal)));

    if (pCtx->bException) {
        js_release(pCtx, value);

        return 0;
    }

    if (pResult) {
        *pResult = value;
    } else {
        js_release(pCtx, value);
    }

    return 1;
}
