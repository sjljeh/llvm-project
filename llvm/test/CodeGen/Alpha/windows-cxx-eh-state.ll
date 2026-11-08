; RUN: llc -mtriple=alpha-pc-windows-msvc -O2 < %s | FileCheck %s
; RUN: llc -mtriple=alpha-pc-windows-msvc -O0 < %s | FileCheck %s --check-prefix=O0
; RUN: llc -mtriple=alpha-pc-windows-msvc -O2 -filetype=obj < %s -o %t.obj
; RUN: llvm-readobj --relocations %t.obj | FileCheck %s --check-prefix=OBJ

; The NT Alpha C++ runtime (__CxxFrameHandler) has no IP-to-state map. It
; reads a { ULONG State; PVOID Rfp; } record at EstablisherFrame +
; FuncInfo.EHContextDelta, and calls every funclet with Rfp in $1. The
; function stores Rfp ($15) once and the state number before each call that
; can throw.
;
;   int f() {
;     try { D d; may_throw(1); }
;     catch (int &e) { may_throw(e); return e; }
;     may_throw(2);
;     return 0;
;   }

target triple = "alpha-pc-windows-msvc19.33.0"

%rtti.TypeDescriptor2 = type { ptr, ptr, [3 x i8] }
%struct.D = type { i8 }

@"??_7type_info@@6B@" = external constant ptr
@"??_R0H@8" = linkonce_odr global %rtti.TypeDescriptor2 { ptr @"??_7type_info@@6B@", ptr null, [3 x i8] c".H\00" }

define i32 @"?f@@YAHXZ"() personality ptr @__CxxFrameHandler {
entry:
  %d = alloca %struct.D, align 1
  %e = alloca ptr, align 4
  %call = invoke ptr @"??0D@@QAA@XZ"(ptr %d)
          to label %cont unwind label %catch.dispatch

cont:
  invoke void @"?may_throw@@YAXH@Z"(i32 1)
          to label %cont2 unwind label %ehcleanup

cont2:
  call void @"??1D@@QAA@XZ"(ptr %d) nounwind
  call void @"?may_throw@@YAXH@Z"(i32 2)
  br label %return

ehcleanup:
  %0 = cleanuppad within none []
  call void @"??1D@@QAA@XZ"(ptr %d) nounwind [ "funclet"(token %0) ]
  cleanupret from %0 unwind label %catch.dispatch

catch.dispatch:
  %1 = catchswitch within none [label %catch] unwind to caller

catch:
  %2 = catchpad within %1 [ptr @"??_R0H@8", i32 8, ptr %e]
  %p = load ptr, ptr %e, align 4
  %v = load i32, ptr %p, align 4
  call void @"?may_throw@@YAXH@Z"(i32 %v) [ "funclet"(token %2) ]
  %p2 = load ptr, ptr %e, align 4
  %v2 = load i32, ptr %p2, align 4
  catchret from %2 to label %return

return:
  %r = phi i32 [ 0, %cont2 ], [ %v2, %catch ]
  ret i32 %r
}

declare ptr @"??0D@@QAA@XZ"(ptr returned)
declare void @"??1D@@QAA@XZ"(ptr)
declare void @"?may_throw@@YAXH@Z"(i32)
declare i32 @__CxxFrameHandler(...)

; States: 0 = try, 1 = D live in the try, 2 = catch. The EH context record
; is the first object below the callee-save area: State at $15+40, Rfp at $15+44 in this
; 64-byte frame, so EHContextDelta = 40 - 64.

; CHECK-LABEL: "?f@@YAHXZ":
; CHECK:      .seh_handler __CxxFrameHandler, @unwind, @except
; CHECK:      lda $30,-64($30)
; CHECK:      bis $30,$30,$15
; CHECK-NEXT: .seh_endprologue
; CHECK-NEXT: stl $15,44($15)
; CHECK-NEXT: lda $0,-1($31)
; CHECK-NEXT: zapnot $0,15,[[M1:\$[0-9]+]]
; CHECK-NEXT: lda [[ST:\$[0-9]+]],40($15)
; CHECK-NEXT: stl [[M1]],0([[ST]])
; CHECK:      stl $31,0([[ST]])
; CHECK:      bsr $26,$"??0D@@QAA@XZ"..ng
; CHECK:      lda [[ONE:\$[0-9]+]],1($31)
; CHECK:      stl [[ONE]],0(
; CHECK:      bsr $26,$"?may_throw@@YAXH@Z"..ng
; CHECK:      bsr $26,$"??1D@@QAA@XZ"..ng
; The plain call after the try is back in state -1.
; CHECK:      stl [[M1]],0(
; CHECK-NEXT: lda $16,2($31)
; CHECK-NEXT: bsr $26,$"?may_throw@@YAXH@Z"..ng
; CHECK:      $ehgcr_0_[[CONT:[0-9]+]]:

; FuncInfo for the NT Alpha runtime: no IP-to-state map, and EHContextDelta
; in place of the x86 ESTypeList.
; CHECK:      "$cppxdata$?f@@YAHXZ":
; CHECK-NEXT: .long 429065504 # MagicNumber
; CHECK-NEXT: .long 3 # MaxState
; CHECK-NEXT: .long "$stateUnwindMap$?f@@YAHXZ" # UnwindMap
; CHECK-NEXT: .long 1 # NumTryBlocks
; CHECK-NEXT: .long "$tryMap$?f@@YAHXZ" # TryBlockMap
; CHECK-NEXT: .long -24 # EHContextDelta
; CHECK-NEXT: .long 0 # IPMapEntries
; CHECK-NEXT: .long 0 # IPToStateXData
; CHECK-NEXT: "$stateUnwindMap$?f@@YAHXZ":
; CHECK-NEXT: .long -1 # ToState
; CHECK-NEXT: .long 0 # Action
; CHECK-NEXT: .long 0 # ToState
; CHECK-NEXT: .long "?dtor$[[DTOR:[0-9]+]]@?0??f@@YAHXZ@4HA" # Action
; CHECK-NEXT: .long -1 # ToState
; CHECK-NEXT: .long 0 # Action
; The Alpha TryBlockMapEntry has no CatchHigh.
; CHECK-NEXT: "$tryMap$?f@@YAHXZ":
; CHECK-NEXT: .long 0 # TryLow
; CHECK-NEXT: .long 1 # TryHigh
; CHECK-NEXT: .long 1 # NumCatches
; CHECK-NEXT: .long "$handlerMap$0$?f@@YAHXZ" # HandlerArray
; CHECK-NEXT: "$handlerMap$0$?f@@YAHXZ":
; CHECK-NEXT: .long 8 # Adjectives
; CHECK-NEXT: .long "??_R0H@8" # Type
; The catch object's displacement is from the establisher frame (the CFA):
; e lives at $15+28.
; CHECK-NEXT: .long -36 # CatchObjOffset
; CHECK-NEXT: .long "?catch$[[CATCH:[0-9]+]]@?0??f@@YAHXZ@4HA" # Handler
; CHECK-NEXT: .text
; CHECK-NEXT: .seh_endproc

; Funclets have no handler of their own: exceptions leaving them unwind
; through the runtime's wrapper to the parent. $1 is the parent's
; Rfp, its $15.
; CHECK:      "?dtor$[[DTOR]]@?0??f@@YAHXZ@4HA":
; CHECK-NEXT: .seh_proc "?dtor$[[DTOR]]@?0??f@@YAHXZ@4HA"
; CHECK-NOT:  .seh_handler
; CHECK:      lda $30,-64($30)
; CHECK:      bis $1,$1,$15
; CHECK-NEXT: .seh_endprologue
; CHECK-NOT:  stl
; CHECK:      lda $16,39($15)
; CHECK-NEXT: bsr $26,$"??1D@@QAA@XZ"..ng
; CHECK:      ret
; CHECK:      .seh_endproc

; The catch stores its state into the parent's EH context record and loads the
; continuation address before releasing its frame.
; CHECK:      "?catch$[[CATCH]]@?0??f@@YAHXZ@4HA":
; CHECK-NEXT: .seh_proc "?catch$[[CATCH]]@?0??f@@YAHXZ@4HA"
; CHECK-NOT:  .seh_handler
; CHECK:      lda $30,-64($30)
; CHECK:      bis $1,$1,$15
; CHECK-NEXT: .seh_endprologue
; CHECK:      lda $9,28($15)
; CHECK:      lda [[TWO:\$[0-9]+]],2($31)
; CHECK:      stl [[TWO]],0(
; CHECK-NEXT: bsr $26,$"?may_throw@@YAXH@Z"..ng
; CHECK:      ldah $0,$ehgcr_0_[[CONT]]($31)
; CHECK-NEXT: lda $0,$ehgcr_0_[[CONT]]($0)
; CHECK-NEXT: ldq $15,0($30)
; CHECK-NEXT: lda $30,64($30)
; CHECK-NEXT: ret
; CHECK:      .seh_endproc

; O0-LABEL: "?f@@YAHXZ":
; O0:       .seh_endprologue
; O0-NEXT:  stl $15,52($15)
; O0:       lda [[ST:\$[0-9]+]],48($15)
; O0:       "$cppxdata$?f@@YAHXZ":
; O0:       .long -16 # EHContextDelta
; O0:       bis $1,$1,$15

; Only the parent's .pdata record names a handler and its FuncInfo; the
; funclet records (at 0x14 and 0x28) leave both words zero.
; OBJ:      Section ({{[0-9]+}}) .pdata {
; OBJ-NEXT:   0x0 IMAGE_REL_ALPHA_REFLONG .text
; OBJ-NEXT:   0x4 IMAGE_REL_ALPHA_REFLONG .text
; OBJ-NEXT:   0x8 IMAGE_REL_ALPHA_REFLONG __CxxFrameHandler
; OBJ-NEXT:   0xC IMAGE_REL_ALPHA_REFLONG $cppxdata$?f@@YAHXZ
; OBJ-NEXT:   0x10 IMAGE_REL_ALPHA_REFLONG .text
; OBJ-NEXT:   0x14 IMAGE_REL_ALPHA_REFLONG .text
; OBJ-NEXT:   0x18 IMAGE_REL_ALPHA_REFLONG .text
; OBJ-NEXT:   0x24 IMAGE_REL_ALPHA_REFLONG .text
; OBJ-NEXT:   0x28 IMAGE_REL_ALPHA_REFLONG .text
; OBJ-NEXT:   0x2C IMAGE_REL_ALPHA_REFLONG .text
; OBJ-NEXT:   0x38 IMAGE_REL_ALPHA_REFLONG .text
; OBJ-NEXT: }
