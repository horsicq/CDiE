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

/* js_lex.c - tokeniser. The whole source is converted into a token array up
 * front; regular-expression literals are disambiguated from division using
 * the classic "previous significant token" heuristic.                       */

#include "js_lex.h"


static const char *g_pKeywords[] = {"break",  "case",   "catch", "continue", "default",    "delete", "do",     "else",  "finally", "for",
                                    "function", "if",   "in",    "instanceof", "new",      "return", "switch", "this",  "throw",   "try",
                                    "typeof", "var",    "void",  "while",    "with",       "null",   "true",   "false", "const",   "let",
                                    NULL};

static const JSTokType g_keywordTypes[] = {T_BREAK,  T_CASE,   T_CATCH, T_CONTINUE,   T_DEFAULT, T_DELETE, T_DO,     T_ELSE, T_FINALLY, T_FOR,
                                           T_FUNCTION, T_IF,   T_IN,    T_INSTANCEOF, T_NEW,     T_RETURN, T_SWITCH, T_THIS, T_THROW,   T_TRY,
                                           T_TYPEOF, T_VAR,    T_VOID,  T_WHILE,      T_WITH,    T_NULL,   T_TRUE,   T_FALSE, T_VAR,    T_VAR};

static int is_ident_start(int nChar)
{
    return (((nChar >= 'a') && (nChar <= 'z')) || ((nChar >= 'A') && (nChar <= 'Z')) || (nChar == '_') || (nChar == '$') || (nChar >= 0x80)) ? 1 : 0;
}

static int is_ident_part(int nChar)
{
    return (is_ident_start(nChar) || ((nChar >= '0') && (nChar <= '9'))) ? 1 : 0;
}

static int is_digit(int nChar)
{
    return ((nChar >= '0') && (nChar <= '9')) ? 1 : 0;
}

static int is_hex(int nChar)
{
    return (is_digit(nChar) || ((nChar >= 'a') && (nChar <= 'f')) || ((nChar >= 'A') && (nChar <= 'F'))) ? 1 : 0;
}

static int hex_value(int nChar)
{
    if (is_digit(nChar)) {
        return nChar - '0';
    }

    if ((nChar >= 'a') && (nChar <= 'f')) {
        return nChar - 'a' + 10;
    }

    return nChar - 'A' + 10;
}

static void tok_push(JSLexer *pLexer, JSToken token)
{
    if (pLexer->nCount + 1 > pLexer->nCapacity) {
        size_t nNew = pLexer->nCapacity ? pLexer->nCapacity * 2 : 256;

        pLexer->pTokens = (JSToken *)cd_realloc(pLexer->pTokens, nNew * sizeof(JSToken));
        pLexer->nCapacity = nNew;
    }

    pLexer->pTokens[pLexer->nCount++] = token;
}

/* Appends a UTF-8 encoding of a code point (used for \uXXXX escapes). */
static void append_code_point(CDBuf *pBuf, unsigned int nCode)
{
    if (nCode < 0x80) {
        cdbuf_append_ch(pBuf, (char)nCode);
    } else if (nCode < 0x800) {
        cdbuf_append_ch(pBuf, (char)(0xC0 | (nCode >> 6)));
        cdbuf_append_ch(pBuf, (char)(0x80 | (nCode & 0x3F)));
    } else if (nCode < 0x10000) {
        cdbuf_append_ch(pBuf, (char)(0xE0 | (nCode >> 12)));
        cdbuf_append_ch(pBuf, (char)(0x80 | ((nCode >> 6) & 0x3F)));
        cdbuf_append_ch(pBuf, (char)(0x80 | (nCode & 0x3F)));
    } else {
        cdbuf_append_ch(pBuf, (char)(0xF0 | (nCode >> 18)));
        cdbuf_append_ch(pBuf, (char)(0x80 | ((nCode >> 12) & 0x3F)));
        cdbuf_append_ch(pBuf, (char)(0x80 | ((nCode >> 6) & 0x3F)));
        cdbuf_append_ch(pBuf, (char)(0x80 | (nCode & 0x3F)));
    }
}

/* Reports a lexical error in the same shape the parser uses. */
static void lex_error(char **ppError, const char *pMessage, int nLine)
{
    if (ppError) {
        char sBuf[128];

        x_snprintf(sBuf, sizeof(sBuf), "SyntaxError: %s at line %d", pMessage, nLine);
        *ppError = cd_strdup(sBuf);
    }
}

static int prev_allows_regexp(JSLexer *pLexer)
{
    JSToken *pPrev = NULL;

    if (pLexer->nCount == 0) {
        return 1;
    }

    pPrev = &pLexer->pTokens[pLexer->nCount - 1];

    switch (pPrev->type) {
        case T_IDENT:
        case T_NUMBER:
        case T_STRING:
        case T_REGEXP:
        case T_RPAREN:
        case T_RBRACKET:
        case T_RBRACE:
        case T_THIS:
        case T_TRUE:
        case T_FALSE:
        case T_NULL:
        case T_INC:
        case T_DEC: return 0;
        default: return 1;
    }
}

int js_lex_run(JSLexer *pLexer, const char *pSource, char **ppError)
{
    const char *p = pSource;
    int nLine = 1;
    int bNewLineBefore = 0;

    x_memset(pLexer, 0, sizeof(*pLexer));
    pLexer->pSource = pSource;

    /* ECMAScript treats U+FEFF as whitespace; skipping it here keeps the
     * first identifier of a BOM-prefixed script from absorbing it.      */
    if (((unsigned char)p[0] == 0xEF) && ((unsigned char)p[1] == 0xBB) && ((unsigned char)p[2] == 0xBF)) {
        p += 3;
    }

    for (;;) {
        JSToken token;

        /* Skip whitespace and comments. */
        for (;;) {
            if (*p == 0) {
                break;
            }

            if ((*p == '\n')) {
                nLine++;
                bNewLineBefore = 1;
                p++;
            } else if ((*p == ' ') || (*p == '\t') || (*p == '\r') || (*p == '\f') || (*p == '\v')) {
                p++;
            } else if ((p[0] == '/') && (p[1] == '/')) {
                while (*p && (*p != '\n')) {
                    p++;
                }
            } else if ((p[0] == '/') && (p[1] == '*')) {
                p += 2;

                while (*p && !((p[0] == '*') && (p[1] == '/'))) {
                    if (*p == '\n') {
                        nLine++;
                        bNewLineBefore = 1;
                    }

                    p++;
                }

                if (*p) {
                    p += 2;
                } else {
                    lex_error(ppError, "unterminated comment", nLine);

                    return 0;
                }
            } else {
                break;
            }
        }

        x_memset(&token, 0, sizeof(token));
        token.nLine = nLine;
        token.bNewLineBefore = bNewLineBefore;
        bNewLineBefore = 0;

        if (*p == 0) {
            token.type = T_EOF;
            tok_push(pLexer, token);
            break;
        }

        /* Identifier or keyword. */
        if (is_ident_start((unsigned char)*p)) {
            const char *pStart = p;
            size_t nSize = 0;
            int i = 0;

            while (is_ident_part((unsigned char)*p)) {
                p++;
            }

            nSize = (size_t)(p - pStart);
            token.type = T_IDENT;
            token.pText = cd_strndup(pStart, nSize);

            for (i = 0; g_pKeywords[i]; i++) {
                if ((x_strlen(g_pKeywords[i]) == nSize) && (x_memcmp(g_pKeywords[i], pStart, nSize) == 0)) {
                    token.type = g_keywordTypes[i];
                    break;
                }
            }

            tok_push(pLexer, token);
            continue;
        }

        /* Number. */
        if (is_digit((unsigned char)*p) || ((*p == '.') && is_digit((unsigned char)p[1]))) {
            const char *pStart = p;

            if ((p[0] == '0') && ((p[1] == 'x') || (p[1] == 'X'))) {
                double nValue = 0;
                int nDigits = 0;

                p += 2;

                while (is_hex((unsigned char)*p)) {
                    nValue = nValue * 16 + hex_value((unsigned char)*p);
                    nDigits++;
                    p++;
                }

                if (nDigits == 0) {
                    lex_error(ppError, "invalid numeric literal", nLine);

                    return 0;
                }

                token.type = T_NUMBER;
                token.nNum = nValue;
                tok_push(pLexer, token);
                continue;
            }

            if ((p[0] == '0') && ((p[1] == 'b') || (p[1] == 'B'))) {
                double nValue = 0;
                int nDigits = 0;

                p += 2;

                while ((*p == '0') || (*p == '1')) {
                    nValue = nValue * 2 + (*p - '0');
                    nDigits++;
                    p++;
                }

                if (nDigits == 0) {
                    lex_error(ppError, "invalid numeric literal", nLine);

                    return 0;
                }

                token.type = T_NUMBER;
                token.nNum = nValue;
                tok_push(pLexer, token);
                continue;
            }

            if ((p[0] == '0') && ((p[1] == 'o') || (p[1] == 'O'))) {
                double nValue = 0;
                int nDigits = 0;

                p += 2;

                while ((*p >= '0') && (*p <= '7')) {
                    nValue = nValue * 8 + (*p - '0');
                    nDigits++;
                    p++;
                }

                if (nDigits == 0) {
                    lex_error(ppError, "invalid numeric literal", nLine);

                    return 0;
                }

                token.type = T_NUMBER;
                token.nNum = nValue;
                tok_push(pLexer, token);
                continue;
            }

            while (is_digit((unsigned char)*p)) {
                p++;
            }

            if (*p == '.') {
                p++;

                while (is_digit((unsigned char)*p)) {
                    p++;
                }
            }

            if ((*p == 'e') || (*p == 'E')) {
                const char *pSave = p;

                p++;

                if ((*p == '+') || (*p == '-')) {
                    p++;
                }

                if (is_digit((unsigned char)*p)) {
                    while (is_digit((unsigned char)*p)) {
                        p++;
                    }
                } else {
                    p = pSave;
                }
            }

            {
                char *pCopy = cd_strndup(pStart, (size_t)(p - pStart));

                token.type = T_NUMBER;
                token.nNum = x_strtod(pCopy, NULL);
                cd_free(pCopy);
            }

            tok_push(pLexer, token);
            continue;
        }

        /* String literal. */
        if ((*p == '"') || (*p == '\'')) {
            char nQuote = *p;
            int nStartLine = nLine;
            CDBuf buf;

            cdbuf_init(&buf);
            p++;

            while (*p && (*p != nQuote)) {
                if (*p == '\\') {
                    p++;

                    switch (*p) {
                        case 'n': cdbuf_append_ch(&buf, '\n'); p++; break;
                        case 't': cdbuf_append_ch(&buf, '\t'); p++; break;
                        case 'r': cdbuf_append_ch(&buf, '\r'); p++; break;
                        case 'b': cdbuf_append_ch(&buf, '\b'); p++; break;
                        case 'f': cdbuf_append_ch(&buf, '\f'); p++; break;
                        case 'v': cdbuf_append_ch(&buf, '\v'); p++; break;
                        case '0':
                            if (!is_digit((unsigned char)p[1])) {
                                cdbuf_append_ch(&buf, '\0');
                                p++;
                            } else {
                                cdbuf_append_ch(&buf, *p);
                                p++;
                            }
                            break;
                        case 'x':
                            if (is_hex((unsigned char)p[1]) && is_hex((unsigned char)p[2])) {
                                cdbuf_append_ch(&buf, (char)((hex_value((unsigned char)p[1]) << 4) | hex_value((unsigned char)p[2])));
                                p += 3;
                            } else {
                                cdbuf_append_ch(&buf, 'x');
                                p++;
                            }
                            break;
                        case 'u':
                            if (is_hex((unsigned char)p[1]) && is_hex((unsigned char)p[2]) && is_hex((unsigned char)p[3]) && is_hex((unsigned char)p[4])) {
                                unsigned int nCode = (unsigned int)((hex_value((unsigned char)p[1]) << 12) | (hex_value((unsigned char)p[2]) << 8) |
                                                                    (hex_value((unsigned char)p[3]) << 4) | hex_value((unsigned char)p[4]));

                                /* A high surrogate followed by \uDC00-\uDFFF is one code
                                 * point; encoding the halves apart would give CESU-8.   */
                                if ((nCode >= 0xD800) && (nCode <= 0xDBFF) && (p[5] == '\\') && (p[6] == 'u') && is_hex((unsigned char)p[7]) &&
                                    is_hex((unsigned char)p[8]) && is_hex((unsigned char)p[9]) && is_hex((unsigned char)p[10])) {
                                    unsigned int nLow = (unsigned int)((hex_value((unsigned char)p[7]) << 12) | (hex_value((unsigned char)p[8]) << 8) |
                                                                       (hex_value((unsigned char)p[9]) << 4) | hex_value((unsigned char)p[10]));

                                    if ((nLow >= 0xDC00) && (nLow <= 0xDFFF)) {
                                        nCode = 0x10000 + ((nCode - 0xD800) << 10) + (nLow - 0xDC00);
                                        p += 6;
                                    }
                                }

                                append_code_point(&buf, nCode);
                                p += 5;
                            } else {
                                cdbuf_append_ch(&buf, 'u');
                                p++;
                            }
                            break;
                        case '\n':
                            nLine++;
                            p++;
                            break;
                        case '\r':
                            /* CRLF is a single line terminator, so the continuation
                             * swallows both bytes.                                 */
                            p++;

                            if (*p == '\n') {
                                p++;
                            }

                            nLine++;
                            break;
                        case 0: break;
                        default:
                            cdbuf_append_ch(&buf, *p);
                            p++;
                            break;
                    }
                } else {
                    if (*p == '\n') {
                        nLine++;
                    }

                    cdbuf_append_ch(&buf, *p);
                    p++;
                }
            }

            if (*p == nQuote) {
                p++;
            } else {
                cdbuf_free(&buf);
                lex_error(ppError, "unterminated string literal", nStartLine);

                return 0;
            }

            token.type = T_STRING;
            token.nTextSize = buf.nSize;
            token.pText = cdbuf_detach(&buf, NULL);
            tok_push(pLexer, token);
            continue;
        }

        /* Regular expression literal. */
        if ((*p == '/') && prev_allows_regexp(pLexer)) {
            const char *pStart = p + 1;
            const char *q = pStart;
            int bInClass = 0;

            while (*q) {
                if (*q == '\\') {
                    if (q[1] == 0) {
                        break;
                    }

                    q += 2;
                    continue;
                }

                if (*q == '[') {
                    bInClass = 1;
                } else if (*q == ']') {
                    bInClass = 0;
                } else if ((*q == '/') && (!bInClass)) {
                    break;
                } else if (*q == '\n') {
                    break;
                }

                q++;
            }

            if (*q == '/') {
                const char *pFlags = q + 1;
                const char *pFlagsEnd = pFlags;

                while (is_ident_part((unsigned char)*pFlagsEnd)) {
                    pFlagsEnd++;
                }

                token.type = T_REGEXP;
                token.pText = cd_strndup(pStart, (size_t)(q - pStart));
                token.pText2 = cd_strndup(pFlags, (size_t)(pFlagsEnd - pFlags));
                tok_push(pLexer, token);
                p = pFlagsEnd;
                continue;
            }
        }

        /* Punctuators, longest match first. There is deliberately no '**' or
         * '**=': QJSEngine rejects exponentiation, so lexing them as two
         * tokens keeps the SyntaxError the reference reports.               */
        {
            struct {
                const char *pText;
                JSTokType type;
            } punctuators[] = {
                {">>>=", T_USHR_ASSIGN}, {"===", T_SEQ},        {"!==", T_SNE},        {"<<=", T_SHL_ASSIGN}, {">>=", T_SHR_ASSIGN},
                {">>>", T_USHR},         {"...", T_ELLIPSIS},   {"==", T_EQ},          {"!=", T_NE},          {"<=", T_LE},
                {">=", T_GE},            {"&&", T_LAND},        {"||", T_LOR},         {"++", T_INC},         {"--", T_DEC},
                {"<<", T_SHL},           {">>", T_SHR},         {"+=", T_ADD_ASSIGN},  {"-=", T_SUB_ASSIGN},  {"*=", T_MUL_ASSIGN},
                {"/=", T_DIV_ASSIGN},    {"%=", T_MOD_ASSIGN},  {"&=", T_AND_ASSIGN},  {"|=", T_OR_ASSIGN},   {"^=", T_XOR_ASSIGN},
                {"=>", T_ARROW},         {"{", T_LBRACE},       {"}", T_RBRACE},       {"(", T_LPAREN},       {")", T_RPAREN},
                {"[", T_LBRACKET},       {"]", T_RBRACKET},     {";", T_SEMI},         {",", T_COMMA},        {"<", T_LT},
                {">", T_GT},             {"+", T_ADD},          {"-", T_SUB},          {"*", T_MUL},          {"/", T_DIV},
                {"%", T_MOD},            {"&", T_AND},          {"|", T_OR},           {"^", T_XOR},          {"!", T_NOT},
                {"~", T_BNOT},           {"?", T_QUESTION},     {":", T_COLON},        {"=", T_ASSIGN},       {".", T_DOT},
                {NULL, T_EOF}};
            int i = 0;
            int bFound = 0;

            for (i = 0; punctuators[i].pText; i++) {
                size_t nSize = x_strlen(punctuators[i].pText);

                if (x_strncmp(p, punctuators[i].pText, nSize) == 0) {
                    token.type = punctuators[i].type;
                    tok_push(pLexer, token);
                    p += nSize;
                    bFound = 1;
                    break;
                }
            }

            if (bFound) {
                continue;
            }
        }

        if (ppError) {
            char sBuf[128];

            x_snprintf(sBuf, sizeof(sBuf), "SyntaxError: unexpected character '%c' at line %d", *p, nLine);
            *ppError = cd_strdup(sBuf);
        }

        return 0;
    }

    return 1;
}

void js_lex_free(JSLexer *pLexer)
{
    size_t i = 0;

    for (i = 0; i < pLexer->nCount; i++) {
        cd_free(pLexer->pTokens[i].pText);
        cd_free(pLexer->pTokens[i].pText2);
    }

    cd_free(pLexer->pTokens);
    x_memset(pLexer, 0, sizeof(*pLexer));
}
