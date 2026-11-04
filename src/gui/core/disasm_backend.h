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
#ifndef DISASM_BACKEND_H
#define DISASM_BACKEND_H
#include <stdint.h>
#include <stddef.h>

typedef int (*DasmRead)(void* user, uint64_t off, void* buf, size_t len);
typedef enum { DASM_ARCH_NONE = 0, DASM_ARCH_X86_32, DASM_ARCH_X86_64 } DasmArch;

typedef struct {
	uint64_t va;            /* virtual address shown in the left column */
	uint64_t file_off;
	uint64_t target;        /* branch target, 0 if none */
	unsigned len;           /* bytes consumed, always >= 1 */
	int      valid;         /* 0 = undecodable, shown as "db" */
	uint8_t  bytes[16];
	char     mnemonic[32];
	char     operands[128];
} DasmLine;

/* Parses the PE headers through rd. Returns 0 if the file is not a PE the
 * build can disassemble (unsupported machine, or cdisasm lacks that arch). */
int       dasm_open(DasmRead rd, void* user, uint64_t file_size);
void      dasm_close(void);
DasmArch  dasm_arch(void);
uint64_t  dasm_entry_offset(void);          /* file offset of the entry point */
int       dasm_decode_at(uint64_t file_off, DasmLine* out);
int       dasm_off_to_va(uint64_t off, uint64_t* va);
int       dasm_va_to_off(uint64_t va, uint64_t* off);

int dasm_section_of(uint64_t off, uint64_t* lo, uint64_t* hi);

#endif