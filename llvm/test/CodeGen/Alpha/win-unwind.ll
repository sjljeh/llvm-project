; RUN: llc -mtriple=alpha-pc-windows-msvc -mattr=+taso -filetype=asm %s -o - | FileCheck %s
; RUN: llc -mtriple=alpha-pc-windows-msvc -mattr=+taso -filetype=obj %s -o %t.obj
; RUN: llvm-readobj --sections --relocations --hex-dump=.pdata %t.obj | FileCheck %s --check-prefix=OBJ

declare i64 @callee(i64)

define i64 @framed(i64 %x) uwtable {
; CHECK-LABEL: framed:
; CHECK-NEXT:  .seh_proc framed
; CHECK:       lda $30,-{{[0-9]+}}($30)
; CHECK-NEXT:  stq $[[SAVE:[0-9]+]],{{[0-9]+}}($30)
; CHECK-NEXT:  bis $26,$26,$[[SAVE]]
; CHECK-NEXT:  .seh_endprologue
; CHECK: bsr $26,$callee..ng
; CHECK:       .seh_endproc
entry:
  %result = call i64 @callee(i64 %x)
  ret i64 %result
}

; Alpha runtime-function records contain five absolute 32-bit fields:
; begin, end, exception handler, handler data, and prologue end.
; OBJ: Name: .pdata
; OBJ: RawDataSize: 20
; OBJ:      RelocationCount: 3
; OBJ:      0x0 IMAGE_REL_ALPHA_REFLONG .text
; OBJ:      0x4 IMAGE_REL_ALPHA_REFLONG .text
; OBJ:      0x10 IMAGE_REL_ALPHA_REFLONG .text
; OBJ:      Hex dump of section '.pdata':
; OBJ-NEXT: 0x00000000 00000000 20000000 00000000 00000000
; OBJ-NEXT: 0x00000010 0c000000