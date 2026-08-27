// RUN: %clang -target alpha-pc-windows-msvc -mtaso -fms-extensions -S %s -o - | FileCheck %s --check-prefix=ASM
// RUN: %clang -target alpha-pc-windows-msvc -mtaso -fms-extensions -emit-llvm -S %s -o - | FileCheck %s --check-prefix=IR

// Microsoft VC4 Alpha's stdarg.h declares this structure itself and calls the
// three-argument spelling of __builtin_va_start.
typedef struct {
  char *a0;
  int offset;
} legacy_va_list;

#define legacy_va_start(list, last) __builtin_va_start(list, last, 1)
#define legacy_va_arg(list, type)                                           \
  (*(((list).offset += ((int)sizeof(type) + 7) & -8),                       \
     (type *)((list).a0 + (list).offset -                                  \
              ((__builtin_isfloat(type) && (list).offset <= 48)            \
                   ? 56                                                     \
                   : ((int)sizeof(type) + 7) & -8))))

__attribute__((noinline)) double take_double(int count, ...) {
  legacy_va_list ap;
  legacy_va_start(ap, count);
  return legacy_va_arg(ap, double);
}

// The register-save-area base is stored in a0 and the first unnamed slot (8)
// is stored in offset.  The legacy macro then selects the FP save area.
// ASM-LABEL: take_double:
// ASM: stl $2,0($3)
// ASM: lda $2,8($31)
// ASM: stl $2,4($3)
// ASM: ldt $f0,0($2)
// ASM: ret

// IR: define dso_local double @take_double
// IR: call void @llvm.va_start
// IR: attributes #0 = {{.*}}"alpha-ms-va-list"{{.*}}
