/*===---- xsaveoptintrin.h - XSAVEOPT intrinsic ----------------------------===
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 *===-----------------------------------------------------------------------===
 */

#ifndef __IMMINTRIN_H
#error "Never use <xsaveoptintrin.h> directly; include <immintrin.h> instead."
#endif

#ifndef __XSAVEOPTINTRIN_H
#define __XSAVEOPTINTRIN_H

/* Define the default attributes for the functions in this file. */
#define __DEFAULT_FN_ATTRS __attribute__((__always_inline__, __nodebug__,  __target__("xsaveopt")))

#ifdef __cplusplus
#define __XSAVEOPT_EXTERN_C extern "C"
#else
#define __XSAVEOPT_EXTERN_C
#endif

#if defined(_MSC_VER) && __has_builtin(_xsaveopt)
__XSAVEOPT_EXTERN_C void __cdecl _xsaveopt(void *__p, unsigned __int64 __m);
#else
static __inline__ void __DEFAULT_FN_ATTRS
_xsaveopt(void *__p, unsigned long long __m) {
  __builtin_ia32_xsaveopt(__p, __m);
}
#endif

#ifdef __x86_64__
#if defined(_MSC_VER) && __has_builtin(_xsaveopt64)
__XSAVEOPT_EXTERN_C void __cdecl _xsaveopt64(void *__p,
                                             unsigned __int64 __m);
#else
static __inline__ void __DEFAULT_FN_ATTRS
_xsaveopt64(void *__p, unsigned long long __m) {
  __builtin_ia32_xsaveopt64(__p, __m);
}
#endif
#endif

#undef __XSAVEOPT_EXTERN_C
#undef __DEFAULT_FN_ATTRS

#endif
