; REQUIRES: powerpc-registered-target
; RUN: llc -mtriple=powerpcle-pc-windows-msvc -filetype=obj < %s -o %t.obj
; RUN: llvm-readobj --file-headers --relocations --codeview %t.obj | FileCheck %s

; Verify that PowerPC Windows objects use the Microsoft CodeView CPU and
; register numbers, and that CodeView section references use PowerPC COFF
; relocations.
;
; CHECK: Machine: IMAGE_FILE_MACHINE_POWERPC (0x1F0)
; CHECK: IMAGE_REL_PPC_SECREL ..main
; CHECK: Compile3Sym {
; CHECK: Machine: PPC601 (0x40)
; CHECK: LocalFramePtrReg: PPC_NOREG (0x0)
; CHECK: Register: PPC_GPR3 (0x4)

target triple = "powerpcle-pc-windows-msvc"

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
!1 = !DIFile(filename: "testCCompiler.c", directory: "C:\\build")
!2 = !{}
!4 = distinct !DISubprogram(name: "main", scope: !1, file: !1, line: 1, type: !5, scopeLine: 1, flags: DIFlagPrototyped, spFlags: DISPFlagDefinition, unit: !0, retainedNodes: !2)
!5 = !DISubroutineType(types: !6)
!6 = !{!3, !3, !14}
!3 = !DIBasicType(name: "int", size: 32, encoding: DW_ATE_signed)
!7 = !{i32 2, !"CodeView", i32 1}
!8 = !{i32 2, !"Debug Info Version", i32 3}
!9 = !DILocalVariable(name: "argc", arg: 1, scope: !4, file: !1, line: 1, type: !3)
!10 = !DILocalVariable(name: "argv", arg: 2, scope: !4, file: !1, line: 1, type: !14)
!11 = !DILocation(line: 1, scope: !4)
!12 = !DILocation(line: 2, scope: !4)
!13 = !DILocation(line: 3, scope: !4)
!14 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !15, size: 32)
!15 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !16, size: 32)
!16 = !DIBasicType(name: "char", size: 8, encoding: DW_ATE_signed_char)
