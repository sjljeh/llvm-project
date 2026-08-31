; RUN: llvm-ml64 %s /Fo %t-no-debug.obj
; RUN: llvm-readobj --sections --codeview %t-no-debug.obj | FileCheck %s --check-prefix=NO-DEBUG
; RUN: llvm-ml /Zd %s /Fo %t-x86-zd.obj
; RUN: llvm-readobj --sections --relocations --codeview %t-x86-zd.obj | FileCheck %s --check-prefixes=COMMON,X86,SHA256 && llvm-readobj --sections --codeview %t-x86-zd.obj | FileCheck %s --check-prefix=ZD
; RUN: llvm-ml64 /Zi %s /Fo %t-x64-zi.obj
; RUN: llvm-readobj --sections --relocations --codeview %t-x64-zi.obj | FileCheck %s --check-prefixes=COMMON,X64,SHA256 && llvm-readobj --sections --codeview %t-x64-zi.obj | FileCheck %s --check-prefix=ZI
; RUN: llvm-ml64 /Zi /Zd %s /Fo %t-x64-both.obj
; RUN: llvm-readobj --sections --codeview %t-x64-both.obj | FileCheck %s --check-prefix=ZI
; RUN: llvm-ml64 /Zi /ZH:SHA_256 /ZH:MD5 %s /Fo %t-md5.obj
; RUN: llvm-readobj --codeview %t-md5.obj | FileCheck %s --check-prefix=MD5
; RUN: llvm-ml64 /Zi /ZH:MD5 /ZH:SHA_256 %s /Fo %t-sha256.obj
; RUN: llvm-readobj --codeview %t-sha256.obj | FileCheck %s --check-prefix=SHA256

.code
DebugProc PROC
  nop
  ret
DebugProc ENDP
END

; COMMON:      Name: .debug$S
; COMMON:      IMAGE_SCN_ALIGN_1BYTES
; COMMON:      IMAGE_SCN_MEM_DISCARDABLE
; COMMON:      SubSectionType: StringTable
; COMMON:      SubSectionType: FileChecksums
; SHA256:      FileChecksum {
; SHA256:      Filename: {{.*}}debug_info.asm
; SHA256:      ChecksumSize: 0x20
; SHA256-NEXT: ChecksumKind: SHA256
; COMMON:      SubSectionType: Lines
; COMMON:      LinkageName: $$000000
; COMMON:      SubSectionType: Symbols
; COMMON:      Compile3Sym {
; COMMON:      Language: Masm
; X86:         Machine: Intel80386
; X64:         Machine: X64
; COMMON:      FunctionLineTable [
; COMMON:      CodeSize: 0x2
; COMMON:      LineNumberStart: 15
; COMMON:      LineNumberStart: 16
; COMMON:      LineNumberStart: 17

; NO-DEBUG:      Name: .debug$S
; NO-DEBUG-NOT:  Name: .debug$T
; NO-DEBUG:      CodeViewDebugInfo [
; NO-DEBUG-NOT:  FileChecksum
; NO-DEBUG-NOT:  SubSectionType: Lines
; NO-DEBUG:      SubSectionType: Symbols
; NO-DEBUG:      NoDbgInfo
; NO-DEBUG-NOT:  EnvBlockSym
; NO-DEBUG-NOT:  GlobalProcSym

; ZD-NOT:      Name: .debug$T
; ZD:          CodeViewDebugInfo [
; ZD:          NoDbgInfo
; ZD-NOT:      EnvBlockSym
; ZD-NOT:      GlobalProcSym

; ZI:          Name: .debug$S
; ZI:          Name: .debug$T
; ZI:          IMAGE_SCN_ALIGN_1BYTES
; ZI:          ArgList (0x1000)
; ZI:          Procedure (0x1001)
; ZI:          Compile3Sym {
; ZI-NOT:      NoDbgInfo
; ZI:          EnvBlockSym {
; ZI:          cwd
; ZI:          exe
; ZI:          src
; ZI:          GlobalProcSym {
; ZI:          CodeSize: 0x2
; ZI:          DbgStart: 0x0
; ZI:          DbgEnd: 0x2
; ZI:          FunctionType: void () (0x1001)
; ZI:          DisplayName: DebugProc
; ZI:          LinkageName: DebugProc
; ZI:          ScopeEndSym {

; MD5:         FileChecksum {
; MD5:         Filename: {{.*}}debug_info.asm
; MD5:         ChecksumSize: 0x10
; MD5-NEXT:    ChecksumKind: MD5
