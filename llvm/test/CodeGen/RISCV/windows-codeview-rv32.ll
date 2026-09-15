; REQUIRES: riscv-registered-target
; RUN: llc -mtriple=riscv32-pc-windows-msvc -filetype=obj < %s -o %t.obj
; RUN: llvm-readobj --file-headers --relocations --codeview %t.obj | FileCheck %s

; Verify that RV32 uses the RISC-V register set with a distinct CodeView CPU
; value.
;
; CHECK: Machine: IMAGE_FILE_MACHINE_RISCV32 (0x5032)
; CHECK: IMAGE_REL_RISCV_SECREL function
; CHECK: Compile3Sym {
; CHECK: Machine: RISCV32 (0xFB)
; CHECK: LocalFramePtrReg: RISCV_NOREG (0x0)
; CHECK: Register: RISCV_X10 (0x14)

target triple = "riscv32-pc-windows-msvc"

define i32 @function(i32 %argument) !dbg !4 {
entry:
  call void @llvm.dbg.value(metadata i32 %argument, metadata !9, metadata !DIExpression()), !dbg !10
  %result = add nsw i32 %argument, -1, !dbg !11
  ret i32 %result, !dbg !12
}

declare void @llvm.dbg.value(metadata, metadata, metadata)

!llvm.dbg.cu = !{!0}
!llvm.module.flags = !{!7, !8}

!0 = distinct !DICompileUnit(language: DW_LANG_C99, file: !1, producer: "clang", isOptimized: false, runtimeVersion: 0, emissionKind: FullDebug, enums: !2)
!1 = !DIFile(filename: "test.c", directory: "C:\\build")
!2 = !{}
!3 = !DIBasicType(name: "int", size: 32, encoding: DW_ATE_signed)
!4 = distinct !DISubprogram(name: "function", scope: !1, file: !1, line: 1, type: !5, scopeLine: 1, flags: DIFlagPrototyped, spFlags: DISPFlagDefinition, unit: !0, retainedNodes: !2)
!5 = !DISubroutineType(types: !6)
!6 = !{!3, !3}
!7 = !{i32 2, !"CodeView", i32 1}
!8 = !{i32 2, !"Debug Info Version", i32 3}
!9 = !DILocalVariable(name: "argument", arg: 1, scope: !4, file: !1, line: 1, type: !3)
!10 = !DILocation(line: 1, scope: !4)
!11 = !DILocation(line: 2, scope: !4)
!12 = !DILocation(line: 3, scope: !4)
