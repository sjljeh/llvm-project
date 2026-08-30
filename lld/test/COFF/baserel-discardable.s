# REQUIRES: x86

# RUN: llvm-mc -triple=i686-pc-windows-msvc -filetype=obj %s -o %t.obj
# RUN: lld-link /dll /noentry /opt:noref /safeseh:no /out:%t.dll %t.obj
# RUN: llvm-readobj --sections --coff-basereloc %t.dll | FileCheck %s

# CHECK:      Name: INIT
# CHECK:      VirtualAddress: [[INIT_RVA:0x[0-9A-F]+]]
# CHECK:      IMAGE_SCN_MEM_DISCARDABLE
# CHECK:      IMAGE_SCN_MEM_EXECUTE
# CHECK:      Name: INITDATA
# CHECK:      VirtualAddress: [[INITDATA_RVA:0x[0-9A-F]+]]
# CHECK:      IMAGE_SCN_MEM_DISCARDABLE
# CHECK-NOT:  IMAGE_SCN_MEM_EXECUTE
# CHECK:      BaseReloc [
# CHECK-NEXT:   Entry {
# CHECK-NEXT:     Type: HIGHLOW
# CHECK-NEXT:     Address: [[INIT_RVA]]
# CHECK-NEXT:   }
# CHECK-NEXT:   Entry {
# CHECK-NEXT:     Type: ABSOLUTE
# CHECK-NEXT:     Address: {{0x[0-9A-F]+}}
# CHECK-NEXT:   }
# CHECK-NEXT:   Entry {
# CHECK-NEXT:     Type: HIGHLOW
# CHECK-NEXT:     Address: [[INITDATA_RVA]]
# CHECK-NEXT:   }
# CHECK-NEXT:   Entry {
# CHECK-NEXT:     Type: ABSOLUTE
# CHECK-NEXT:     Address: {{0x[0-9A-F]+}}
# CHECK-NEXT:   }

        .section INIT,"dwxD"
        .long target

        .section INITDATA,"dwD"
        .long target

        .text
target:
        retl
