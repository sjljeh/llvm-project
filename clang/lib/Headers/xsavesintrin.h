/*===---- xsavesintrin.h - XSAVES intrinsic --------------------------------===
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 *===-----------------------------------------------------------------------===
 */

#ifndef __IMMINTRIN_H
#error "Never use <xsavesintrin.h> directly; include <immintrin.h> instead."
#endif

#ifndef __XSAVESINTRIN_H
#define __XSAVESINTRIN_H

/* Define the default attributes for the functions in this file. */
#define __DEFAULT_FN_ATTRS __attribute__((__always_inline__, __nodebug__,  __target__("xsaves")))

#ifdef __cplusplus
#define __XSAVES_EXTERN_C extern "C"
#else
#define __XSAVES_EXTERN_C
#endif

#if defined(_MSC_VER) && __has_builtin(_xsaves)
__XSAVES_EXTERN_C void __cdecl _xsaves(void *__p, unsigned __int64 __m);
#else
static __inline__ void __DEFAULT_FN_ATTRS
_xsaves(void *__p, unsigned long long __m) {
  __builtin_ia32_xsaves(__p, __m);
}
#endif

#if defined(_MSC_VER) && __has_builtin(_xrstors)
__XSAVES_EXTERN_C void __cdecl _xrstors(void const *__p,
                                        unsigned __int64 __m);
#else
static __inline__ void __DEFAULT_FN_ATTRS
_xrstors(void *__p, unsigned long long __m) {
  __builtin_ia32_xrstors(__p, __m);
}
#endif

#ifdef __x86_64__
#if defined(_MSC_VER) && __has_builtin(_xrstors64)
__XSAVES_EXTERN_C void __cdecl _xrstors64(void const *__p,
                                          unsigned __int64 __m);
#else
static __inline__ void __DEFAULT_FN_ATTRS
_xrstors64(void *__p, unsigned long long __m) {
  __builtin_ia32_xrstors64(__p, __m);
}
#endif

#if defined(_MSC_VER) && __has_builtin(_xsaves64)
__XSAVES_EXTERN_C void __cdecl _xsaves64(void *__p, unsigned __int64 __m);
#else
static __inline__ void __DEFAULT_FN_ATTRS
_xsaves64(void *__p, unsigned long long __m) {
  __builtin_ia32_xsaves64(__p, __m);
}
#endif
#endif

#undef __XSAVES_EXTERN_C
#undef __DEFAULT_FN_ATTRS

#endif
