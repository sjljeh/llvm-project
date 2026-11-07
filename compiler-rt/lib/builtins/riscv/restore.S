//===-- restore.S - restore up to 12 callee-save registers ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Multiple entry points depending on number of registers to restore
//
//===----------------------------------------------------------------------===//

// All of the entry points are in the same section since we rely on many of
// them falling through into each other and don't want the linker to
// accidentally split them up, garbage collect, or reorder them.
//
// For the conventional ABIs, entry points are grouped up into 2s for rv64 and
// 4s for rv32 since this is the minimum grouping which will maintain the
// required 16-byte stack alignment.
//
// For the ilp32e/lp64e abis, entry points are grouped into 1s, since this is
// the minimum grouping which will maintain the required 4-byte stack alignment.

#include "../assembly.h"

TEXT_SECTION

// Like DEFINE_COMPILERRT_FUNCTION but without:
// - Section change (TEXT_SECTION)
// - Subsections via symbols (FILE_LEVEL_DIRECTIVE)
//
// This is needed because `__riscv_restore_<N>` could fallthrough to a later
// `__riscv_restore_<N>` tail.
#define DEFINE_COMPILERRT_OVERLAPPING_FUNCTION(name) \
  DEFINE_CODE_STATE \
  .globl FUNC_SYMBOL(SYMBOL_NAME(name)) SEPARATOR \
  SYMBOL_IS_FUNC(SYMBOL_NAME(name)) SEPARATOR \
  HIDDEN(SYMBOL_NAME(name)) SEPARATOR \
  DECLARE_FUNC_ENCODING \
  FUNC_SYMBOL(SYMBOL_NAME(name)):

#if defined(_WIN32) && __riscv_xlen == 64 && !defined(__riscv_abi_rve)

// Windows (RVUW): each entry is a separate function with an exact unwind
// record, because a fall-through chain would need entry-dependent unwind
// state. The caller tail-calls an entry from its epilogue, so the entry
// continues that epilogue on the caller's frame: the frame and register
// layout match __riscv_save_<N> (ra at size-8 and s<k> at size-16-8*k).

DEFINE_COMPILERRT_FUNCTION(__riscv_restore_12)
  .seh_proc SYMBOL_NAME(__riscv_restore_12)
  .seh_set_cfa sp, 112
  .seh_save_gpr ra, -8
  .seh_save_gpr s0, -16
  .seh_save_gpr s1, -24
  .seh_save_gpr s2, -32
  .seh_save_gpr s3, -40
  .seh_save_gpr s4, -48
  .seh_save_gpr s5, -56
  .seh_save_gpr s6, -64
  .seh_save_gpr s7, -72
  .seh_save_gpr s8, -80
  .seh_save_gpr s9, -88
  .seh_save_gpr s10, -96
  .seh_save_gpr s11, -104
  .seh_endprologue
  .seh_startepilogue
  ld s11, 8(sp)
  .seh_same_gpr s11
  ld s10, 16(sp)
  .seh_same_gpr s10
  ld s9, 24(sp)
  .seh_same_gpr s9
  ld s8, 32(sp)
  .seh_same_gpr s8
  ld s7, 40(sp)
  .seh_same_gpr s7
  ld s6, 48(sp)
  .seh_same_gpr s6
  ld s5, 56(sp)
  .seh_same_gpr s5
  ld s4, 64(sp)
  .seh_same_gpr s4
  ld s3, 72(sp)
  .seh_same_gpr s3
  ld s2, 80(sp)
  .seh_same_gpr s2
  ld s1, 88(sp)
  .seh_same_gpr s1
  ld s0, 96(sp)
  .seh_same_gpr s0
  ld ra, 104(sp)
  .seh_same_gpr ra
  addi sp, sp, 112
  .seh_set_cfa sp, 0
  ret
  .seh_endepilogue
  .seh_endproc

DEFINE_COMPILERRT_FUNCTION(__riscv_restore_11)
DEFINE_COMPILERRT_FUNCTION_ALIAS(__riscv_restore_10, __riscv_restore_11)
  .seh_proc SYMBOL_NAME(__riscv_restore_11)
  .seh_set_cfa sp, 96
  .seh_save_gpr ra, -8
  .seh_save_gpr s0, -16
  .seh_save_gpr s1, -24
  .seh_save_gpr s2, -32
  .seh_save_gpr s3, -40
  .seh_save_gpr s4, -48
  .seh_save_gpr s5, -56
  .seh_save_gpr s6, -64
  .seh_save_gpr s7, -72
  .seh_save_gpr s8, -80
  .seh_save_gpr s9, -88
  .seh_save_gpr s10, -96
  .seh_endprologue
  .seh_startepilogue
  ld s10, 0(sp)
  .seh_same_gpr s10
  ld s9, 8(sp)
  .seh_same_gpr s9
  ld s8, 16(sp)
  .seh_same_gpr s8
  ld s7, 24(sp)
  .seh_same_gpr s7
  ld s6, 32(sp)
  .seh_same_gpr s6
  ld s5, 40(sp)
  .seh_same_gpr s5
  ld s4, 48(sp)
  .seh_same_gpr s4
  ld s3, 56(sp)
  .seh_same_gpr s3
  ld s2, 64(sp)
  .seh_same_gpr s2
  ld s1, 72(sp)
  .seh_same_gpr s1
  ld s0, 80(sp)
  .seh_same_gpr s0
  ld ra, 88(sp)
  .seh_same_gpr ra
  addi sp, sp, 96
  .seh_set_cfa sp, 0
  ret
  .seh_endepilogue
  .seh_endproc

DEFINE_COMPILERRT_FUNCTION(__riscv_restore_9)
DEFINE_COMPILERRT_FUNCTION_ALIAS(__riscv_restore_8, __riscv_restore_9)
  .seh_proc SYMBOL_NAME(__riscv_restore_9)
  .seh_set_cfa sp, 80
  .seh_save_gpr ra, -8
  .seh_save_gpr s0, -16
  .seh_save_gpr s1, -24
  .seh_save_gpr s2, -32
  .seh_save_gpr s3, -40
  .seh_save_gpr s4, -48
  .seh_save_gpr s5, -56
  .seh_save_gpr s6, -64
  .seh_save_gpr s7, -72
  .seh_save_gpr s8, -80
  .seh_endprologue
  .seh_startepilogue
  ld s8, 0(sp)
  .seh_same_gpr s8
  ld s7, 8(sp)
  .seh_same_gpr s7
  ld s6, 16(sp)
  .seh_same_gpr s6
  ld s5, 24(sp)
  .seh_same_gpr s5
  ld s4, 32(sp)
  .seh_same_gpr s4
  ld s3, 40(sp)
  .seh_same_gpr s3
  ld s2, 48(sp)
  .seh_same_gpr s2
  ld s1, 56(sp)
  .seh_same_gpr s1
  ld s0, 64(sp)
  .seh_same_gpr s0
  ld ra, 72(sp)
  .seh_same_gpr ra
  addi sp, sp, 80
  .seh_set_cfa sp, 0
  ret
  .seh_endepilogue
  .seh_endproc

DEFINE_COMPILERRT_FUNCTION(__riscv_restore_7)
DEFINE_COMPILERRT_FUNCTION_ALIAS(__riscv_restore_6, __riscv_restore_7)
  .seh_proc SYMBOL_NAME(__riscv_restore_7)
  .seh_set_cfa sp, 64
  .seh_save_gpr ra, -8
  .seh_save_gpr s0, -16
  .seh_save_gpr s1, -24
  .seh_save_gpr s2, -32
  .seh_save_gpr s3, -40
  .seh_save_gpr s4, -48
  .seh_save_gpr s5, -56
  .seh_save_gpr s6, -64
  .seh_endprologue
  .seh_startepilogue
  ld s6, 0(sp)
  .seh_same_gpr s6
  ld s5, 8(sp)
  .seh_same_gpr s5
  ld s4, 16(sp)
  .seh_same_gpr s4
  ld s3, 24(sp)
  .seh_same_gpr s3
  ld s2, 32(sp)
  .seh_same_gpr s2
  ld s1, 40(sp)
  .seh_same_gpr s1
  ld s0, 48(sp)
  .seh_same_gpr s0
  ld ra, 56(sp)
  .seh_same_gpr ra
  addi sp, sp, 64
  .seh_set_cfa sp, 0
  ret
  .seh_endepilogue
  .seh_endproc

DEFINE_COMPILERRT_FUNCTION(__riscv_restore_5)
DEFINE_COMPILERRT_FUNCTION_ALIAS(__riscv_restore_4, __riscv_restore_5)
  .seh_proc SYMBOL_NAME(__riscv_restore_5)
  .seh_set_cfa sp, 48
  .seh_save_gpr ra, -8
  .seh_save_gpr s0, -16
  .seh_save_gpr s1, -24
  .seh_save_gpr s2, -32
  .seh_save_gpr s3, -40
  .seh_save_gpr s4, -48
  .seh_endprologue
  .seh_startepilogue
  ld s4, 0(sp)
  .seh_same_gpr s4
  ld s3, 8(sp)
  .seh_same_gpr s3
  ld s2, 16(sp)
  .seh_same_gpr s2
  ld s1, 24(sp)
  .seh_same_gpr s1
  ld s0, 32(sp)
  .seh_same_gpr s0
  ld ra, 40(sp)
  .seh_same_gpr ra
  addi sp, sp, 48
  .seh_set_cfa sp, 0
  ret
  .seh_endepilogue
  .seh_endproc

DEFINE_COMPILERRT_FUNCTION(__riscv_restore_3)
DEFINE_COMPILERRT_FUNCTION_ALIAS(__riscv_restore_2, __riscv_restore_3)
  .seh_proc SYMBOL_NAME(__riscv_restore_3)
  .seh_set_cfa sp, 32
  .seh_save_gpr ra, -8
  .seh_save_gpr s0, -16
  .seh_save_gpr s1, -24
  .seh_save_gpr s2, -32
  .seh_endprologue
  .seh_startepilogue
  ld s2, 0(sp)
  .seh_same_gpr s2
  ld s1, 8(sp)
  .seh_same_gpr s1
  ld s0, 16(sp)
  .seh_same_gpr s0
  ld ra, 24(sp)
  .seh_same_gpr ra
  addi sp, sp, 32
  .seh_set_cfa sp, 0
  ret
  .seh_endepilogue
  .seh_endproc

DEFINE_COMPILERRT_FUNCTION(__riscv_restore_1)
DEFINE_COMPILERRT_FUNCTION_ALIAS(__riscv_restore_0, __riscv_restore_1)
  .seh_proc SYMBOL_NAME(__riscv_restore_1)
  .seh_set_cfa sp, 16
  .seh_save_gpr ra, -8
  .seh_save_gpr s0, -16
  .seh_endprologue
  .seh_startepilogue
  ld s0, 0(sp)
  .seh_same_gpr s0
  ld ra, 8(sp)
  .seh_same_gpr ra
  addi sp, sp, 16
  .seh_set_cfa sp, 0
  ret
  .seh_endepilogue
  .seh_endproc

#elif __riscv_xlen == 32

#ifndef __riscv_abi_rve

DEFINE_COMPILERRT_OVERLAPPING_FUNCTION(__riscv_restore_12)
  lw      s11, 12(sp)
  addi    sp, sp, 16
  // fallthrough into __riscv_restore_11

DEFINE_COMPILERRT_OVERLAPPING_FUNCTION(__riscv_restore_11)
DEFINE_COMPILERRT_FUNCTION_ALIAS(__riscv_restore_10, __riscv_restore_11)
DEFINE_COMPILERRT_FUNCTION_ALIAS(__riscv_restore_9,  __riscv_restore_11)
DEFINE_COMPILERRT_FUNCTION_ALIAS(__riscv_restore_8,  __riscv_restore_11)
  lw      s10, 0(sp)
  lw      s9,  4(sp)
  lw      s8,  8(sp)
  lw      s7,  12(sp)
  addi    sp, sp, 16
  // fallthrough into __riscv_restore_7

DEFINE_COMPILERRT_OVERLAPPING_FUNCTION(__riscv_restore_7)
DEFINE_COMPILERRT_FUNCTION_ALIAS(__riscv_restore_6, __riscv_restore_7)
DEFINE_COMPILERRT_FUNCTION_ALIAS(__riscv_restore_5, __riscv_restore_7)
DEFINE_COMPILERRT_FUNCTION_ALIAS(__riscv_restore_4, __riscv_restore_7)
  lw      s6,  0(sp)
  lw      s5,  4(sp)
  lw      s4,  8(sp)
  lw      s3,  12(sp)
  addi    sp, sp, 16
  // fallthrough into __riscv_restore_3

DEFINE_COMPILERRT_OVERLAPPING_FUNCTION(__riscv_restore_3)
DEFINE_COMPILERRT_FUNCTION_ALIAS(__riscv_restore_2, __riscv_restore_3)
DEFINE_COMPILERRT_FUNCTION_ALIAS(__riscv_restore_1, __riscv_restore_3)
DEFINE_COMPILERRT_FUNCTION_ALIAS(__riscv_restore_0, __riscv_restore_3)
  lw      s2,  0(sp)
  lw      s1,  4(sp)
  lw      s0,  8(sp)
  lw      ra,  12(sp)
  addi    sp, sp, 16
  ret
END_COMPILERRT_FUNCTION(__riscv_restore_12)
END_COMPILERRT_FUNCTION(__riscv_restore_11)
END_COMPILERRT_FUNCTION(__riscv_restore_7)
END_COMPILERRT_FUNCTION(__riscv_restore_3)

#else

DEFINE_COMPILERRT_OVERLAPPING_FUNCTION(__riscv_restore_2)
  lw      s1, 0(sp)
  addi    sp, sp, 4
  // fallthrough into __riscv_restore_1/0

DEFINE_COMPILERRT_OVERLAPPING_FUNCTION(__riscv_restore_1)
  lw      s0, 0(sp)
  addi    sp, sp, 4
  // fallthrough into __riscv_restore_0

DEFINE_COMPILERRT_OVERLAPPING_FUNCTION(__riscv_restore_0)
  lw      ra, 0(sp)
  addi    sp, sp, 4
  ret
END_COMPILERRT_FUNCTION(__riscv_restore_2)
END_COMPILERRT_FUNCTION(__riscv_restore_1)
END_COMPILERRT_FUNCTION(__riscv_restore_0)

#endif

#elif __riscv_xlen == 64

#ifndef __riscv_abi_rve

DEFINE_COMPILERRT_OVERLAPPING_FUNCTION(__riscv_restore_12)
  ld      s11, 8(sp)
  addi    sp, sp, 16
  // fallthrough into __riscv_restore_11/10

DEFINE_COMPILERRT_OVERLAPPING_FUNCTION(__riscv_restore_11)
DEFINE_COMPILERRT_FUNCTION_ALIAS(__riscv_restore_10, __riscv_restore_11)
  ld      s10, 0(sp)
  ld      s9,  8(sp)
  addi    sp, sp, 16
  // fallthrough into __riscv_restore_9/8

DEFINE_COMPILERRT_OVERLAPPING_FUNCTION(__riscv_restore_9)
DEFINE_COMPILERRT_FUNCTION_ALIAS(__riscv_restore_8, __riscv_restore_9)
  ld      s8,  0(sp)
  ld      s7,  8(sp)
  addi    sp, sp, 16
  // fallthrough into __riscv_restore_7/6

DEFINE_COMPILERRT_OVERLAPPING_FUNCTION(__riscv_restore_7)
DEFINE_COMPILERRT_FUNCTION_ALIAS(__riscv_restore_6, __riscv_restore_7)
  ld      s6,  0(sp)
  ld      s5,  8(sp)
  addi    sp, sp, 16
  // fallthrough into __riscv_restore_5/4

DEFINE_COMPILERRT_OVERLAPPING_FUNCTION(__riscv_restore_5)
DEFINE_COMPILERRT_FUNCTION_ALIAS(__riscv_restore_4, __riscv_restore_5)
  ld      s4,  0(sp)
  ld      s3,  8(sp)
  addi    sp, sp, 16
  // fallthrough into __riscv_restore_3/2

DEFINE_COMPILERRT_OVERLAPPING_FUNCTION(__riscv_restore_3)
DEFINE_COMPILERRT_FUNCTION_ALIAS(__riscv_restore_2, __riscv_restore_3)
  ld      s2,  0(sp)
  ld      s1,  8(sp)
  addi    sp, sp, 16
  // fallthrough into __riscv_restore_1/0

DEFINE_COMPILERRT_OVERLAPPING_FUNCTION(__riscv_restore_1)
DEFINE_COMPILERRT_FUNCTION_ALIAS(__riscv_restore_0, __riscv_restore_1)
  ld      s0,  0(sp)
  ld      ra,  8(sp)
  addi    sp, sp, 16
  ret
END_COMPILERRT_FUNCTION(__riscv_restore_12)
END_COMPILERRT_FUNCTION(__riscv_restore_11)
END_COMPILERRT_FUNCTION(__riscv_restore_9)
END_COMPILERRT_FUNCTION(__riscv_restore_7)
END_COMPILERRT_FUNCTION(__riscv_restore_5)
END_COMPILERRT_FUNCTION(__riscv_restore_3)
END_COMPILERRT_FUNCTION(__riscv_restore_1)

#else

DEFINE_COMPILERRT_OVERLAPPING_FUNCTION(__riscv_restore_2)
  ld      s1, 0(sp)
  addi    sp, sp, 8
  // fallthrough into __riscv_restore_1/0

DEFINE_COMPILERRT_OVERLAPPING_FUNCTION(__riscv_restore_1)
  ld      s0, 0(sp)
  addi    sp, sp, 8
  // fallthrough into __riscv_restore_0

DEFINE_COMPILERRT_OVERLAPPING_FUNCTION(__riscv_restore_0)
  ld      ra, 0(sp)
  addi    sp, sp, 8
  ret

#endif

#else
# error "xlen must be 32 or 64 for save-restore implementation
#endif
