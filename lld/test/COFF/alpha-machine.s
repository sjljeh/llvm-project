# RUN: llvm-mc -triple=alpha-pc-windows-msvc -mattr=+taso -filetype=obj %s -o %t.alpha.obj
# RUN: lld-link /machine:alpha /entry:start /subsystem:native /nodefaultlib %t.alpha.obj /out:%t.alpha.exe
# RUN: llvm-readobj --file-headers %t.alpha.exe | FileCheck %s --check-prefix=ALPHA
# RUN: llvm-mc -triple=alpha-pc-windows-msvc -filetype=obj %s -o %t.alpha64.obj
# RUN: lld-link /machine:alpha64 /entry:start /subsystem:native /nodefaultlib %t.alpha64.obj /out:%t.alpha64.exe
# RUN: llvm-readobj --file-headers %t.alpha64.exe | FileCheck %s --check-prefix=ALPHA64
# RUN: not lld-link /machine:alpha64 /entry:start /subsystem:native /nodefaultlib %t.alpha.obj /out:%t.bad.exe 2>&1 | FileCheck %s --check-prefix=INCOMPAT

.text
.globl start
start:
  ret

# ALPHA: Format: COFF-Alpha
# ALPHA: Machine: IMAGE_FILE_MACHINE_ALPHA (0x184)
# ALPHA: Magic: 0x10B
# ALPHA: Subsystem: IMAGE_SUBSYSTEM_NATIVE (0x1)

# ALPHA64: Format: COFF-Alpha64
# ALPHA64: Machine: IMAGE_FILE_MACHINE_ALPHA64 (0x284)
# ALPHA64: Magic: 0x20B
# ALPHA64: ImageBase: 0x140000000
# ALPHA64: Subsystem: IMAGE_SUBSYSTEM_NATIVE (0x1)

# INCOMPAT: machine type alpha conflicts with alpha64
