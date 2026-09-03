//===-- PPCWin32ABIInfo.h - Windows PowerPC ABI policy ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_POWERPC_PPCWIN32ABIINFO_H
#define LLVM_LIB_TARGET_POWERPC_PPCWIN32ABIINFO_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/CallingConvLower.h"
#include "llvm/CodeGen/SelectionDAGNodes.h"
#include "llvm/Support/Alignment.h"
#include <utility>

namespace llvm {

class SelectionDAG;

/// Calling-convention and variadic ABI policy for 32-bit Windows PowerPC.
class PPCWin32ABIInfo {
public:
  static constexpr unsigned WordSize = 4;
  static constexpr unsigned ReturnSaveOffset = 4;
  static constexpr unsigned TOCSaveOffset = 8;
  static constexpr unsigned ParameterAreaOffset = 24;
  static constexpr unsigned MinimumFrameSize = 56;

  static ArrayRef<MCPhysReg> getArgumentGPRs();
  static ArrayRef<MCPhysReg> getArgumentFPRs();
  static MCRegister getArgumentGPR(unsigned ParameterOffset);

  static unsigned getArgumentSlotSize(MVT LocVT);
  static Align getArgumentStackAlignment(MVT LocVT);
  static unsigned allocateArgument(unsigned &NextOffset, MVT LocVT);
  static unsigned ensureMinimumFrameSize(unsigned Size);

  static void analyzeFormalArguments(CCState &State,
                                     const SmallVectorImpl<ISD::InputArg> &Ins);
  static void analyzeCallOperands(CCState &State,
                                  const SmallVectorImpl<ISD::OutputArg> &Outs);
  static CCAssignFn *getReturnAssignFn(CallingConv::ID CallConv);

  static SDValue lowerVASTART(SDValue Op, SelectionDAG &DAG);
  static void homeVarArgRegisters(SDValue Chain, const SDLoc &DL,
                                  SelectionDAG &DAG, CCState &CCInfo,
                                  SmallVectorImpl<SDValue> &MemOps);

  static void duplicateVarArgFloatingPoint(
      SDValue Arg, unsigned ParameterOffset, bool IsLittleEndian, SDValue Chain,
      const SDLoc &DL, SelectionDAG &DAG,
      SmallVectorImpl<std::pair<unsigned, SDValue>> &RegsToPass,
      function_ref<void(SDValue, unsigned)> PassStackWord);

  static SDValue saveTOCBase(SDValue Chain, SDValue StackPtr, const SDLoc &DL,
                             SelectionDAG &DAG);

private:
  static bool assignArgument(unsigned ValNo, MVT ValVT, MVT LocVT,
                             CCValAssign::LocInfo LocInfo,
                             ISD::ArgFlagsTy ArgFlags, Type *OrigTy,
                             CCState &State);
};

} // end namespace llvm

#endif
