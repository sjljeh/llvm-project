//===- llvm/CodeGen/NTUWRuleTracker.h - NTUW rule synthesis -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Derives NTUW state rules (llvm/Support/NTRISCWinEH.h) for Alpha, MIPS and
// PowerPC functions from the instructions the AsmPrinter actually emits.
//
// The tracker sees every instruction just before the streamer emits it,
// executes it symbolically (registers and stack slots hold "the caller's
// value of register R", "the CFA plus k", a constant, or unknown), and after
// each instruction derives the rules that recover the caller's state. When
// the rules change, it emits them labelled after the instruction. A rule
// change after the prologue opens an epilogue scope, which ends after the
// next return. An instruction whose effect cannot be described is an error,
// never silently ignored.
//
// A record holds at most 65535 epilogue scopes and 65535 epilogue code
// slots. A function that would need more is split, at a block boundary in
// the body, into chained records (UNW_ChainInfo), which continue the
// parent's body state.
//
// Blocks are emitted in layout order, not control-flow order. The state at
// the end of each block is passed to its successors, and a block starts in
// the state its emitted predecessors agree on (the body's state if none has
// been emitted). After the prologue, a block whose rules differ from the
// body's, such as code outside a shrink-wrapped frame, is described by a
// scope that lasts while the rules differ. A branch that reaches a block
// already emitted with other rules is an error.
//
// Labels are placed after the whole machine instruction (a branch and its
// delay slot, or a pseudo expansion) or before the next instruction,
// whichever comes first, so a rule for an instruction in a delay slot takes
// effect after the slot, as NTUW specifies.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_NTUWRULETRACKER_H
#define LLVM_CODEGEN_NTUWRULETRACKER_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/AsmPrinterHandler.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/NTRISCWinEH.h"
#include <map>
#include <optional>

namespace llvm {

class AsmPrinter;
class MachineBasicBlock;
class MCInst;
class MCRegisterInfo;
class MCInstrInfo;

class LLVM_ABI NTUWRuleTracker : public AsmPrinterHandler,
                                 public MCStreamer::InstructionObserver {
public:
  enum class RegClass : uint8_t { GPR, FPR, SPR };

  struct Reg {
    RegClass Class = RegClass::GPR;
    uint8_t Num = 0;
    bool operator==(const Reg &O) const {
      return Class == O.Class && Num == O.Num;
    }
  };

  /// One step of an instruction's semantics. Steps apply in order.
  struct Effect {
    enum Kind : uint8_t {
      /// Dst becomes unknown.
      Clobber,
      /// Dst = Imm.
      SetConst,
      /// Dst = Src + Imm.
      AddImm,
      /// Dst = Src + Src2.
      AddReg,
      /// Dst = Src - Src2.
      SubReg,
      /// Dst = Src | Imm.
      OrImm,
      /// Dst = Src << Imm.
      ShlImm,
      /// Dst = Src (any classes).
      Copy,
      /// Dst = Size-byte memory at Src + Imm.
      Load,
      /// Size-byte memory at Src2 + Imm = Src.
      Store,
      /// Size-byte memory at Src2 + Src3 = Src.
      StoreIndexed,
      /// Dst = the word at 0(Src), where Src is the stack pointer and the
      /// ABI keeps the CFA there once the frame is allocated (the PowerPC
      /// back chain), even after a dynamic allocation.
      LoadBackChain,
    };
    Kind K;
    Reg Dst, Src, Src2, Src3;
    int64_t Imm = 0;
    uint8_t Size = 0;
  };

  struct Decoded {
    SmallVector<Effect, 4> Effects;
    /// The decoder described the instruction completely. Otherwise the
    /// tracker clobbers every register the instruction defines.
    bool Known = false;
    bool IsCall = false;
    /// The instruction returns to the caller or tail-calls out of the
    /// function, ending an epilogue.
    bool LeavesFunction = false;
    bool IsBranch = false;
  };

  NTUWRuleTracker(AsmPrinter &AP, NTRISCWinEH::Machine M, uint8_t FramePointer);
  ~NTUWRuleTracker() override;

  /// Describes Inst. Returns false to fall back to the instruction
  /// descriptor's definitions.
  virtual bool decode(const MCInst &Inst, Decoded &D) const = 0;
  /// Maps a target register to the NTUW namespace, or nothing for registers
  /// NTUW does not describe (such as volatile PowerPC condition fields).
  virtual std::optional<Reg> mapRegister(unsigned Reg) const = 0;

  // MCStreamer::InstructionObserver
  void observeInstruction(const MCInst &Inst,
                          const MCSubtargetInfo &STI) override;

  // AsmPrinterHandler
  void beginFunction(const MachineFunction *MF) override;
  void endFunction(const MachineFunction *MF) override;
  void endModule() override;
  void beginInstruction(const MachineInstr *MI) override;
  void endInstruction() override;
  void endFunclet() override;

protected:
  AsmPrinter &AP;
  NTRISCWinEH::Machine M;
  NTRISCWinEH::MachineInfo Info;

private:
  struct Value {
    enum Kind : uint8_t { Unknown, Const, Entry } K = Unknown;
    Reg R;
    int64_t Off = 0;
    bool operator==(const Value &O) const {
      return K == O.K && (K == Unknown || (K == Const && Off == O.Off) ||
                          (K == Entry && R == O.R && Off == O.Off));
    }
  };

  struct Slot {
    Value V;
    uint8_t Size;
  };

  struct Rules {
    uint8_t CFARegister = 0;
    int32_t CFAOffset = 0;
    /// Indexed by preserved-register key; see keyOf.
    SmallVector<NTRISCWinEH::RegisterRule, 0> Regs;
    bool operator==(const Rules &O) const {
      return CFARegister == O.CFARegister && CFAOffset == O.CFAOffset &&
             Regs == O.Regs;
    }
  };

  struct State {
    Value GPR[32];
    Value FPR[32];
    Value SPR[NTRISCWinEH::NumSPRs];
    /// Stack memory, keyed by offset from the CFA.
    std::map<int64_t, Slot> Memory;
    /// Registers an epilogue has reloaded with the caller's value. Until
    /// then a saved register's rule stays SAVE, even while it still holds
    /// the caller's value; afterwards it is SAME, so that the rule never
    /// points at a slot the epilogue is about to free.
    bool Restored[3][32] = {};
    Rules R;
  };

  enum class Mode { Outside, Prologue, Body, Epilogue };

  Value &valueOf(State &S, Reg R);
  Value read(const State &S, Reg R) const;
  void write(State &S, Reg R, Value V);
  void apply(State &S, const Effect &E);
  void clobberVolatiles(State &S);
  bool deriveRules(const State &S, Rules &Out, std::string &Error) const;
  void emitRuleChanges(const Rules &From, const Rules &To);
  void resetFrame(const WinEH::FrameInfo *Frame);
  void emitPending();
  void enterBodyIfPrologueEnded();
  /// The first frame of a chain of records: frames in one chain share the
  /// rules and block states.
  static const WinEH::FrameInfo *getChainRoot(const WinEH::FrameInfo *F);
  void splitIfRecordIsFull();
  static void meetInto(State &Into, const State &Other);
  void enterBlock(const MachineBasicBlock &MBB);
  void leaveBlock(const MachineBasicBlock &MBB);
  void passState(const MachineBasicBlock &Succ,
                 SmallPtrSetImpl<const MachineBasicBlock *> &Visited);
  [[noreturn]] void fail(const Twine &Message) const;

  SmallVector<Reg, 48> Preserved;
  uint8_t FramePointer;
  const WinEH::FrameInfo *Frame = nullptr;
  Mode CurMode = Mode::Outside;
  State Cur;
  State Body;
  struct PendingRule {
    unsigned Opcode;
    unsigned Register;
    int64_t Operand;
  };
  SmallVector<PendingRule, 8> Pending;
  bool EndEpilogueAfterInstruction = false;
  const MachineFunction *MF = nullptr;
  /// Epilogue scopes and code slots in the current record.
  unsigned RecordScopes = 0;
  unsigned RecordSlots = 0;

  /// The block being emitted, and the frame it belongs to.
  const MachineBasicBlock *CurBlock = nullptr;
  const WinEH::FrameInfo *BlockFrame = nullptr;
  /// The state emitted predecessors pass to a block not yet emitted.
  struct Incoming {
    const WinEH::FrameInfo *Frame;
    State S;
  };
  DenseMap<const MachineBasicBlock *, Incoming> IncomingStates;
  /// The rules each emitted block starts with.
  DenseMap<const MachineBasicBlock *,
           std::pair<const WinEH::FrameInfo *, Rules>>
      EntryRules;
};

} // namespace llvm

#endif
