// RUN: rm -rf %t && split-file %s %t
// RUN: %clang -target alpha-pc-windows-msvc -mtaso -O2 -c %t/start.c -o %t/start.obj
// RUN: llvm-readobj --file-headers --relocations %t/start.obj | FileCheck %s --check-prefix=OBJ
// RUN: %clang -target alpha-pc-windows-msvc -mtaso -O2 -c %t/api.c -o %t/api.obj
// RUN: lld-link /machine:alpha /entry:start /subsystem:native /nodefaultlib %t/start.obj %t/api.obj /out:%t/external-call.exe
// RUN: llvm-readobj --file-headers %t/external-call.exe | FileCheck %s --check-prefix=EXE

//--- start.c
extern int api(int, int);
int start(void) { return api(1, 2); }

//--- api.c
int api(int a, int b) { return a + b; }

// OBJ: Format: COFF-Alpha
// OBJ: AddressSize: 32bit
// OBJ: IMAGE_REL_ALPHA_BRADDR api

// EXE: Format: COFF-Alpha
// EXE: Machine: IMAGE_FILE_MACHINE_ALPHA (0x184)
// EXE: IMAGE_FILE_EXECUTABLE_IMAGE
// EXE: Subsystem: IMAGE_SUBSYSTEM_NATIVE (0x1)
