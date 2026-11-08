//===-- Alpha.h - Top-level interface for Alpha representation --*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the entry points for global functions defined in the LLVM
// Alpha back-end.
//
//===----------------------------------------------------------------------===//

#ifndef TARGET_ALPHA_H
#define TARGET_ALPHA_H

#include "MCTargetDesc/AlphaMCTargetDesc.h"
#include "llvm/Target/TargetMachine.h"

namespace llvm {
  namespace Alpha {
    // These describe LDAx

    static const int IMM_LOW  = -32768;
    static const int IMM_HIGH = 32767;
    static const int IMM_MULT = 65536;
  }

  class AllocaInst;
  class AlphaTargetMachine;
  class MCContext;
  class MCSymbol;
  class StringRef;
  class FunctionPass;
  class formatted_raw_ostream;

  FunctionPass *createAlphaISelDag(AlphaTargetMachine &TM);
  FunctionPass *createAlphaPatternInstructionSelector(TargetMachine &TM);
  FunctionPass *createAlphaLLRPPass();
  FunctionPass *createAlphaBranchSelectionPass();
  FunctionPass *createAlphaWinEHStatePass();

  /// Whether \p AI is the Windows C++ EH context record that
  /// AlphaWinEHState allocates.
  bool isAlphaWinCXXEHContext(const AllocaInst &AI);

  /// `<FuncName>$parent_frame_mask`: the mask that llvm.eh.recoverfp applies
  /// to a Windows SEH parent's establisher frame to form its
  /// llvm.localaddress, -(frame alignment) when the parent's frame is
  /// realigned and -1 otherwise (AlphaFrameLowering, isRealigned).
  MCSymbol *getAlphaParentFrameMaskSymbol(MCContext &Ctx, StringRef FuncName);

} // end namespace llvm;

#endif
