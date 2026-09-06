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

/* js_internal.h - shared internals of the interpreter. */

#ifndef JS_INTERNAL_H
#define JS_INTERNAL_H

#include "js.h"

/* --------------------------------------------------------------- strings  */

/* Every string, object and scope is linked into an engine-wide list so that
 * js_free can reclaim whatever reference counting could not (see the note
 * above jsobj_unref). The lists are doubly linked so that an allocation
 * whose count drops to zero can unlink itself in constant time.           */
struct JSStr {
    cd_i32 nRef;
    size_t nSize;
    JSStr *pNextAll;
    JSStr *pPrevAll;
    char *pData;
};

JSStr *jsstr_new(JSCtx *pCtx, const char *pData, size_t nSize);
JSStr *jsstr_ref(JSStr *pStr);
void jsstr_unref(JSCtx *pCtx, JSStr *pStr);

/* -------------------------------------------------------------- property  */

typedef struct {
    char *pKey;
    size_t nKeySize; /* x_strlen(pKey), cached: the probe loop is very hot */
    cd_u32 nHash;
    JSVal value;
    cd_u8 bDeleted;
    cd_u8 bDontEnum;
} JSProp;

typedef struct {
    JSProp *pEntries;
    size_t nSize;
    size_t nCapacity;
    cd_i32 *pIndex; /* open addressing hash -> entry index, -1 = empty */
    size_t nIndexSize;
    size_t nLive;
} JSPropMap;

void jsprops_init(JSPropMap *pMap);
void jsprops_free(JSCtx *pCtx, JSPropMap *pMap);
JSProp *jsprops_find(JSPropMap *pMap, const char *pKey, size_t nKeySize);
/* Same lookup with a hash the caller already computed (prototype walks). */
JSProp *jsprops_find_hashed(JSPropMap *pMap, const char *pKey, size_t nKeySize, cd_u32 nHash);
JSProp *jsprops_put(JSCtx *pCtx, JSPropMap *pMap, const char *pKey, size_t nKeySize);
/* Stores value in an entry, taking its reference and releasing the old one. */
void jsprop_store(JSCtx *pCtx, JSProp *pProp, JSVal value);
int jsprops_del(JSCtx *pCtx, JSPropMap *pMap, const char *pKey);

/* --------------------------------------------------------------- objects  */

typedef enum {
    JCLASS_OBJECT = 0,
    JCLASS_ARRAY,
    JCLASS_FUNCTION,
    JCLASS_NATIVE,
    JCLASS_STRING,
    JCLASS_NUMBER,
    JCLASS_BOOLEAN,
    JCLASS_REGEXP,
    JCLASS_ERROR,
    JCLASS_ARGUMENTS,
    JCLASS_SCOPE
} JSClass;

typedef struct JSScope JSScope;
typedef struct JSRegExp JSRegExp;

struct JSObj {
    cd_i32 nRef;
    cd_i32 nMapRefs; /* how many property maps hold this object; see jsprop_store */
    JSClass cls;
    JSObj *pProto;
    JSObj *pNextAll;
    JSObj *pPrevAll;
    JSPropMap props;
    cd_u8 bExtensible;
    cd_u8 bSweeping;

    /* array */
    cd_i64 nArrayLen;

    /* function (script) */
    JSNode *pFnNode;
    JSScope *pScope;
    char *pFnName;

    /* function (native) */
    JSNativeFn nativeFn;
    void *pUser;
    int nNativeArgc;

    /* bound function */
    JSObj *pBoundTarget;
    JSVal boundThis;
    JSVal *pBoundArgs;
    int nBoundArgs;

    /* primitive wrapper */
    JSVal primitive;

    /* regexp */
    JSRegExp *pRegExp;
};

struct JSScope {
    cd_i32 nRef;
    JSObj *pVars;
    JSScope *pParent;
    JSScope *pNextAll;
    JSScope *pPrevAll;
};

/* ------------------------------------------------------------- context  */

typedef struct {
    JSNode **ppNodes;
    size_t nSize;
    size_t nCapacity;
} JSNodePool;

struct JSCtx {
    JSObj *pGlobal;
    JSScope *pGlobalScope;

    JSObj *pObjectProto;
    JSObj *pFunctionProto;
    JSObj *pArrayProto;
    JSObj *pStringProto;
    JSObj *pNumberProto;
    JSObj *pBooleanProto;
    JSObj *pRegExpProto;
    JSObj *pErrorProto;

    /* engine-wide allocation lists for the final sweep */
    JSStr *pAllStrings;
    JSObj *pAllObjects;
    JSScope *pAllScopes;
    CDVec vecPrograms; /* parsed programs (JSNode roots) */

    int bException;
    JSVal exception;
    char *pErrorText;

    int nCallDepth;
    int nMaxCallDepth;

    /* AST recursion depth of the evaluator, guarded like the call depth. */
    int nEvalDepth;
    int nMaxEvalDepth;

    /* Nesting of the eager free path; see jsobj_unref. */
    int nFreeDepth;

    void *pUser;

    /* Cached empty string. */
    JSStr *pEmptyStr;
};

/* --------------------------------------------------------------- helpers  */

JSObj *jsobj_new(JSCtx *pCtx, JSClass cls, JSObj *pProto);
JSObj *jsobj_ref(JSObj *pObj);
void jsobj_unref(JSCtx *pCtx, JSObj *pObj);

JSScope *jsscope_new(JSCtx *pCtx, JSScope *pParent);
JSScope *jsscope_ref(JSScope *pScope);
void jsscope_unref(JSCtx *pCtx, JSScope *pScope);

JSVal jsval_obj(JSObj *pObj);   /* takes ownership of one reference */
JSVal jsval_str(JSStr *pStr);   /* takes ownership of one reference */

/* Raw property access (no prototype chain). */
JSVal jsobj_get_own(JSCtx *pCtx, JSObj *pObj, const char *pKey, size_t nKeySize, int *pbFound);
/* Full lookup with prototype chain and special cases (length, indices...). */
JSVal jsobj_get(JSCtx *pCtx, JSObj *pObj, const char *pKey, size_t nKeySize);
void jsobj_put(JSCtx *pCtx, JSObj *pObj, const char *pKey, size_t nKeySize, JSVal value);
void jsobj_put_hidden(JSCtx *pCtx, JSObj *pObj, const char *pKey, JSVal value);
int jsobj_has(JSCtx *pCtx, JSObj *pObj, const char *pKey, size_t nKeySize);

/* Number/string helpers shared by the builtins. */
JSVal js_number_to_string(JSCtx *pCtx, double nValue, int nRadix);
/* Number::toString fast path for exact 32-bit integers; 0 = not applicable. */
int js_int_to_buf(char *pBuf, double nValue);
double js_string_to_number(const char *pData, size_t nSize);
int js_is_array_index(const char *pKey, size_t nKeySize, cd_i64 *pnIndex);
JSVal js_concat_str(JSCtx *pCtx, JSVal left, JSVal right);

/* Equality / comparison used by the interpreter and by Array.sort. */
int js_strict_equals(JSCtx *pCtx, JSVal left, JSVal right);
int js_loose_equals(JSCtx *pCtx, JSVal left, JSVal right);
JSVal js_to_primitive(JSCtx *pCtx, JSVal value, int bPreferString);

/* Object construction helpers. */
JSVal js_construct(JSCtx *pCtx, JSVal fn, int nArgc, JSVal *pArgv);

/* Builtin installation (js_builtins.c). */
void js_install_builtins(JSCtx *pCtx);
void js_install_regexp(JSCtx *pCtx);

/* Regular expressions (js_regexp.c). */
JSRegExp *jsregexp_compile(const char *pPattern, const char *pFlags, char **ppError);
void jsregexp_free(JSRegExp *pRegExp);
int jsregexp_ngroups(JSRegExp *pRegExp);
int jsregexp_global(JSRegExp *pRegExp);
int jsregexp_ignorecase(JSRegExp *pRegExp);
int jsregexp_multiline(JSRegExp *pRegExp);
const char *jsregexp_source(JSRegExp *pRegExp);
const char *jsregexp_flags(JSRegExp *pRegExp);
/* Executes at or after nStart. Returns 1 on match; pnCaps holds
 * 2*(ngroups+1) offsets (-1 when the group did not participate).           */
int jsregexp_exec(JSRegExp *pRegExp, const char *pText, size_t nTextSize, size_t nStart, cd_i32 *pnCaps);

JSVal js_new_regexp_val(JSCtx *pCtx, const char *pPattern, const char *pFlags);
JSRegExp *js_get_regexp(JSVal value);

/* Parser (js_parse.c). */
JSNode *js_parse_program(JSCtx *pCtx, const char *pSource, const char *pName, char **ppError);
void js_free_node(JSNode *pNode);

/* Interpreter (js_interp.c). */
JSVal js_run_program(JSCtx *pCtx, JSNode *pProgram, JSScope *pScope, JSVal thisVal);
JSVal js_call_function(JSCtx *pCtx, JSObj *pFn, JSVal thisVal, int nArgc, JSVal *pArgv, int bConstruct);

/* Sets the pending exception from a printf-style message. */
JSVal js_throw_value(JSCtx *pCtx, JSVal value);

#endif /* JS_INTERNAL_H */
