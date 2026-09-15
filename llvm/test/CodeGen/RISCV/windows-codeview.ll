; REQUIRES: riscv-registered-target
; RUN: llc -mtriple=riscv64-pc-windows-msvc -filetype=obj < %s -o %t.obj
; RUN: llvm-readobj --file-headers --relocations --codeview %t.obj | FileCheck %s

; Verify the RISC-V Windows CodeView CPU and register numbering.
;
; CHECK: Machine: IMAGE_FILE_MACHINE_RISCV64 (0x5064)
; CHECK: IMAGE_REL_RISCV_SECREL main
; CHECK: Compile3Sym {
; CHECK: Machine: RISCV64 (0xFA)
; CHECK: LocalFramePtrReg: RISCV_NOREG (0x0)
; CHECK: Register: RISCV_X10 (0x14)

target triple = "riscv64-pc-windows-msvc"

define i32 @main(i32 %argc, ptr %argv) !dbg !4 {
entry:
  call void @llvm.dbg.value(metadata i32 %argc, metadata !9, metadata !DIExpression()), !dbg !11
  call void @llvm.dbg.value(metadata ptr %argv, metadata !10, metadata !DIExpression()), !dbg !11
  %sub = add nsw i32 %argc, -1, !dbg !12
  ret i32 %sub, !dbg !13
}

declare void @llvm.dbg.value(metadata, metadata, metadata)

!llvm.dbg.cu = !{!0}
!llvm.module.flags = !{!7, !8}

!0 = distinct !DICompileUnit(language: DW_LANG_C99, file: !1, producer: "clang", isOptimized: false, runtimeVersion: 0, emissionKind: FullDebug, enums: !2)
!1 = !DIFile(filename: "test.c", directory: "C:\\build")
!2 = !{}
!3 = !DIBasicType(name: "int", size: 32, encoding: DW_ATE_signed)
!4 = distinct !DISubprogram(name: "main", scope: !1, file: !1, line: 1, type: !5, scopeLine: 1, flags: DIFlagPrototyped, spFlags: DISPFlagDefinition, unit: !0, retainedNodes: !2)
!5 = !DISubroutineType(types: !6)
!6 = !{!3, !3, !14}
!7 = !{i32 2, !"CodeView", i32 1}
!8 = !{i32 2, !"Debug Info Version", i32 3}
!9 = !DILocalVariable(name: "argc", arg: 1, scope: !4, file: !1, line: 1, type: !3)
!10 = !DILocalVariable(name: "argv", arg: 2, scope: !4, file: !1, line: 1, type: !14)
!11 = !DILocation(line: 1, scope: !4)
!12 = !DILocation(line: 2, scope: !4)
!13 = !DILocation(line: 3, scope: !4)
!14 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !15, size: 64)
!15 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !16, size: 64)
!16 = !DIBasicType(name: "char", size: 8, encoding: DW_ATE_signed_char)
