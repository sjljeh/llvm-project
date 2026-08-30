# REQUIRES: x86
# RUN: split-file %s %t.dir
# RUN: llvm-mc -triple i686-windows-msvc %t.dir/stdcall.s -o %t.stdcall.obj -filetype=obj
# RUN: llvm-mc -triple i686-windows-msvc %t.dir/cdecl.s -o %t.cdecl.obj -filetype=obj
# RUN: llvm-ar rcs %t.cdecl.lib %t.cdecl.obj
# RUN: lld-link -safeseh:no %t.stdcall.obj %t.cdecl.lib -out:%t.dll -dll -nodefaultlib -noentry -def:%t.dir/export.def
# RUN: llvm-nm %t.lib | FileCheck --check-prefix=IMPLIB %s
# RUN: llvm-objdump -d --no-show-raw-insn %t.dll | FileCheck --check-prefix=DLL %s

## link.exe infers the decorated target before extracting an exact undecorated
## target from an archive.

# IMPLIB-DAG: T __imp__exported@8
# IMPLIB-DAG: T _exported@8

# DLL: <exported>:
# DLL-NEXT: movl $1, %eax
# DLL-NEXT: retl $8

#--- export.def
EXPORTS
  exported=target

#--- stdcall.s
.text
.globl _target@8
_target@8:
  movl $1, %eax
  retl $8

#--- cdecl.s
.text
.globl _target
_target:
  movl $2, %eax
  retl
