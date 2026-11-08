//==-- AlphaFrameLowering.h - Define frame lowering for Alpha --*- C++ -*---==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//
//
//===----------------------------------------------------------------------===//

#ifndef ALPHA_FRAMEINFO_H
#define ALPHA_FRAMEINFO_H

#include "Alpha.h"
#include "llvm/CodeGen/TargetFrameLowering.h"

namespace llvm {
  class AlphaSubtarget;
  struct WinEHFuncInfo;

  class AlphaFrameLowering : public TargetFrameLowering {
  public:
    explicit AlphaFrameLowering(const AlphaSubtarget &)
        : TargetFrameLowering(StackGrowsDown, Align(16), 0) {}

    /// emitProlog/emitEpilog - These methods insert prolog and epilog code into
    /// the function.
    void emitPrologue(MachineFunction &MF,
                      MachineBasicBlock &MBB) const override;
    void emitEpilogue(MachineFunction &MF,
                      MachineBasicBlock &MBB) const override;

    /// Expand the NT_PROBE_STACK that emitPrologue placed before a Windows
    /// frame allocation into stores to every page of the new frame.
    void inlineStackProbe(MachineFunction &MF,
                          MachineBasicBlock &PrologMBB) const override;

    bool
    spillCalleeSavedRegisters(MachineBasicBlock &MBB,
                              MachineBasicBlock::iterator MI,
                              ArrayRef<CalleeSavedInfo> CSI,
                              const TargetRegisterInfo *TRI) const override;

    bool
    restoreCalleeSavedRegisters(MachineBasicBlock &MBB,
                                MachineBasicBlock::iterator MI,
                                MutableArrayRef<CalleeSavedInfo> CSI,
                                const TargetRegisterInfo *TRI) const override;

    MachineBasicBlock::iterator eliminateCallFramePseudoInstr(
        MachineFunction &MF, MachineBasicBlock &MBB,
        MachineBasicBlock::iterator MI) const override;

    void processFunctionBeforeFrameFinalized(
        MachineFunction &MF, RegScavenger *RS = nullptr) const override;

    bool hasFPImpl(const MachineFunction &MF) const override;
    bool hasReservedCallFrame(const MachineFunction &MF) const override;

    void determineCalleeSaves(MachineFunction &MF, BitVector &SavedRegs,
                              RegScavenger *RS) const override;

    StackOffset getFrameIndexReference(const MachineFunction &MF, int FI,
                                       Register &FrameReg) const override;

    /// The register that addresses frame object \p FI in the function body
    /// and in its funclets.
    Register getFrameBaseRegister(const MachineFunction &MF, int FI) const;

    /// Whether a realigned frame also needs a base pointer: $30 moves (alloca),
    /// or a funclet must address the parent's realigned objects.
    static bool needsBasePointerIfRealigned(const MachineFunction &MF);
    static bool hasBP(const MachineFunction &MF);
    /// The callee-saved register that holds the realigned frame base.
    static constexpr unsigned BasePtrReg = Alpha::R14;

    static bool isCalleeSaveSlot(const MachineFunction &MF, int FI);
    static bool isAlphaWinCXXEH(const MachineFunction &MF);
    /// Whether MF uses NTUW's __CxxFrameHandler3.
    static bool isNTUWCXXEH(const MachineFunction &MF);

  private:
    void createNTUWCXXEHObjects(MachineFunction &MF,
                                WinEHFuncInfo &EHInfo) const;
    void pinRuntimeFrameObjects(MachineFunction &MF,
                                WinEHFuncInfo &EHInfo) const;
    /// Offset from the allocated $30 of the slot that saves the caller's $15.
    int64_t getFPSaveOffset(const MachineFunction &MF) const;
    /// Dst = Src & -Align, with instructions the NT unwinder does not reverse.
    void emitAlignBase(MachineBasicBlock &MBB, MachineBasicBlock::iterator MBBI,
                       const DebugLoc &DL, Register Src, Register Dst,
                       uint64_t Align) const;

    /// Materialize a large frame size in $28 for an NT `addq $30, $28, $30`.
    void emitNTFrameSize(MachineBasicBlock &MBB,
                         MachineBasicBlock::iterator MBBI, const DebugLoc &DL,
                         long NumBytes) const;
  };

} // End llvm namespace

#endif
