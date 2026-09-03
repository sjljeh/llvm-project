//===-- PPCWin32ABIInfo.cpp - Windows PowerPC ABI policy ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "PPCWin32ABIInfo.h"
#include "PPCCallingConv.h"
#include "PPCMachineFunctionInfo.h"
#include "PPCRegisterInfo.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/SelectionDAG.h"
#include "llvm/Support/MathExtras.h"
#include <algorithm>

using namespace llvm;

ArrayRef<MCPhysReg> PPCWin32ABIInfo::getArgumentGPRs() {
  static const MCPhysReg GPRs[] = {PPC::R3, PPC::R4, PPC::R5, PPC::R6,
                                   PPC::R7, PPC::R8, PPC::R9, PPC::R10};
  return GPRs;
}

ArrayRef<MCPhysReg> PPCWin32ABIInfo::getArgumentFPRs() {
  static const MCPhysReg FPRs[] = {
      PPC::F1, PPC::F2, PPC::F3,  PPC::F4,  PPC::F5,  PPC::F6, PPC::F7,
      PPC::F8, PPC::F9, PPC::F10, PPC::F11, PPC::F12, PPC::F13};
  return FPRs;
}

MCRegister PPCWin32ABIInfo::getArgumentGPR(unsigned ParameterOffset) {
  if (ParameterOffset < ParameterAreaOffset ||
      ParameterOffset >= MinimumFrameSize || ParameterOffset % WordSize != 0)
    return MCRegister();
  return getArgumentGPRs()[(ParameterOffset - ParameterAreaOffset) / WordSize];
}

unsigned PPCWin32ABIInfo::getArgumentSlotSize(MVT LocVT) {
  return std::max<unsigned>(LocVT.getStoreSize(), WordSize);
}

Align PPCWin32ABIInfo::getArgumentStackAlignment(MVT LocVT) {
  return Align(getArgumentSlotSize(LocVT) >= 8 ? 8 : WordSize);
}

unsigned PPCWin32ABIInfo::allocateArgument(unsigned &NextOffset, MVT LocVT) {
  NextOffset = alignTo(NextOffset, getArgumentStackAlignment(LocVT));
  unsigned Offset = NextOffset;
  NextOffset += getArgumentSlotSize(LocVT);
  return Offset;
}

unsigned PPCWin32ABIInfo::ensureMinimumFrameSize(unsigned Size) {
  return std::max(Size, MinimumFrameSize);
}

bool PPCWin32ABIInfo::assignArgument(unsigned ValNo, MVT ValVT, MVT LocVT,
                                     CCValAssign::LocInfo LocInfo,
                                     ISD::ArgFlagsTy, Type *, CCState &State) {
  ArrayRef<MCPhysReg> GPRs = getArgumentGPRs();

  if (LocVT == MVT::i1) {
    LocVT = MVT::i32;
    LocInfo = CCValAssign::ZExt;
  }

  unsigned Size = getArgumentSlotSize(LocVT);
  unsigned Offset = State.AllocateStack(Size, getArgumentStackAlignment(LocVT));
  assert(Offset >= ParameterAreaOffset && "invalid NT parameter offset");

  unsigned FirstWord = (Offset - ParameterAreaOffset) / WordSize;
  unsigned NumWords = alignTo(Size, WordSize) / WordSize;
  for (unsigned I = 0; I != NumWords && FirstWord + I < GPRs.size(); ++I) {
    MCRegister Reg = State.AllocateReg(GPRs[FirstWord + I]);
    assert(Reg == GPRs[FirstWord + I] &&
           "NT parameter register allocated out of position");
    (void)Reg;
  }

  // Floating arguments use FPRs as their primary location, while the GPRs
  // above reserve the corresponding parameter words. Variadic calls fill
  // those GPR shadows in duplicateVarArgFloatingPoint().
  if (LocVT == MVT::f32 || LocVT == MVT::f64) {
    if (MCRegister Reg = State.AllocateReg(getArgumentFPRs())) {
      State.addLoc(CCValAssign::getReg(ValNo, ValVT, Reg, LocVT, LocInfo));
      return false;
    }
  } else if (Size == WordSize && FirstWord < GPRs.size()) {
    State.addLoc(
        CCValAssign::getReg(ValNo, ValVT, GPRs[FirstWord], LocVT, LocInfo));
    return false;
  }

  State.addLoc(CCValAssign::getMem(ValNo, ValVT, Offset, LocVT, LocInfo));
  return false;
}

void PPCWin32ABIInfo::analyzeFormalArguments(
    CCState &State, const SmallVectorImpl<ISD::InputArg> &Ins) {
  State.AllocateStack(ParameterAreaOffset, Align(WordSize));
  State.AnalyzeFormalArguments(Ins, assignArgument);
}

void PPCWin32ABIInfo::analyzeCallOperands(
    CCState &State, const SmallVectorImpl<ISD::OutputArg> &Outs) {
  State.AllocateStack(ParameterAreaOffset, Align(WordSize));
  State.AnalyzeCallOperands(Outs, assignArgument);
}

CCAssignFn *PPCWin32ABIInfo::getReturnAssignFn(CallingConv::ID CallConv) {
  return CallConv == CallingConv::Cold ? RetCC_PPC_Cold : RetCC_PPC;
}

SDValue PPCWin32ABIInfo::lowerVASTART(SDValue Op, SelectionDAG &DAG) {
  MachineFunction &MF = DAG.getMachineFunction();
  PPCFunctionInfo *FuncInfo = MF.getInfo<PPCFunctionInfo>();
  EVT PtrVT = DAG.getTargetLoweringInfo().getPointerTy(MF.getDataLayout());
  SDLoc DL(Op);

  SDValue Frame = DAG.getFrameIndex(FuncInfo->getVarArgsFrameIndex(), PtrVT);
  const Value *SV = cast<SrcValueSDNode>(Op.getOperand(2))->getValue();
  return DAG.getStore(Op.getOperand(0), DL, Frame, Op.getOperand(1),
                      MachinePointerInfo(SV));
}

void PPCWin32ABIInfo::homeVarArgRegisters(SDValue Chain, const SDLoc &DL,
                                          SelectionDAG &DAG, CCState &CCInfo,
                                          SmallVectorImpl<SDValue> &MemOps) {
  MachineFunction &MF = DAG.getMachineFunction();
  MachineFrameInfo &MFI = MF.getFrameInfo();
  PPCFunctionInfo *FuncInfo = MF.getInfo<PPCFunctionInfo>();
  EVT PtrVT = DAG.getTargetLoweringInfo().getPointerTy(MF.getDataLayout());

  FuncInfo->setVarArgsFrameIndex(
      MFI.CreateFixedObject(WordSize, CCInfo.getStackSize(), true));

  // The caller supplies register shadows for floating variadic arguments, so
  // homing r3-r10 creates one addressable stream for every argument kind.
  for (auto [Index, GPArgReg] : llvm::enumerate(getArgumentGPRs())) {
    Register VReg = MF.getRegInfo().getLiveInVirtReg(GPArgReg);
    if (!VReg)
      VReg = MF.addLiveIn(GPArgReg, &PPC::GPRCRegClass);

    int FI =
        MFI.CreateFixedObject(WordSize, ParameterAreaOffset + Index * WordSize,
                              /*IsImmutable=*/false);
    SDValue FIN = DAG.getFrameIndex(FI, PtrVT);
    SDValue Val = DAG.getCopyFromReg(Chain, DL, VReg, PtrVT);
    MemOps.push_back(
        DAG.getStore(Val.getValue(1), DL, Val, FIN, MachinePointerInfo()));
  }
}

void PPCWin32ABIInfo::duplicateVarArgFloatingPoint(
    SDValue Arg, unsigned ParameterOffset, bool IsLittleEndian, SDValue Chain,
    const SDLoc &DL, SelectionDAG &DAG,
    SmallVectorImpl<std::pair<unsigned, SDValue>> &RegsToPass,
    function_ref<void(SDValue, unsigned)> PassStackWord) {
  assert((Arg.getValueType() == MVT::f32 || Arg.getValueType() == MVT::f64) &&
         "expected a floating-point argument");

  SmallVector<SDValue, 2> Words;
  if (Arg.getValueType() == MVT::f32) {
    Words.push_back(DAG.getBitcast(MVT::i32, Arg));
  } else {
    SDValue Bits = DAG.getBitcast(MVT::i64, Arg);
    Words.push_back(DAG.getNode(ISD::EXTRACT_ELEMENT, DL, MVT::i32, Bits,
                                DAG.getConstant(0, DL, MVT::i32)));
    Words.push_back(DAG.getNode(ISD::EXTRACT_ELEMENT, DL, MVT::i32, Bits,
                                DAG.getConstant(1, DL, MVT::i32)));
    if (!IsLittleEndian)
      std::swap(Words[0], Words[1]);
  }

  // A variadic float remains in its FPR and is also copied to the GPR or stack
  // words that a variadic callee will expose through va_list.
  for (auto [WordIndex, Word] : llvm::enumerate(Words)) {
    unsigned Offset = ParameterOffset + WordIndex * WordSize;
    if (MCRegister Reg = getArgumentGPR(Offset))
      RegsToPass.emplace_back(Reg.id(), Word);
    else
      PassStackWord(Word, Offset);
  }
}

SDValue PPCWin32ABIInfo::saveTOCBase(SDValue Chain, SDValue StackPtr,
                                     const SDLoc &DL, SelectionDAG &DAG) {
  SDValue TOCVal = DAG.getCopyFromReg(Chain, DL, PPC::R2, MVT::i32);
  SDValue TOCOff = DAG.getIntPtrConstant(TOCSaveOffset, DL);
  SDValue TOCSlot = DAG.getNode(ISD::ADD, DL, MVT::i32, StackPtr, TOCOff);
  return DAG.getStore(
      TOCVal.getValue(1), DL, TOCVal, TOCSlot,
      MachinePointerInfo::getStack(DAG.getMachineFunction(), TOCSaveOffset));
}
