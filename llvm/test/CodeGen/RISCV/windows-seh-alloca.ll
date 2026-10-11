; RUN: llc -mtriple=riscv64-w64-windows-gnu -mattr=+m,+a,+f,+d,+c,-relax -target-abi=lp64d < %s | FileCheck %s
; RUN: llc -mtriple=riscv64-w64-windows-gnu -mattr=+m,+a,+f,+d,+c,-relax -target-abi=lp64d -filetype=obj < %s -o %t.obj

; CHECK-LABEL: except_alloca:
; CHECK:         addi s0, sp, 128
; CHECK-NEXT:    .seh_set_cfa x8, 0
; CHECK-NEXT:    mv s1, sp
; CHECK-NEXT:    .seh_endprologue
; CHECK:         sd a0, 8(s1) # 8-byte Folded Spill
; CHECK:         sd a0, 0(s1) # 8-byte Folded Spill
; CHECK:         ld a0, 8(s1) # 8-byte Folded Reload
; CHECK-NEXT:    ld a1, 0(s1) # 8-byte Folded Reload
; CHECK:         .seh_startepilogue
; CHECK-NEXT:    addi sp, s0, -128
; CHECK-NEXT:    .seh_set_cfa x2, 128

; CHECK-LABEL: finally_alloca:
; CHECK:         addi s0, sp, 32
; CHECK-NEXT:    .seh_set_cfa x8, 0
; CHECK-NEXT:    mv s1, sp
; CHECK-NEXT:    .seh_endprologue
; CHECK:         .seh_startepilogue
; CHECK-NEXT:    addi sp, s0, -32

; CHECK-LABEL: "?dtor$5@?0?finally_alloca@4HA":
; CHECK:         mv s0, a1
; CHECK-NEXT:    mv s1, sp
; CHECK-NEXT:    .seh_endprologue
; CHECK:         .seh_startepilogue
; CHECK-NEXT:    .seh_set_cfa x2, 32
; CHECK-NOT:     addi sp, s0
; CHECK:         .seh_endepilogue

target datalayout = "e-m:w-p:64:64-i64:64-i128:128-n32:64-S128"
target triple = "riscv64-w64-windows-gnu"

; Function Attrs: nounwind uwtable
define dso_local void @except_alloca(i64 noundef %n) local_unnamed_addr #0 personality ptr @__C_specific_handler {
entry:
  %n.addr = alloca i64, align 8
  store i64 %n, ptr %n.addr, align 8
  %call = tail call i64 @value() #4
  %call1 = tail call i64 @value() #4
  %call2 = tail call i64 @value() #4
  %call3 = tail call i64 @value() #4
  %call4 = tail call i64 @value() #4
  %call5 = tail call i64 @value() #4
  %call6 = tail call i64 @value() #4
  %call7 = tail call i64 @value() #4
  %call8 = tail call i64 @value() #4
  %call9 = tail call i64 @value() #4
  %call10 = tail call i64 @value() #4
  %call11 = tail call i64 @value() #4
  invoke void @llvm.seh.try.begin()
          to label %invoke.cont unwind label %catch.dispatch

invoke.cont:                                      ; preds = %entry
  %n.addr.0.n.addr.0.n.addr.0.n.addr.0. = load volatile i64, ptr %n.addr, align 8
  %0 = alloca i8, i64 %n.addr.0.n.addr.0.n.addr.0.n.addr.0., align 16
  invoke void @use(ptr noundef nonnull %0) #5
          to label %invoke.cont12 unwind label %catch.dispatch

invoke.cont12:                                    ; preds = %invoke.cont
  invoke void @llvm.seh.try.end()
          to label %__try.cont unwind label %catch.dispatch

catch.dispatch:                                   ; preds = %invoke.cont12, %invoke.cont, %entry
  %1 = catchswitch within none [label %__except.ret] unwind to caller

__except.ret:                                     ; preds = %catch.dispatch
  %2 = catchpad within %1 [ptr @__filt_except_alloca]
  catchret from %2 to label %__except

__except:                                         ; preds = %__except.ret
  %3 = call i32 @llvm.eh.exceptioncode(token %2)
  call void @use(ptr noundef null) #4
  br label %__try.cont

__try.cont:                                       ; preds = %invoke.cont12, %__except
  call void @sink(i64 noundef %call, i64 noundef %call1, i64 noundef %call2, i64 noundef %call3, i64 noundef %call4, i64 noundef %call5, i64 noundef %call6, i64 noundef %call7, i64 noundef %call8, i64 noundef %call9, i64 noundef %call10, i64 noundef %call11) #4
  ret void
}

declare dso_local i64 @value() local_unnamed_addr #1

; Function Attrs: nounwind uwtable
define internal signext i32 @__filt_except_alloca(ptr nofree readnone captures(none) %exception_pointers, ptr nofree readnone captures(none) %frame_pointer) #0 {
entry:
  %call = tail call signext i32 @filter() #4
  ret i32 %call
}

declare dso_local signext i32 @filter() local_unnamed_addr #1

; Function Attrs: mustprogress nounwind willreturn memory(write)
declare void @llvm.seh.try.begin() #2

declare dso_local i32 @__C_specific_handler(...)

declare dso_local void @use(ptr noundef) local_unnamed_addr #1

; Function Attrs: mustprogress nounwind willreturn memory(write)
declare void @llvm.seh.try.end() #2

; Function Attrs: nofree nosync nounwind memory(none)
declare i32 @llvm.eh.exceptioncode(token) #3

declare dso_local void @sink(i64 noundef, i64 noundef, i64 noundef, i64 noundef, i64 noundef, i64 noundef, i64 noundef, i64 noundef, i64 noundef, i64 noundef, i64 noundef, i64 noundef) local_unnamed_addr #1

; Function Attrs: nounwind uwtable
define dso_local void @finally_alloca(i64 noundef %n) local_unnamed_addr #0 personality ptr @__C_specific_handler {
entry:
  %n.addr = alloca i64, align 8
  store i64 %n, ptr %n.addr, align 8
  invoke void @llvm.seh.try.begin()
          to label %invoke.cont unwind label %ehcleanup

invoke.cont:                                      ; preds = %entry
  %n.addr.0.n.addr.0.n.addr.0.n.addr.0. = load volatile i64, ptr %n.addr, align 8
  %0 = alloca i8, i64 %n.addr.0.n.addr.0.n.addr.0.n.addr.0., align 16
  invoke void @use(ptr noundef nonnull %0) #5
          to label %invoke.cont1 unwind label %ehcleanup

invoke.cont1:                                     ; preds = %invoke.cont
  invoke void @llvm.seh.try.end()
          to label %invoke.cont2 unwind label %ehcleanup

invoke.cont2:                                     ; preds = %invoke.cont1
  call void @finish() #4
  ret void

ehcleanup:                                        ; preds = %invoke.cont1, %invoke.cont, %entry
  %1 = cleanuppad within none []
  call void @finish() #4 [ "funclet"(token %1) ]
  cleanupret from %1 unwind to caller
}

declare dso_local void @finish() local_unnamed_addr #1

attributes #0 = { nounwind uwtable "no-trapping-math"="true" "probe-stack"="inline-asm" "stack-protector-buffer-size"="8" "target-cpu"="generic-rv64" "target-features"="+64bit,+a,+c,+d,+f,+i,+m,+zaamo,+zalrsc,+zca,+zcd,+zicsr,+zifencei,+zmmul,-b,-e,-experimental-p,-experimental-smcsps,-experimental-smehv,-experimental-smidctrl,-experimental-smijt,-experimental-smip,-experimental-smnip,-experimental-smpmpmt,-experimental-sscsps,-experimental-ssehv,-experimental-ssidctrl,-experimental-ssijt,-experimental-ssip,-experimental-ssnip,-experimental-svukte,-experimental-xqccmi,-experimental-xqccmt,-experimental-xsfmclic,-experimental-xsfsclic,-experimental-y,-experimental-zibi,-experimental-zicfilp,-experimental-zilx,-experimental-zvabd,-experimental-zvbc32e,-experimental-zvdot4a8i,-experimental-zvfbdota32f,-experimental-zvfbfa,-experimental-zvfofp8min,-experimental-zvfqwbdota8f,-experimental-zvfqwdota8f,-experimental-zvfwbdota16bf,-experimental-zvfwdota16bf,-experimental-zvkgs,-experimental-zvqwbdota16i,-experimental-zvqwbdota8i,-experimental-zvqwdota16i,-experimental-zvqwdota8i,-experimental-zvvfmm,-experimental-zvvmm,-experimental-zvvmtls,-experimental-zvvmttls,-experimental-zvzip,-h,-q,-relax,-sdext,-sdtrig,-sha,-shcounterenw,-shgatpa,-shlcofideleg,-shtvala,-shvsatpa,-shvstvala,-shvstvecd,-smaia,-smcdeleg,-smcntrpmf,-smcsrind,-smctr,-smdbltrp,-smepmp,-smmpm,-smnpm,-smpmpdeleg,-smrnmi,-smstateen,-ssaia,-ssccfg,-ssccptr,-sscofpmf,-sscounterenw,-sscsrind,-ssctr,-ssdbltrp,-ssnpm,-sspm,-sspmp,-sspmpen,-ssqosid,-ssstateen,-ssstrict,-sstc,-sstvala,-sstvecd,-ssu64xl,-supm,-svade,-svadu,-svbare,-svinval,-svnapot,-svpbmt,-svrsw60t59b,-svvptc,-v,-xaifet,-xandesbfhcvt,-xandesperf,-xandesvbfhcvt,-xandesvdot,-xandesvpackfph,-xandesvsinth,-xandesvsintload,-xcheriot,-xcvalu,-xcvbi,-xcvbitmanip,-xcvelw,-xcvmac,-xcvmem,-xcvsimd,-xmipscbop,-xmipscmov,-xmipsexectl,-xmipslsp,-xqccmp,-xqci,-xqcia,-xqciac,-xqcibi,-xqcibm,-xqcicli,-xqcicm,-xqcics,-xqcicsr,-xqciint,-xqciio,-xqcilb,-xqcili,-xqcilia,-xqcilo,-xqcilsm,-xqcisim,-xqcisls,-xqcisync,-xsfcease,-xsfmm128t,-xsfmm16t,-xsfmm32a,-xsfmm32a16f,-xsfmm32a32f,-xsfmm32a8f,-xsfmm32a8i,-xsfmm32t,-xsfmm64a64f,-xsfmm64t,-xsfmmbase,-xsfvcp,-xsfvfbfexp16e,-xsfvfexp16e,-xsfvfexp32e,-xsfvfexpa,-xsfvfexpa64e,-xsfvfnrclipxfqf,-xsfvfwmaccqqq,-xsfvqmaccdod,-xsfvqmaccqoq,-xsifivecdiscarddlone,-xsifivecflushdlone,-xsmtvdot,-xsmtvdotii,-xtheadba,-xtheadbb,-xtheadbs,-xtheadcmo,-xtheadcondmov,-xtheadfmemidx,-xtheadmac,-xtheadmemidx,-xtheadmempair,-xtheadsync,-xtheadvdot,-xwchc,-za128rs,-za64rs,-zabha,-zacas,-zalasr,-zama16b,-zawrs,-zba,-zbb,-zbc,-zbkb,-zbkc,-zbkx,-zbs,-zcb,-zce,-zcf,-zclsd,-zcmop,-zcmp,-zcmt,-zdinx,-zfa,-zfbfmin,-zfh,-zfhmin,-zfinx,-zhinx,-zhinxmin,-zic64b,-zicbom,-zicbop,-zicboz,-ziccamoa,-ziccamoc,-ziccid,-ziccif,-zicclsm,-ziccrse,-zicfiss,-zicntr,-zicond,-zihintntl,-zihintpause,-zihpm,-zilsd,-zimop,-zk,-zkn,-zknd,-zkne,-zknh,-zkr,-zks,-zksed,-zksh,-zkt,-ztso,-zvbb,-zvbc,-zve32f,-zve32x,-zve64d,-zve64f,-zve64x,-zvfbfmin,-zvfbfwma,-zvfh,-zvfhmin,-zvkb,-zvkg,-zvkn,-zvknc,-zvkned,-zvkng,-zvknha,-zvknhb,-zvks,-zvksc,-zvksed,-zvksg,-zvksh,-zvkt,-zvl1024b,-zvl128b,-zvl16384b,-zvl2048b,-zvl256b,-zvl32768b,-zvl32b,-zvl4096b,-zvl512b,-zvl64b,-zvl65536b,-zvl8192b" }
attributes #1 = { "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="generic-rv64" "target-features"="+64bit,+a,+c,+d,+f,+i,+m,+zaamo,+zalrsc,+zca,+zcd,+zicsr,+zifencei,+zmmul,-b,-e,-experimental-p,-experimental-smcsps,-experimental-smehv,-experimental-smidctrl,-experimental-smijt,-experimental-smip,-experimental-smnip,-experimental-smpmpmt,-experimental-sscsps,-experimental-ssehv,-experimental-ssidctrl,-experimental-ssijt,-experimental-ssip,-experimental-ssnip,-experimental-svukte,-experimental-xqccmi,-experimental-xqccmt,-experimental-xsfmclic,-experimental-xsfsclic,-experimental-y,-experimental-zibi,-experimental-zicfilp,-experimental-zilx,-experimental-zvabd,-experimental-zvbc32e,-experimental-zvdot4a8i,-experimental-zvfbdota32f,-experimental-zvfbfa,-experimental-zvfofp8min,-experimental-zvfqwbdota8f,-experimental-zvfqwdota8f,-experimental-zvfwbdota16bf,-experimental-zvfwdota16bf,-experimental-zvkgs,-experimental-zvqwbdota16i,-experimental-zvqwbdota8i,-experimental-zvqwdota16i,-experimental-zvqwdota8i,-experimental-zvvfmm,-experimental-zvvmm,-experimental-zvvmtls,-experimental-zvvmttls,-experimental-zvzip,-h,-q,-relax,-sdext,-sdtrig,-sha,-shcounterenw,-shgatpa,-shlcofideleg,-shtvala,-shvsatpa,-shvstvala,-shvstvecd,-smaia,-smcdeleg,-smcntrpmf,-smcsrind,-smctr,-smdbltrp,-smepmp,-smmpm,-smnpm,-smpmpdeleg,-smrnmi,-smstateen,-ssaia,-ssccfg,-ssccptr,-sscofpmf,-sscounterenw,-sscsrind,-ssctr,-ssdbltrp,-ssnpm,-sspm,-sspmp,-sspmpen,-ssqosid,-ssstateen,-ssstrict,-sstc,-sstvala,-sstvecd,-ssu64xl,-supm,-svade,-svadu,-svbare,-svinval,-svnapot,-svpbmt,-svrsw60t59b,-svvptc,-v,-xaifet,-xandesbfhcvt,-xandesperf,-xandesvbfhcvt,-xandesvdot,-xandesvpackfph,-xandesvsinth,-xandesvsintload,-xcheriot,-xcvalu,-xcvbi,-xcvbitmanip,-xcvelw,-xcvmac,-xcvmem,-xcvsimd,-xmipscbop,-xmipscmov,-xmipsexectl,-xmipslsp,-xqccmp,-xqci,-xqcia,-xqciac,-xqcibi,-xqcibm,-xqcicli,-xqcicm,-xqcics,-xqcicsr,-xqciint,-xqciio,-xqcilb,-xqcili,-xqcilia,-xqcilo,-xqcilsm,-xqcisim,-xqcisls,-xqcisync,-xsfcease,-xsfmm128t,-xsfmm16t,-xsfmm32a,-xsfmm32a16f,-xsfmm32a32f,-xsfmm32a8f,-xsfmm32a8i,-xsfmm32t,-xsfmm64a64f,-xsfmm64t,-xsfmmbase,-xsfvcp,-xsfvfbfexp16e,-xsfvfexp16e,-xsfvfexp32e,-xsfvfexpa,-xsfvfexpa64e,-xsfvfnrclipxfqf,-xsfvfwmaccqqq,-xsfvqmaccdod,-xsfvqmaccqoq,-xsifivecdiscarddlone,-xsifivecflushdlone,-xsmtvdot,-xsmtvdotii,-xtheadba,-xtheadbb,-xtheadbs,-xtheadcmo,-xtheadcondmov,-xtheadfmemidx,-xtheadmac,-xtheadmemidx,-xtheadmempair,-xtheadsync,-xtheadvdot,-xwchc,-za128rs,-za64rs,-zabha,-zacas,-zalasr,-zama16b,-zawrs,-zba,-zbb,-zbc,-zbkb,-zbkc,-zbkx,-zbs,-zcb,-zce,-zcf,-zclsd,-zcmop,-zcmp,-zcmt,-zdinx,-zfa,-zfbfmin,-zfh,-zfhmin,-zfinx,-zhinx,-zhinxmin,-zic64b,-zicbom,-zicbop,-zicboz,-ziccamoa,-ziccamoc,-ziccid,-ziccif,-zicclsm,-ziccrse,-zicfiss,-zicntr,-zicond,-zihintntl,-zihintpause,-zihpm,-zilsd,-zimop,-zk,-zkn,-zknd,-zkne,-zknh,-zkr,-zks,-zksed,-zksh,-zkt,-ztso,-zvbb,-zvbc,-zve32f,-zve32x,-zve64d,-zve64f,-zve64x,-zvfbfmin,-zvfbfwma,-zvfh,-zvfhmin,-zvkb,-zvkg,-zvkn,-zvknc,-zvkned,-zvkng,-zvknha,-zvknhb,-zvks,-zvksc,-zvksed,-zvksg,-zvksh,-zvkt,-zvl1024b,-zvl128b,-zvl16384b,-zvl2048b,-zvl256b,-zvl32768b,-zvl32b,-zvl4096b,-zvl512b,-zvl64b,-zvl65536b,-zvl8192b" }
attributes #2 = { mustprogress nounwind willreturn memory(write) }
attributes #3 = { nofree nosync nounwind memory(none) }
attributes #4 = { nounwind }
attributes #5 = { noinline }

!llvm.dbg.cu = !{!0}
!llvm.module.flags = !{!2, !3, !4, !5, !7, !8, !9}

!0 = distinct !DICompileUnit(language: DW_LANG_C11, file: !1, isOptimized: true, runtimeVersion: 0, emissionKind: NoDebug, splitDebugInlining: false, nameTableKind: None)
!1 = !DIFile(filename: "/private/tmp/claude-501/-Users-mac-working-dir-reactos-dev4-output-Clang-arm64-debug/acbc95f9-8039-4088-810e-a8c0a029c294/scratchpad/llvm/seh2.c", directory: "/Users/mac/working_dir/reactos-dev4/output-Clang-arm64-debug")
!2 = !{i32 2, !"Debug Info Version", i32 3}
!3 = !{i32 1, !"exception-model", !"wineh"}
!4 = !{i32 1, !"target-abi", !"lp64d"}
!5 = !{i32 6, !"riscv-isa", !6}
!6 = !{!"rv64i2p1_m2p0_a2p1_f2p2_d2p2_c2p0_zicsr2p0_zifencei2p0_zmmul1p0_zaamo1p0_zalrsc1p0_zca1p0_zcd1p0"}
!7 = !{i32 4, !"probe-stack", !"inline-asm"}
!8 = !{i32 7, !"uwtable", i32 2}
!9 = !{i32 8, !"SmallDataLimit", i32 0}
