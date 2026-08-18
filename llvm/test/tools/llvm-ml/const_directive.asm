; RUN: llvm-ml64 %s /Fo - | llvm-readobj --sections - | FileCheck %s

.const
value QWORD 1

; CHECK:      Name: .rdata
; CHECK-NOT:  }
; CHECK-DAG:  IMAGE_SCN_CNT_INITIALIZED_DATA
; CHECK-DAG:  IMAGE_SCN_MEM_READ
; CHECK-NOT:  IMAGE_SCN_MEM_WRITE
; CHECK:      }

END
