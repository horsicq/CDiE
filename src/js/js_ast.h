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

/* js_ast.h - abstract syntax tree shared by the parser and the interpreter. */

#ifndef JS_AST_H
#define JS_AST_H

#include "js_internal.h"

typedef enum {
    N_PROGRAM = 0,
    N_NUM,
    N_STR,
    N_REGEXP,
    N_IDENT,
    N_THIS,
    N_NULL,
    N_BOOL,
    N_ARRAY,
    N_OBJECT,
    N_PROP,      /* key/value pair inside an object literal, num = key size */
    N_FUNCTION,  /* a = params list, b = body block, str = name */
    N_CALL,      /* a = callee, list = arguments */
    N_NEW,       /* a = callee, list = arguments */
    N_MEMBER,    /* a = object, str = property name */
    N_INDEX,     /* a = object, b = index expression */
    N_UNARY,     /* op, a */
    N_UPDATE,    /* op (++/--), a, num = 1 for prefix */
    N_BINARY,    /* op, a, b */
    N_LOGICAL,   /* op (&& ||), a, b */
    N_ASSIGN,    /* op, a = target, b = value */
    N_COND,      /* a ? b : c */
    N_SEQ,       /* a, b */
    N_VAR,       /* list of N_VARDECL */
    N_VARDECL,   /* str = name, a = initialiser (optional) */
    N_BLOCK,     /* list of statements */
    N_IF,        /* a = test, b = then, c = else */
    N_FOR,       /* a = init, b = test, c = update, d = body */
    N_FORIN,     /* a = left (N_VAR or target expr), b = object, d = body */
    N_WHILE,     /* a = test, d = body */
    N_DOWHILE,   /* a = test, d = body */
    N_RETURN,    /* a = argument */
    N_BREAK,     /* str = label */
    N_CONTINUE,  /* str = label */
    N_THROW,     /* a = argument */
    N_TRY,       /* a = block, b = catch body, str = catch param, c = finally */
    N_SWITCH,    /* a = discriminant, list = N_CASE */
    N_CASE,      /* a = test (NULL for default), list = statements */
    N_LABELED,   /* str = label, a = statement */
    N_EMPTY,
    N_EXPRSTMT   /* a = expression */
} JSNodeType;

/* Operator identifiers used by N_UNARY/N_BINARY/N_ASSIGN/N_LOGICAL. */
typedef enum {
    OP_NONE = 0,
    OP_ADD,
    OP_SUB,
    OP_MUL,
    OP_DIV,
    OP_MOD,
    OP_LT,
    OP_GT,
    OP_LE,
    OP_GE,
    OP_EQ,
    OP_NE,
    OP_SEQ,
    OP_SNE,
    OP_AND,
    OP_OR,
    OP_XOR,
    OP_SHL,
    OP_SHR,
    OP_USHR,
    OP_LAND,
    OP_LOR,
    OP_NOT,
    OP_BNOT,
    OP_NEG,
    OP_POS,
    OP_TYPEOF,
    OP_VOID,
    OP_DELETE,
    OP_IN,
    OP_INSTANCEOF,
    OP_INC,
    OP_DEC,
    OP_ASSIGN
} JSOp;

struct JSNode {
    JSNodeType type;
    JSOp op;
    int nLine;
    /* Length and hash of pStr, filled in on the first identifier lookup:
     * variable access would otherwise rehash the same name endlessly.     */
    cd_u8 bStrKey;
    size_t nStrSize;
    cd_u32 nStrHash;
    double nNum;
    char *pStr;
    char *pStr2;
    JSNode *a;
    JSNode *b;
    JSNode *c;
    JSNode *d;
    JSNode **ppList;
    size_t nList;
    /* Function bodies cache their declared variable names for hoisting. */
    char **ppVarNames;
    size_t nVarNames;
};

#endif /* JS_AST_H */
