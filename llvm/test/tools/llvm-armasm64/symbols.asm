; RUN: llvm-armasm64 %s %t.obj
; RUN: llvm-readobj --symbols --relocations %t.obj | FileCheck %s --check-prefix=OBJ --implicit-check-not="Name: unused_extern" --implicit-check-not="Name: unused_weak"

        IMPORT imported
        EXTERN used_extern
        EXTERN unused_extern
        IMPORT weak_default, WEAK fallback_default
        IMPORT weak_no_library, WEAK fallback_no_library, TYPE 1
        IMPORT weak_library, WEAK fallback_library, TYPE 2
        EXTERN used_weak, WEAK fallback_used, TYPE 3
        EXTERN unused_weak, WEAK fallback_unused, TYPE 1
        EXPORT function[FUNC]
        GLOBAL datum [DATA]
        COMMON zero
        COMMON common, 12

        AREA |.text|, CODE, READONLY
function
        bl imported
        bl used_extern
        bl weak_default
        bl weak_no_library
        bl weak_library
        bl used_weak
        adrp x0, common
        ret

        AREA |.data|, DATA, READWRITE
datum   DCD 1
        END

; OBJ:      0x0 IMAGE_REL_ARM64_BRANCH26 imported
; OBJ:      0x4 IMAGE_REL_ARM64_BRANCH26 used_extern
; OBJ:      0x8 IMAGE_REL_ARM64_BRANCH26 weak_default
; OBJ:      0xC IMAGE_REL_ARM64_BRANCH26 weak_no_library
; OBJ:      0x10 IMAGE_REL_ARM64_BRANCH26 weak_library
; OBJ:      0x14 IMAGE_REL_ARM64_BRANCH26 used_weak
; OBJ:      0x18 IMAGE_REL_ARM64_PAGEBASE_REL21 common
; OBJ:      Name: imported
; OBJ:      Name: fallback_unused
; OBJ:      Name: function
; OBJ-NEXT: Value: 0
; OBJ-NEXT: Section: .text
; OBJ-NEXT: BaseType: Null
; OBJ-NEXT: ComplexType: Function
; OBJ-NEXT: StorageClass: External
; OBJ:      Name: datum
; OBJ-NEXT: Value: 0
; OBJ-NEXT: Section: .data
; OBJ-NEXT: BaseType: Null
; OBJ-NEXT: ComplexType: Null
; OBJ-NEXT: StorageClass: External
; OBJ:      Name: zero
; OBJ-NEXT: Value: 0
; OBJ-NEXT: Section: IMAGE_SYM_UNDEFINED
; OBJ:      Name: common
; OBJ-NEXT: Value: 12
; OBJ-NEXT: Section: IMAGE_SYM_UNDEFINED
; OBJ:      Name: used_extern
; OBJ:      Name: weak_default
; OBJ:      StorageClass: WeakExternal
; OBJ:      Linked: fallback_default
; OBJ-NEXT: Search: Alias
; OBJ:      Name: weak_no_library
; OBJ:      StorageClass: WeakExternal
; OBJ:      Linked: fallback_no_library
; OBJ-NEXT: Search: NoLibrary
; OBJ:      Name: weak_library
; OBJ:      StorageClass: WeakExternal
; OBJ:      Linked: fallback_library
; OBJ-NEXT: Search: Library
; OBJ:      Name: used_weak
; OBJ:      StorageClass: WeakExternal
; OBJ:      Linked: fallback_used
; OBJ-NEXT: Search: Alias
