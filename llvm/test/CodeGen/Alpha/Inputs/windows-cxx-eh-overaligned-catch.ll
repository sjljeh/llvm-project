; A catch object aligned beyond the stack alignment cannot sit at a fixed
; offset from the 16-byte-aligned establisher frame (windows-cxx-eh-realign.ll).

%rtti.TypeDescriptor2 = type { ptr, ptr, [3 x i8] }
@"??_7type_info@@6B@" = external constant ptr
@"??_R0H@8" = linkonce_odr global %rtti.TypeDescriptor2 { ptr @"??_7type_info@@6B@", ptr null, [3 x i8] c".H\00" }

define i32 @overaligned_catch() personality ptr @__CxxFrameHandler {
entry:
  %e = alloca [16 x i32], align 64
  invoke void @use(ptr null)
          to label %exit unwind label %dispatch

dispatch:
  %cs = catchswitch within none [label %catch] unwind to caller

catch:
  %cp = catchpad within %cs [ptr @"??_R0H@8", i32 0, ptr %e]
  %v = load i32, ptr %e, align 64
  catchret from %cp to label %exit

exit:
  ret i32 0
}

declare void @use(ptr)
declare i32 @__CxxFrameHandler(...)
