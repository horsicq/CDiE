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

#include "xdisasm.h"

typedef struct {
    XBFile *pFile;
    cd_i64 nStart;
    cd_i64 nPos;
    int bOperand16;
    int bAddress16;
    int bRexW;
    int nBits;
} DisasmState;

static cd_u8 fetch(DisasmState *pState)
{
    cd_u8 nByte = xb_u8(pState->pFile, pState->nPos);

    pState->nPos++;

    return nByte;
}

static cd_u8 peek(DisasmState *pState)
{
    return xb_u8(pState->pFile, pState->nPos);
}

/* Consumes ModRM plus SIB and displacement. */
static void modrm(DisasmState *pState)
{
    cd_u8 nModRM = fetch(pState);
    int nMod = (nModRM >> 6) & 3;
    int nRM = nModRM & 7;

    if (pState->bAddress16 && (pState->nBits == 16 || pState->nBits == 32)) {
        if ((nMod == 0) && (nRM == 6)) {
            pState->nPos += 2;
        } else if (nMod == 1) {
            pState->nPos += 1;
        } else if (nMod == 2) {
            pState->nPos += 2;
        }

        return;
    }

    if (nMod == 3) {
        return;
    }

    if (nRM == 4) {
        cd_u8 nSib = fetch(pState);

        if ((nMod == 0) && ((nSib & 7) == 5)) {
            pState->nPos += 4;

            return;
        }
    } else if ((nMod == 0) && (nRM == 5)) {
        pState->nPos += 4;

        return;
    }

    if (nMod == 1) {
        pState->nPos += 1;
    } else if (nMod == 2) {
        pState->nPos += 4;
    }
}

static int operand_size(DisasmState *pState)
{
    if (pState->bRexW) {
        return 8;
    }

    if (pState->bOperand16) {
        return 2;
    }

    return 4;
}

/* x86 tops out at 15 bytes per instruction; a longer decode means the byte run
 * is not an instruction at all. The Capstone-backed reference falls back to a
 * one-byte "db" there, so step a single byte and report no mnemonic. */
#define XDISASM_MAX_LENGTH 15

static void disasm_finish(const DisasmState *pState, XDisasmResult *pResult)
{
    cd_i64 nLength = pState->nPos - pState->nStart;

    if (nLength > XDISASM_MAX_LENGTH) {
        x_memset(pResult, 0, sizeof(*pResult));
        nLength = 1;
    }

    if (nLength <= 0) {
        nLength = 1;
    }

    pResult->nSize = (int)nLength;
}

static void set_mnemonic(XDisasmResult *pResult, const char *pText)
{
    size_t nSize = x_strlen(pText);

    if (nSize >= sizeof(pResult->sMnemonic)) {
        nSize = sizeof(pResult->sMnemonic) - 1;
    }

    x_memcpy(pResult->sMnemonic, pText, nSize);
    pResult->sMnemonic[nSize] = 0;
}

static const char *g_pGroup1[8] = {"ADD", "OR", "ADC", "SBB", "AND", "SUB", "XOR", "CMP"};
static const char *g_pShiftGroup[8] = {"ROL", "ROR", "RCL", "RCR", "SHL", "SHR", "SHL", "SAR"};
static const char *g_pCondition[16] = {"O", "NO", "B", "AE", "E", "NE", "BE", "A", "S", "NS", "P", "NP", "L", "GE", "LE", "G"};

XDisasmResult xdisasm(XBFile *pFile, cd_i64 nOffset, int nBits)
{
    DisasmState state;
    XDisasmResult result;
    cd_u8 nOpcode = 0;
    int bDone = 0;

    x_memset(&result, 0, sizeof(result));

    if ((nOffset < 0) || (nOffset >= pFile->nSize)) {
        result.nSize = 1;

        return result;
    }

    state.pFile = pFile;
    state.nStart = nOffset;
    state.nPos = nOffset;
    state.bOperand16 = (nBits == 16) ? 1 : 0;
    state.bAddress16 = (nBits == 16) ? 1 : 0;
    state.bRexW = 0;
    state.nBits = nBits;

    /* Legacy prefixes. */
    while (!bDone) {
        cd_u8 nByte = 0;

        /* Never walk past the 15-byte instruction limit: a long run of prefix
         * bytes is not an instruction, and scanning it all is wasted work. */
        if ((state.nPos - state.nStart) >= XDISASM_MAX_LENGTH) {
            break;
        }

        nByte = peek(&state);

        switch (nByte) {
            case 0xF0:
            case 0xF2:
            case 0xF3:
            case 0x2E:
            case 0x36:
            case 0x3E:
            case 0x26:
            case 0x64:
            case 0x65:
                state.nPos++;
                break;
            case 0x66:
                state.bOperand16 = !state.bOperand16;
                state.nPos++;
                break;
            case 0x67:
                state.bAddress16 = !state.bAddress16;
                state.nPos++;
                break;
            default: bDone = 1; break;
        }
    }

    /* REX prefix (64 bit only). */
    if (nBits == 64) {
        cd_u8 nByte = peek(&state);

        if ((nByte >= 0x40) && (nByte <= 0x4F)) {
            state.bRexW = (nByte & 8) ? 1 : 0;
            state.nPos++;
        }
    }

    nOpcode = fetch(&state);

    if (nOpcode == 0x0F) {
        cd_u8 nSecond = fetch(&state);

        if ((nSecond >= 0x80) && (nSecond <= 0x8F)) {
            char sBuf[16];

            x_snprintf(sBuf, sizeof(sBuf), "J%s", g_pCondition[nSecond & 0x0F]);
            set_mnemonic(&result, sBuf);
            state.nPos += (state.bOperand16 ? 2 : 4);
        } else if ((nSecond >= 0x90) && (nSecond <= 0x9F)) {
            char sBuf[16];

            x_snprintf(sBuf, sizeof(sBuf), "SET%s", g_pCondition[nSecond & 0x0F]);
            set_mnemonic(&result, sBuf);
            modrm(&state);
        } else if ((nSecond >= 0x40) && (nSecond <= 0x4F)) {
            char sBuf[16];

            x_snprintf(sBuf, sizeof(sBuf), "CMOV%s", g_pCondition[nSecond & 0x0F]);
            set_mnemonic(&result, sBuf);
            modrm(&state);
        } else if ((nSecond >= 0xC8) && (nSecond <= 0xCF)) {
            set_mnemonic(&result, "BSWAP");
        } else {
            switch (nSecond) {
                case 0xA3: set_mnemonic(&result, "BT"); modrm(&state); break;
                case 0xAB: set_mnemonic(&result, "BTS"); modrm(&state); break;
                case 0xB3: set_mnemonic(&result, "BTR"); modrm(&state); break;
                case 0xBB: set_mnemonic(&result, "BTC"); modrm(&state); break;
                case 0xBC: set_mnemonic(&result, "BSF"); modrm(&state); break;
                case 0xBD: set_mnemonic(&result, "BSR"); modrm(&state); break;
                case 0xBA: set_mnemonic(&result, "BT"); modrm(&state); state.nPos += 1; break;
                case 0xB6:
                case 0xB7: set_mnemonic(&result, "MOVZX"); modrm(&state); break;
                case 0xBE:
                case 0xBF: set_mnemonic(&result, "MOVSX"); modrm(&state); break;
                case 0xAF: set_mnemonic(&result, "IMUL"); modrm(&state); break;
                case 0xA2: set_mnemonic(&result, "CPUID"); break;
                case 0x31: set_mnemonic(&result, "RDTSC"); break;
                case 0x05: set_mnemonic(&result, "SYSCALL"); break;
                case 0x0B: set_mnemonic(&result, "UD2"); break;
                case 0x1F: set_mnemonic(&result, "NOP"); modrm(&state); break;
                case 0xC0:
                case 0xC1: set_mnemonic(&result, "XADD"); modrm(&state); break;
                case 0xB0:
                case 0xB1: set_mnemonic(&result, "CMPXCHG"); modrm(&state); break;
                case 0xA0: set_mnemonic(&result, "PUSH"); break;
                case 0xA1: set_mnemonic(&result, "POP"); break;
                case 0xA8: set_mnemonic(&result, "PUSH"); break;
                case 0xA9: set_mnemonic(&result, "POP"); break;
                default:
                    /* Unknown two byte opcode: assume ModRM follows. */
                    modrm(&state);
                    break;
            }
        }

        disasm_finish(&state, &result);

        return result;
    }

    if (nOpcode < 0x40) {
        int nLow = nOpcode & 7;
        int nGroup = (nOpcode >> 3) & 7;

        if (nLow <= 3) {
            set_mnemonic(&result, g_pGroup1[nGroup]);
            modrm(&state);
            disasm_finish(&state, &result);

            return result;
        }

        if (nLow == 4) {
            set_mnemonic(&result, g_pGroup1[nGroup]);
            state.nPos += 1;
            disasm_finish(&state, &result);

            return result;
        }

        if (nLow == 5) {
            set_mnemonic(&result, g_pGroup1[nGroup]);
            state.nPos += (state.bOperand16 ? 2 : 4);
            disasm_finish(&state, &result);

            return result;
        }
    }

    if ((nOpcode >= 0x50) && (nOpcode <= 0x57)) {
        set_mnemonic(&result, "PUSH");
        disasm_finish(&state, &result);

        return result;
    }

    if ((nOpcode >= 0x58) && (nOpcode <= 0x5F)) {
        set_mnemonic(&result, "POP");
        disasm_finish(&state, &result);

        return result;
    }

    if ((nOpcode >= 0x70) && (nOpcode <= 0x7F)) {
        char sBuf[16];

        x_snprintf(sBuf, sizeof(sBuf), "J%s", g_pCondition[nOpcode & 0x0F]);
        set_mnemonic(&result, sBuf);
        state.nPos += 1;
        disasm_finish(&state, &result);

        return result;
    }

    if ((nOpcode >= 0x90) && (nOpcode <= 0x97)) {
        set_mnemonic(&result, (nOpcode == 0x90) ? "NOP" : "XCHG");
        disasm_finish(&state, &result);

        return result;
    }

    if ((nOpcode >= 0xB0) && (nOpcode <= 0xB7)) {
        set_mnemonic(&result, "MOV");
        state.nPos += 1;
        disasm_finish(&state, &result);

        return result;
    }

    if ((nOpcode >= 0xB8) && (nOpcode <= 0xBF)) {
        set_mnemonic(&result, "MOV");
        state.nPos += (state.bRexW ? 8 : (state.bOperand16 ? 2 : 4));
        disasm_finish(&state, &result);

        return result;
    }

    switch (nOpcode) {
        case 0x80: set_mnemonic(&result, "CMP"); modrm(&state); state.nPos += 1; break;
        case 0x81: set_mnemonic(&result, "CMP"); modrm(&state); state.nPos += (state.bOperand16 ? 2 : 4); break;
        case 0x83: set_mnemonic(&result, "CMP"); modrm(&state); state.nPos += 1; break;
        case 0x84:
        case 0x85: set_mnemonic(&result, "TEST"); modrm(&state); break;
        case 0x86:
        case 0x87: set_mnemonic(&result, "XCHG"); modrm(&state); break;
        case 0x88:
        case 0x89:
        case 0x8A:
        case 0x8B: set_mnemonic(&result, "MOV"); modrm(&state); break;
        case 0x8D: set_mnemonic(&result, "LEA"); modrm(&state); break;
        case 0x8F: set_mnemonic(&result, "POP"); modrm(&state); break;
        case 0x98: set_mnemonic(&result, "CWDE"); break;
        case 0x99: set_mnemonic(&result, "CDQ"); break;
        case 0x9C: set_mnemonic(&result, "PUSHFD"); break;
        case 0x9D: set_mnemonic(&result, "POPFD"); break;
        case 0x60: set_mnemonic(&result, "PUSHAD"); break;
        case 0x61: set_mnemonic(&result, "POPAD"); break;
        case 0x68: set_mnemonic(&result, "PUSH"); state.nPos += (state.bOperand16 ? 2 : 4); break;
        case 0x6A: set_mnemonic(&result, "PUSH"); state.nPos += 1; break;
        case 0x69: set_mnemonic(&result, "IMUL"); modrm(&state); state.nPos += (state.bOperand16 ? 2 : 4); break;
        case 0x6B: set_mnemonic(&result, "IMUL"); modrm(&state); state.nPos += 1; break;
        case 0xA8: set_mnemonic(&result, "TEST"); state.nPos += 1; break;
        case 0xA9: set_mnemonic(&result, "TEST"); state.nPos += (state.bOperand16 ? 2 : 4); break;
        case 0xC0:
        case 0xC1: {
            cd_u8 nModRM = peek(&state);

            set_mnemonic(&result, g_pShiftGroup[(nModRM >> 3) & 7]);
            modrm(&state);
            state.nPos += 1;
            break;
        }
        case 0xD0:
        case 0xD1:
        case 0xD2:
        case 0xD3: {
            cd_u8 nModRM = peek(&state);

            set_mnemonic(&result, g_pShiftGroup[(nModRM >> 3) & 7]);
            modrm(&state);
            break;
        }
        case 0xC2: set_mnemonic(&result, "RET"); state.nPos += 2; break;
        case 0xC3: set_mnemonic(&result, "RET"); break;
        case 0xC6: set_mnemonic(&result, "MOV"); modrm(&state); state.nPos += 1; break;
        case 0xC7: set_mnemonic(&result, "MOV"); modrm(&state); state.nPos += (state.bOperand16 ? 2 : 4); break;
        case 0xC9: set_mnemonic(&result, "LEAVE"); break;
        case 0xCC: set_mnemonic(&result, "INT3"); break;
        case 0xCD: set_mnemonic(&result, "INT"); state.nPos += 1; break;
        case 0xE8: set_mnemonic(&result, "CALL"); state.nPos += (state.bOperand16 ? 2 : 4); break;
        case 0xE9: set_mnemonic(&result, "JMP"); state.nPos += (state.bOperand16 ? 2 : 4); break;
        case 0xEB: set_mnemonic(&result, "JMP"); state.nPos += 1; break;
        case 0xF4: set_mnemonic(&result, "HLT"); break;
        case 0xF5: set_mnemonic(&result, "CMC"); break;
        case 0xF6: {
            cd_u8 nModRM = peek(&state);
            int nGroup = (nModRM >> 3) & 7;
            static const char *pNames[8] = {"TEST", "TEST", "NOT", "NEG", "MUL", "IMUL", "DIV", "IDIV"};

            set_mnemonic(&result, pNames[nGroup]);
            modrm(&state);

            if (nGroup <= 1) {
                state.nPos += 1;
            }

            break;
        }
        case 0xF7: {
            cd_u8 nModRM = peek(&state);
            int nGroup = (nModRM >> 3) & 7;
            static const char *pNames[8] = {"TEST", "TEST", "NOT", "NEG", "MUL", "IMUL", "DIV", "IDIV"};

            set_mnemonic(&result, pNames[nGroup]);
            modrm(&state);

            if (nGroup <= 1) {
                state.nPos += (state.bOperand16 ? 2 : 4);
            }

            break;
        }
        case 0xFE: {
            cd_u8 nModRM = peek(&state);

            set_mnemonic(&result, (((nModRM >> 3) & 7) == 0) ? "INC" : "DEC");
            modrm(&state);
            break;
        }
        case 0xFF: {
            cd_u8 nModRM = peek(&state);
            int nGroup = (nModRM >> 3) & 7;
            static const char *pNames[8] = {"INC", "DEC", "CALL", "CALL", "JMP", "JMP", "PUSH", "?"};

            set_mnemonic(&result, pNames[nGroup]);
            modrm(&state);
            break;
        }
        case 0xA0:
        case 0xA1:
        case 0xA2:
        case 0xA3:
            set_mnemonic(&result, "MOV");
            state.nPos += ((nBits == 64) ? 8 : (state.bAddress16 ? 2 : 4));
            break;
        default:
            /* Fall back to a single byte instruction. */
            (void)operand_size(&state);
            break;
    }

    disasm_finish(&state, &result);

    return result;
}
