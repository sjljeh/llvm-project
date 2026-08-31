//===-- AlphaISelLowering.cpp - Alpha DAG Lowering Implementation ---------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the AlphaISelLowering class.
//
//===----------------------------------------------------------------------===//

#include "AlphaISelLowering.h"
#include "AlphaMachineFunctionInfo.h"
#include "AlphaTargetMachine.h"
#include "llvm/CodeGen/CallingConvLower.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/PseudoSourceValue.h"
#include "llvm/CodeGen/SelectionDAG.h"
#include "llvm/CodeGen/TargetLoweringObjectFileImpl.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include <iterator>
using namespace llvm;

/// AddLiveIn - This helper function adds the specified physical register to the
/// MachineFunction as a live in value.  It also creates a corresponding virtual
/// register for it.
static unsigned AddLiveIn(MachineFunction &MF, unsigned PReg,
                          const TargetRegisterClass *RC) {
  assert(RC->contains(PReg) && "Not the correct regclass!");
  unsigned VReg = MF.getRegInfo().createVirtualRegister(RC);
  MF.getRegInfo().addLiveIn(PReg, VReg);
  return VReg;
}

static bool has32BitPointers(const SelectionDAG &DAG) {
  return DAG.getDataLayout().getPointerSizeInBits() == 32;
}

static bool isWindowsAlpha(const SelectionDAG &DAG) {
  return DAG.getMachineFunction().getTarget().getTargetTriple().isOSWindows();
}

static bool shouldSignExtendWindowsI32(const SelectionDAG &DAG, EVT FromVT,
                                       EVT ToVT) {
  return isWindowsAlpha(DAG) && FromVT == MVT::i32 && ToVT == MVT::i64;
}

static SDValue signExtendWindowsI32(SelectionDAG &DAG, const SDLoc &DL,
                                    SDValue Value) {
  if (Value.getValueType() == MVT::i32)
    return DAG.getNode(ISD::SIGN_EXTEND, DL, MVT::i64, Value);

  SDValue Low = DAG.getNode(ISD::TRUNCATE, DL, MVT::i32, Value);
  return DAG.getNode(ISD::SIGN_EXTEND, DL, MVT::i64, Low);
}

static SDValue loadPointer(SelectionDAG &DAG, const SDLoc &DL, SDValue Chain,
                           SDValue Ptr, MachinePointerInfo PtrInfo) {
  if (has32BitPointers(DAG))
    return DAG.getExtLoad(ISD::ZEXTLOAD, DL, MVT::i64, Chain, Ptr, PtrInfo,
                          MVT::i32);
  return DAG.getLoad(MVT::i64, DL, Chain, Ptr, PtrInfo);
}

static SDValue storePointer(SelectionDAG &DAG, const SDLoc &DL, SDValue Chain,
                            SDValue Value, SDValue Ptr,
                            MachinePointerInfo PtrInfo) {
  if (has32BitPointers(DAG))
    return DAG.getTruncStore(Chain, DL, Value, Ptr, PtrInfo, MVT::i32);
  return DAG.getStore(Chain, DL, Value, Ptr, PtrInfo);
}

AlphaTargetLowering::AlphaTargetLowering(const TargetMachine &TM,
                                         const AlphaSubtarget &STI)
  : TargetLowering(TM, STI) {
  // Set up the TargetLowering object.
  //I am having problems with shr n i8 1
  setBooleanContents(ZeroOrOneBooleanContent);
  setBooleanVectorContents(ZeroOrOneBooleanContent); // FIXME: Is this correct?

  setLibcallImpl(RTLIB::MEMCPY, RTLIB::impl_memcpy);
  setLibcallImpl(RTLIB::MEMMOVE, RTLIB::impl_memmove);
  setLibcallImpl(RTLIB::MEMSET, RTLIB::impl_memset);

  addRegisterClass(MVT::i64, &Alpha::GPRCRegClass);
  addRegisterClass(MVT::f64, &Alpha::F8RCRegClass);
  addRegisterClass(MVT::f32, &Alpha::F4RCRegClass);

  // We want to custom lower some of our intrinsics.
  setOperationAction(ISD::INTRINSIC_WO_CHAIN, MVT::Other, Custom);

  setLoadExtAction(ISD::EXTLOAD, MVT::i64, MVT::i1, Promote);
  setLoadExtAction(ISD::EXTLOAD, MVT::f64, MVT::f32, Expand);

  setLoadExtAction(ISD::ZEXTLOAD, MVT::i64, MVT::i1, Promote);
  setLoadExtAction(ISD::ZEXTLOAD, MVT::i64, MVT::i32, Expand);

  setLoadExtAction(ISD::SEXTLOAD, MVT::i64, MVT::i1, Promote);
  setLoadExtAction(ISD::SEXTLOAD, MVT::i64, MVT::i8, Expand);
  setLoadExtAction(ISD::SEXTLOAD, MVT::i64, MVT::i16, Expand);

  setTruncStoreAction(MVT::f64, MVT::f32, Expand);

  //  setOperationAction(ISD::BRIND,        MVT::Other,   Expand);
  setOperationAction(ISD::BR_JT,        MVT::Other, Expand);
  setOperationAction(ISD::BR_CC,        MVT::Other, Expand);
  setOperationAction(ISD::BR_CC,        MVT::i64,   Expand);
  setOperationAction(ISD::BR_CC,        MVT::f32,   Expand);
  setOperationAction(ISD::BR_CC,        MVT::f64,   Expand);
  setOperationAction(ISD::SELECT_CC,    MVT::Other, Expand);
  setOperationAction(ISD::SELECT_CC,    MVT::i64,   Expand);
  setOperationAction(ISD::SELECT_CC,    MVT::f32,   Expand);
  setOperationAction(ISD::SELECT_CC,    MVT::f64,   Expand);

  setOperationAction(ISD::SIGN_EXTEND_INREG, MVT::i1, Expand);

  setOperationAction(ISD::FREM, MVT::f32, Expand);
  setOperationAction(ISD::FREM, MVT::f64, Expand);

  setOperationAction(ISD::UINT_TO_FP, MVT::i64, Expand);
  setOperationAction(ISD::SINT_TO_FP, MVT::i64, Custom);
  setOperationAction(ISD::FP_TO_UINT, MVT::i64, Expand);
  setOperationAction(ISD::FP_TO_SINT, MVT::i64, Custom);

  if (STI.hasCT()) {
    setOperationAction(ISD::CTPOP, MVT::i64, Legal);
    setOperationAction(ISD::CTTZ, MVT::i64, Legal);
    setOperationAction(ISD::CTLZ, MVT::i64, Legal);
  } else {
    setOperationAction(ISD::CTPOP, MVT::i64, Expand);
    setOperationAction(ISD::CTTZ, MVT::i64, Expand);
    setOperationAction(ISD::CTLZ, MVT::i64, Expand);
  }
  setOperationAction(ISD::BSWAP    , MVT::i64, Expand);
  setOperationAction(ISD::ROTL     , MVT::i64, Expand);
  setOperationAction(ISD::ROTR     , MVT::i64, Expand);

  setOperationAction(ISD::SREM     , MVT::i64, Custom);
  setOperationAction(ISD::UREM     , MVT::i64, Custom);
  setOperationAction(ISD::SDIV     , MVT::i64, Custom);
  setOperationAction(ISD::UDIV     , MVT::i64, Custom);

  setOperationAction(ISD::ADDC     , MVT::i64, Expand);
  setOperationAction(ISD::ADDE     , MVT::i64, Expand);
  setOperationAction(ISD::SUBC     , MVT::i64, Expand);
  setOperationAction(ISD::SUBE     , MVT::i64, Expand);

  setOperationAction(ISD::UMUL_LOHI, MVT::i64, Expand);
  setOperationAction(ISD::SMUL_LOHI, MVT::i64, Expand);

  setOperationAction(ISD::SRL_PARTS, MVT::i64, Custom);
  setOperationAction(ISD::SRA_PARTS, MVT::i64, Expand);
  setOperationAction(ISD::SHL_PARTS, MVT::i64, Expand);

  // We don't support sin/cos/sqrt/pow
  setOperationAction(ISD::FSIN , MVT::f64, Expand);
  setOperationAction(ISD::FCOS , MVT::f64, Expand);
  setOperationAction(ISD::FSIN , MVT::f32, Expand);
  setOperationAction(ISD::FCOS , MVT::f32, Expand);

  setOperationAction(ISD::FSQRT, MVT::f64, Expand);
  setOperationAction(ISD::FSQRT, MVT::f32, Expand);

  setOperationAction(ISD::FPOW , MVT::f32, Expand);
  setOperationAction(ISD::FPOW , MVT::f64, Expand);

  setOperationAction(ISD::FMA, MVT::f64, Expand);
  setOperationAction(ISD::FMA, MVT::f32, Expand);

  setOperationAction(ISD::SETCC, MVT::f32, Promote);

  setOperationAction(ISD::BITCAST, MVT::f32, Promote);

  setOperationAction(ISD::EH_LABEL, MVT::Other, Expand);

  // Not implemented yet.
  setOperationAction(ISD::STACKSAVE, MVT::Other, Expand);
  setOperationAction(ISD::STACKRESTORE, MVT::Other, Expand);
  setOperationAction(ISD::DYNAMIC_STACKALLOC, MVT::i64, Expand);

  // We want to legalize GlobalAddress and ConstantPool and
  // ExternalSymbols nodes into the appropriate instructions to
  // materialize the address.
  setOperationAction(ISD::GlobalAddress,  MVT::i64, Custom);
  setOperationAction(ISD::ConstantPool,   MVT::i64, Custom);
  setOperationAction(ISD::ExternalSymbol, MVT::i64, Custom);
  setOperationAction(ISD::GlobalTLSAddress, MVT::i64, Custom);

  setOperationAction(ISD::VASTART, MVT::Other, Custom);
  setOperationAction(ISD::VAEND,   MVT::Other, Expand);
  setOperationAction(ISD::VACOPY,  MVT::Other, Custom);
  setOperationAction(ISD::VAARG,   MVT::Other, Custom);
  setOperationAction(ISD::VAARG,   MVT::i32,   Custom);

  setOperationAction(ISD::JumpTable, MVT::i64, Custom);
  setOperationAction(ISD::JumpTable, MVT::i32, Custom);

  setOperationAction(ISD::ATOMIC_LOAD,  MVT::i32, Expand);
  setOperationAction(ISD::ATOMIC_STORE, MVT::i32, Expand);

  setStackPointerRegisterToSaveRestore(Alpha::R30);

  // Alpha fetches and slots instructions in naturally aligned 16-byte groups.
  // Keep loop headers and function entries on a fetch boundary.
  setMinFunctionAlignment(Align(16));
  setPrefFunctionAlignment(Align(16));
  setPrefLoopAlignment(Align(16));

  computeRegisterProperties(STI.getRegisterInfo());
}

EVT AlphaTargetLowering::getSetCCResultType(const DataLayout &DL,
                                            LLVMContext &Context,
                                            EVT VT) const {
  return MVT::i64;
}

const char *AlphaTargetLowering::getTargetNodeName(unsigned Opcode) const {
  switch (Opcode) {
  default: return 0;
  case AlphaISD::CVTQT_: return "Alpha::CVTQT_";
  case AlphaISD::CVTQS_: return "Alpha::CVTQS_";
  case AlphaISD::CVTTQ_: return "Alpha::CVTTQ_";
  case AlphaISD::GPRelHi: return "Alpha::GPRelHi";
  case AlphaISD::GPRelLo: return "Alpha::GPRelLo";
  case AlphaISD::RelLit: return "Alpha::RelLit";
  case AlphaISD::TPRelHi:
    return "Alpha::TPRelHi";
  case AlphaISD::TPRelLo:
    return "Alpha::TPRelLo";
  case AlphaISD::GOTTPRel:
    return "Alpha::GOTTPRel";
  case AlphaISD::RDUNIQUE:
    return "Alpha::RDUNIQUE";
  case AlphaISD::GlobalRetAddr: return "Alpha::GlobalRetAddr";
  case AlphaISD::CALL:   return "Alpha::CALL";
  case AlphaISD::DivCall: return "Alpha::DivCall";
  case AlphaISD::RET_FLAG: return "Alpha::RET_FLAG";
  case AlphaISD::COND_BRANCH_I: return "Alpha::COND_BRANCH_I";
  case AlphaISD::COND_BRANCH_F: return "Alpha::COND_BRANCH_F";
  }
}

static SDValue LowerJumpTable(SDValue Op, SelectionDAG &DAG) {
  EVT PtrVT = Op.getValueType();
  JumpTableSDNode *JT = cast<JumpTableSDNode>(Op);
  SDValue JTI = DAG.getTargetJumpTable(JT->getIndex(), PtrVT);
  // FIXME there isn't really any debug info here
  SDLoc dl(Op);

  SDValue Hi = DAG.getNode(AlphaISD::GPRelHi,  dl, MVT::i64, JTI,
                             DAG.getGLOBAL_OFFSET_TABLE(MVT::i64));
  SDValue Lo = DAG.getNode(AlphaISD::GPRelLo, dl, MVT::i64, JTI, Hi);
  return Lo;
}

//http://www.cs.arizona.edu/computer.help/policy/DIGITAL_unix/
//AA-PY8AC-TET1_html/callCH3.html#BLOCK21

//For now, just use variable size stack frame format

//In a standard call, the first six items are passed in registers $16
//- $21 and/or registers $f16 - $f21. (See Section 4.1.2 for details
//of argument-to-register correspondence.) The remaining items are
//collected in a memory argument list that is a naturally aligned
//array of quadwords. In a standard call, this list, if present, must
//be passed at 0(SP).
//7 ... n         0(SP) ... (n-7)*8(SP)

// //#define FP    $15
// //#define RA    $26
// //#define PV    $27
// //#define GP    $29
// //#define SP    $30

#define GET_CALLING_CONV_IMPL
#include "AlphaGenCallingConv.inc"

SDValue AlphaTargetLowering::LowerCall(
    TargetLowering::CallLoweringInfo &CLI,
    SmallVectorImpl<SDValue> &InVals) const {
  SDValue Chain = CLI.Chain;
  SDValue Callee = CLI.Callee;
  CallingConv::ID CallConv = CLI.CallConv;
  bool isVarArg = CLI.IsVarArg;
  const SmallVectorImpl<ISD::OutputArg> &Outs = CLI.Outs;
  const SmallVectorImpl<SDValue> &OutVals = CLI.OutVals;
  const SmallVectorImpl<ISD::InputArg> &Ins = CLI.Ins;
  const SDLoc &dl = CLI.DL;
  SelectionDAG &DAG = CLI.DAG;

  // Alpha target does not yet support tail call optimization.
  CLI.IsTailCall = false;

  // Analyze operands of the call, assigning locations to each operand.
  SmallVector<CCValAssign, 16> ArgLocs;
  CCState CCInfo(CallConv, isVarArg, DAG.getMachineFunction(),
		 ArgLocs, *DAG.getContext());

  CCInfo.AnalyzeCallOperands(Outs, CC_Alpha);

  unsigned NumBytes = CCInfo.getStackSize();

  Chain = DAG.getCALLSEQ_START(Chain, NumBytes, 0, dl);

  SmallVector<std::pair<unsigned, SDValue>, 4> RegsToPass;
  SmallVector<SDValue, 12> MemOpChains;
  SDValue StackPtr;

  // Walk the register/memloc assignments, inserting copies/loads.
  for (unsigned i = 0, e = ArgLocs.size(); i != e; ++i) {
    CCValAssign &VA = ArgLocs[i];

    SDValue Arg = OutVals[i];

    if (shouldSignExtendWindowsI32(DAG, Outs[i].ArgVT, Arg.getValueType()))
      Arg = signExtendWindowsI32(DAG, dl, Arg);

    // Promote the value if needed.
    switch (VA.getLocInfo()) {
      default: llvm_unreachable("Unknown loc info!");
      case CCValAssign::Full: break;
      case CCValAssign::SExt:
        Arg = DAG.getNode(ISD::SIGN_EXTEND, dl, VA.getLocVT(), Arg);
        break;
      case CCValAssign::ZExt:
        Arg = DAG.getNode(ISD::ZERO_EXTEND, dl, VA.getLocVT(), Arg);
        break;
      case CCValAssign::AExt:
        Arg = DAG.getNode(
            shouldSignExtendWindowsI32(DAG, VA.getValVT(), VA.getLocVT())
                ? ISD::SIGN_EXTEND
                : ISD::ANY_EXTEND,
            dl, VA.getLocVT(), Arg);
        break;
    }

    // Arguments that can be passed on register must be kept at RegsToPass
    // vector
    if (VA.isRegLoc()) {
      RegsToPass.push_back(std::make_pair(VA.getLocReg(), Arg));

    } else {
      assert(VA.isMemLoc());

      if (StackPtr.getNode() == 0)
        StackPtr = DAG.getCopyFromReg(Chain, dl, Alpha::R30, MVT::i64);

      SDValue PtrOff = DAG.getNode(ISD::ADD, dl,
                                    getPointerTy(DAG.getDataLayout()),
                                    StackPtr,
                                    DAG.getIntPtrConstant(VA.getLocMemOffset(),
                                                          dl));

      MemOpChains.push_back(DAG.getStore(Chain, dl, Arg, PtrOff,
                                         MachinePointerInfo()));
    }
  }

  // Transform all store nodes into one single node because all store nodes are
  // independent of each other.
  if (!MemOpChains.empty())
    Chain = DAG.getNode(ISD::TokenFactor, dl, MVT::Other, MemOpChains);

  // Build a sequence of copy-to-reg nodes chained together with token chain and
  // flag operands which copy the outgoing args into registers.  The InFlag in
  // necessary since all emitted instructions must be stuck together.
  SDValue InFlag;
  for (unsigned i = 0, e = RegsToPass.size(); i != e; ++i) {
    Chain = DAG.getCopyToReg(Chain, dl, RegsToPass[i].first,
                             RegsToPass[i].second, InFlag);
    InFlag = Chain.getValue(1);
  }

  // Returns a chain & a flag for retval copy to use.
  SDVTList NodeTys = DAG.getVTList(MVT::Other, MVT::Glue);
  SmallVector<SDValue, 8> Ops;
  Ops.push_back(Chain);

  if (GlobalAddressSDNode *G = dyn_cast<GlobalAddressSDNode>(Callee))
    Callee = DAG.getTargetGlobalAddress(G->getGlobal(), dl, MVT::i64,
                                        G->getOffset());
  else if (ExternalSymbolSDNode *E = dyn_cast<ExternalSymbolSDNode>(Callee))
    Callee = DAG.getTargetExternalSymbol(E->getSymbol(), MVT::i64);

  Ops.push_back(Callee);

  // Add argument registers to the end of the list so that they are
  // known live into the call.
  for (unsigned i = 0, e = RegsToPass.size(); i != e; ++i)
    Ops.push_back(DAG.getRegister(RegsToPass[i].first,
                                  RegsToPass[i].second.getValueType()));

  if (InFlag.getNode())
    Ops.push_back(InFlag);

  Chain = DAG.getNode(AlphaISD::CALL, dl, NodeTys, Ops);
  InFlag = Chain.getValue(1);

  // Create the CALLSEQ_END node.
  Chain = DAG.getCALLSEQ_END(Chain, NumBytes, 0, InFlag, dl);
  InFlag = Chain.getValue(1);

  // Handle result values, copying them out of physregs into vregs that we
  // return.
  return LowerCallResult(Chain, InFlag, CallConv, isVarArg,
                         Ins, dl, DAG, InVals);
}

/// LowerCallResult - Lower the result values of a call into the
/// appropriate copies out of appropriate physical registers.
///
SDValue
AlphaTargetLowering::LowerCallResult(SDValue Chain, SDValue InFlag,
                                     CallingConv::ID CallConv, bool isVarArg,
                                     const SmallVectorImpl<ISD::InputArg> &Ins,
                                     const SDLoc &dl, SelectionDAG &DAG,
                                     SmallVectorImpl<SDValue> &InVals) const {

  // Assign locations to each value returned by this call.
  SmallVector<CCValAssign, 16> RVLocs;
  CCState CCInfo(CallConv, isVarArg, DAG.getMachineFunction(),
		 RVLocs, *DAG.getContext());

  CCInfo.AnalyzeCallResult(Ins, RetCC_Alpha);

  // Copy all of the result registers out of their specified physreg.
  for (unsigned i = 0; i != RVLocs.size(); ++i) {
    CCValAssign &VA = RVLocs[i];

    Chain = DAG.getCopyFromReg(Chain, dl, VA.getLocReg(),
                               VA.getLocVT(), InFlag).getValue(1);
    SDValue RetValue = Chain.getValue(0);
    InFlag = Chain.getValue(2);

    // If this is an 8/16/32-bit value, it is really passed promoted to 64
    // bits. Insert an assert[sz]ext to capture this, then truncate to the
    // right size.
    if (VA.getLocInfo() == CCValAssign::SExt)
      RetValue = DAG.getNode(ISD::AssertSext, dl, VA.getLocVT(), RetValue,
                             DAG.getValueType(VA.getValVT()));
    else if (VA.getLocInfo() == CCValAssign::ZExt)
      RetValue = DAG.getNode(ISD::AssertZext, dl, VA.getLocVT(), RetValue,
                             DAG.getValueType(VA.getValVT()));

    if (VA.getLocInfo() != CCValAssign::Full)
      RetValue = DAG.getNode(ISD::TRUNCATE, dl, VA.getValVT(), RetValue);

    InVals.push_back(RetValue);
  }

  return Chain;
}

SDValue
AlphaTargetLowering::LowerFormalArguments(SDValue Chain,
                                          CallingConv::ID CallConv, bool isVarArg,
                                          const SmallVectorImpl<ISD::InputArg>
                                            &Ins,
                                          const SDLoc &dl, SelectionDAG &DAG,
                                          SmallVectorImpl<SDValue> &InVals)
                                            const {

  MachineFunction &MF = DAG.getMachineFunction();
  MachineFrameInfo &MFI = MF.getFrameInfo();
  AlphaMachineFunctionInfo *FuncInfo = MF.getInfo<AlphaMachineFunctionInfo>();

  unsigned args_int[] = {
    Alpha::R16, Alpha::R17, Alpha::R18, Alpha::R19, Alpha::R20, Alpha::R21};
  unsigned args_float[] = {
    Alpha::F16, Alpha::F17, Alpha::F18, Alpha::F19, Alpha::F20, Alpha::F21};

  for (unsigned ArgNo = 0, e = Ins.size(); ArgNo != e; ++ArgNo) {
    EVT ObjectVT = Ins[ArgNo].VT;
    SDValue ArgVal;

    if (ArgNo  < 6) {
      switch (ObjectVT.getSimpleVT().SimpleTy) {
      default:
        llvm_unreachable("Invalid value type!");
      case MVT::f64:
        args_float[ArgNo] = AddLiveIn(MF, args_float[ArgNo],
                                      &Alpha::F8RCRegClass);
        ArgVal = DAG.getCopyFromReg(Chain, dl, args_float[ArgNo], ObjectVT);
        break;
      case MVT::f32:
        args_float[ArgNo] = AddLiveIn(MF, args_float[ArgNo],
                                      &Alpha::F4RCRegClass);
        ArgVal = DAG.getCopyFromReg(Chain, dl, args_float[ArgNo], ObjectVT);
        break;
      case MVT::i64:
        args_int[ArgNo] = AddLiveIn(MF, args_int[ArgNo],
                                    &Alpha::GPRCRegClass);
        ArgVal = DAG.getCopyFromReg(Chain, dl, args_int[ArgNo], MVT::i64);
        break;
      case MVT::i32:
        args_int[ArgNo] = AddLiveIn(MF, args_int[ArgNo],
                                    &Alpha::GPRCRegClass);
        ArgVal = DAG.getCopyFromReg(Chain, dl, args_int[ArgNo], MVT::i64);
        ArgVal = DAG.getNode(ISD::TRUNCATE, dl, MVT::i32, ArgVal);
        break;
      }
    } else { //more args
      // Create the frame index object for this incoming parameter...
      int FI = MFI.CreateFixedObject(8, 8 * (ArgNo - 6), true);

      // Create the SelectionDAG nodes corresponding to a load
      //from this parameter
      SDValue FIN = DAG.getFrameIndex(FI, MVT::i64);
      ArgVal = DAG.getLoad(ObjectVT, dl, Chain, FIN, MachinePointerInfo());
    }
    InVals.push_back(ArgVal);
  }

  // If the functions takes variable number of arguments, copy all regs to stack
  if (isVarArg) {
    FuncInfo->setVarArgsOffset(Ins.size() * 8);
    std::vector<SDValue> LS;
    for (int i = 0; i < 6; ++i) {
      if (Register::isPhysicalRegister(args_int[i]))
        args_int[i] = AddLiveIn(MF, args_int[i], &Alpha::GPRCRegClass);
      SDValue argt = DAG.getCopyFromReg(Chain, dl, args_int[i], MVT::i64);
      int FI = MFI.CreateFixedObject(8, -8 * (6 - i), true);
      if (i == 0) FuncInfo->setVarArgsBase(FI);
      SDValue SDFI = DAG.getFrameIndex(FI, MVT::i64);
      LS.push_back(DAG.getStore(Chain, dl, argt, SDFI, MachinePointerInfo()));

      if (Register::isPhysicalRegister(args_float[i]))
        args_float[i] = AddLiveIn(MF, args_float[i], &Alpha::F8RCRegClass);
      argt = DAG.getCopyFromReg(Chain, dl, args_float[i], MVT::f64);
      FI = MFI.CreateFixedObject(8, - 8 * (12 - i), true);
      SDFI = DAG.getFrameIndex(FI, MVT::i64);
      LS.push_back(DAG.getStore(Chain, dl, argt, SDFI, MachinePointerInfo()));
    }

    //Set up a token factor with all the stack traffic
    Chain = DAG.getNode(ISD::TokenFactor, dl, MVT::Other, LS);
  }

  return Chain;
}

SDValue
AlphaTargetLowering::LowerReturn(SDValue Chain,
                                 CallingConv::ID CallConv, bool isVarArg,
                                 const SmallVectorImpl<ISD::OutputArg> &Outs,
                                 const SmallVectorImpl<SDValue> &OutVals,
                                  const SDLoc &dl, SelectionDAG &DAG) const {

  SDValue Copy = DAG.getCopyToReg(Chain, dl, Alpha::R26,
                                  DAG.getNode(AlphaISD::GlobalRetAddr,
                                              dl, MVT::i64),
                                  SDValue());
  switch (Outs.size()) {
  default:
    llvm_unreachable("Do not know how to return this many arguments!");
  case 0:
    break;
    //return SDValue(); // ret void is legal
  case 1: {
    EVT ArgVT = Outs[0].VT;
    unsigned ArgReg;
    if (ArgVT.isInteger())
      ArgReg = Alpha::R0;
    else {
      assert(ArgVT.isFloatingPoint());
      ArgReg = Alpha::F0;
    }
    Copy = DAG.getCopyToReg(Copy, dl, ArgReg,
                            ArgVT == MVT::i32
                                ? (isWindowsAlpha(DAG)
                                       ? signExtendWindowsI32(DAG, dl,
                                                             OutVals[0])
                                       : DAG.getNode(ISD::ANY_EXTEND, dl,
                                                     MVT::i64, OutVals[0]))
                                : OutVals[0],
                            Copy.getValue(1));
    break;
  }
  case 2: {
    EVT ArgVT = Outs[0].VT;
    unsigned ArgReg1, ArgReg2;
    if (ArgVT.isInteger()) {
      ArgReg1 = Alpha::R0;
      ArgReg2 = Alpha::R1;
    } else {
      assert(ArgVT.isFloatingPoint());
      ArgReg1 = Alpha::F0;
      ArgReg2 = Alpha::F1;
    }
    Copy = DAG.getCopyToReg(Copy, dl, ArgReg1,
                            OutVals[0], Copy.getValue(1));
    Copy = DAG.getCopyToReg(Copy, dl, ArgReg2,
                             OutVals[1], Copy.getValue(1));
    break;
  }
  }
  return DAG.getNode(AlphaISD::RET_FLAG, dl,
                     MVT::Other, Copy, Copy.getValue(1));
}

void AlphaTargetLowering::LowerVAARG(SDNode *N, SDValue &Chain,
                                      SDValue &DataPtr,
                                      SelectionDAG &DAG) const {
  Chain = N->getOperand(0);
  SDValue VAListP = N->getOperand(1);
  const Value *VAListS = cast<SrcValueSDNode>(N->getOperand(2))->getValue();
  SDLoc dl(N);

  MachineFunction &MF = DAG.getMachineFunction();
  AlphaMachineFunctionInfo *FuncInfo = MF.getInfo<AlphaMachineFunctionInfo>();

  SDValue Current = loadPointer(DAG, dl, Chain, VAListP,
                                MachinePointerInfo(VAListS));
  Chain = Current.getValue(1);

  DataPtr = Current;
  if (N->getValueType(0).isFloatingPoint()) {
    SDValue Base = DAG.getFrameIndex(FuncInfo->getVarArgsBase(), MVT::i64);
    SDValue FPDataPtr = DAG.getNode(ISD::SUB, dl, MVT::i64, Current,
                                    DAG.getConstant(8 * 6, dl, MVT::i64));
    SDValue RegSaveLimit = DAG.getNode(ISD::ADD, dl, MVT::i64, Base,
                                       DAG.getConstant(8 * 6, dl, MVT::i64));
    SDValue CC = DAG.getSetCC(dl, MVT::i64, Current, RegSaveLimit, ISD::SETLT);
    DataPtr = DAG.getNode(ISD::SELECT, dl, MVT::i64, CC, FPDataPtr, Current);
  }

  SDValue Next = DAG.getNode(ISD::ADD, dl, MVT::i64, Current,
                             DAG.getConstant(8, dl, MVT::i64));
  Chain = storePointer(DAG, dl, Chain, Next, VAListP,
                       MachinePointerInfo(VAListS));
}

/// LowerOperation - Provide custom lowering hooks for some operations.
///
SDValue AlphaTargetLowering::LowerOperation(SDValue Op,
                                            SelectionDAG &DAG) const {
  SDLoc dl(Op);
  switch (Op.getOpcode()) {
  default: llvm_unreachable("Wasn't expecting to be able to lower this!");
  case ISD::JumpTable: return LowerJumpTable(Op, DAG);

  case ISD::INTRINSIC_WO_CHAIN:
    break;

  case ISD::SRL_PARTS: {
    SDValue ShOpLo = Op.getOperand(0);
    SDValue ShOpHi = Op.getOperand(1);
    SDValue ShAmt  = Op.getOperand(2);
    SDValue bm = DAG.getNode(ISD::SUB, dl, MVT::i64,
                             DAG.getConstant(64, dl, MVT::i64), ShAmt);
    SDValue BMCC = DAG.getSetCC(dl, MVT::i64, bm,
                                DAG.getConstant(0, dl, MVT::i64), ISD::SETLE);
    // if 64 - shAmt <= 0
    SDValue Hi_Neg = DAG.getConstant(0, dl, MVT::i64);
    SDValue ShAmt_Neg = DAG.getNode(ISD::SUB, dl, MVT::i64,
                                    DAG.getConstant(0, dl, MVT::i64), bm);
    SDValue Lo_Neg = DAG.getNode(ISD::SRL, dl, MVT::i64, ShOpHi, ShAmt_Neg);
    // else
    SDValue carries = DAG.getNode(ISD::SHL, dl, MVT::i64, ShOpHi, bm);
    SDValue Hi_Pos =  DAG.getNode(ISD::SRL, dl, MVT::i64, ShOpHi, ShAmt);
    SDValue Lo_Pos = DAG.getNode(ISD::SRL, dl, MVT::i64, ShOpLo, ShAmt);
    Lo_Pos = DAG.getNode(ISD::OR, dl, MVT::i64, Lo_Pos, carries);
    // Merge
    SDValue Hi = DAG.getNode(ISD::SELECT, dl, MVT::i64, BMCC, Hi_Neg, Hi_Pos);
    SDValue Lo = DAG.getNode(ISD::SELECT, dl, MVT::i64, BMCC, Lo_Neg, Lo_Pos);
    SDValue Ops[2] = { Lo, Hi };
    return DAG.getMergeValues(Ops, dl);
  }
    //  case ISD::SRA_PARTS:

    //  case ISD::SHL_PARTS:


  case ISD::SINT_TO_FP: {
    assert(Op.getOperand(0).getValueType() == MVT::i64 &&
           "Unhandled SINT_TO_FP type in custom expander!");
    SDValue LD;
    bool isDouble = Op.getValueType() == MVT::f64;
    LD = DAG.getNode(ISD::BITCAST, dl, MVT::f64, Op.getOperand(0));
    SDValue FP = DAG.getNode(isDouble?AlphaISD::CVTQT_:AlphaISD::CVTQS_, dl,
                               isDouble?MVT::f64:MVT::f32, LD);
    return FP;
  }
  case ISD::FP_TO_SINT: {
    bool isDouble = Op.getOperand(0).getValueType() == MVT::f64;
    SDValue src = Op.getOperand(0);

    if (!isDouble) //Promote
      src = DAG.getNode(ISD::FP_EXTEND, dl, MVT::f64, src);

    src = DAG.getNode(AlphaISD::CVTTQ_, dl, MVT::f64, src);

    return DAG.getNode(ISD::BITCAST, dl, MVT::i64, src);
  }
  case ISD::ConstantPool: {
    ConstantPoolSDNode *CP = cast<ConstantPoolSDNode>(Op);
    const Constant *C = CP->getConstVal();
    SDValue CPI = DAG.getTargetConstantPool(C, MVT::i64, CP->getAlign());
    // FIXME there isn't really any debug info here

    SDValue Hi = DAG.getNode(AlphaISD::GPRelHi,  dl, MVT::i64, CPI,
                               DAG.getGLOBAL_OFFSET_TABLE(MVT::i64));
    SDValue Lo = DAG.getNode(AlphaISD::GPRelLo, dl, MVT::i64, CPI, Hi);
    return Lo;
  }
  case ISD::GlobalTLSAddress: {
    GlobalAddressSDNode *GSDN = cast<GlobalAddressSDNode>(Op);
    const GlobalValue *GV = GSDN->getGlobal();
    MachineFunction &MF = DAG.getMachineFunction();
    if (!MF.getTarget().getTargetTriple().isOSBinFormatELF()) {
      DAG.getContext()->diagnose(DiagnosticInfoUnsupported(
          MF.getFunction(), "Alpha TLS is supported only for ELF",
          dl.getDebugLoc()));
      return DAG.getPOISON(MVT::i64);
    }

    TLSModel::Model Model = getTargetMachine().getTLSModel(GV);
    SDValue GA =
        DAG.getTargetGlobalAddress(GV, dl, MVT::i64, GSDN->getOffset());
    SDValue TP = DAG.getNode(AlphaISD::RDUNIQUE, dl, MVT::i64);
    if (Model == TLSModel::LocalExec) {
      SDValue Hi = DAG.getNode(AlphaISD::TPRelHi, dl, MVT::i64, GA, TP);
      return DAG.getNode(AlphaISD::TPRelLo, dl, MVT::i64, GA, Hi);
    }
    if (Model == TLSModel::InitialExec) {
      SDValue TPOff = DAG.getNode(AlphaISD::GOTTPRel, dl, MVT::i64, GA,
                                  DAG.getGLOBAL_OFFSET_TABLE(MVT::i64));
      return DAG.getNode(ISD::ADD, dl, MVT::i64, TP, TPOff);
    }

    DAG.getContext()->diagnose(DiagnosticInfoUnsupported(
        MF.getFunction(),
        "Alpha ELF general-dynamic and local-dynamic TLS are not supported",
        dl.getDebugLoc()));
    return DAG.getPOISON(MVT::i64);
  }
  case ISD::GlobalAddress: {
    GlobalAddressSDNode *GSDN = cast<GlobalAddressSDNode>(Op);
    const GlobalValue *GV = GSDN->getGlobal();
    SDValue GA = DAG.getTargetGlobalAddress(GV, dl, MVT::i64,
                                            GSDN->getOffset());
    // FIXME there isn't really any debug info here

    //    if (!GV->hasWeakLinkage() && !GV->isDeclaration()
    //        && !GV->hasLinkOnceLinkage()) {
    if (DAG.getMachineFunction().getTarget().getTargetTriple().isOSBinFormatCOFF() ||
        GV->hasLocalLinkage()) {
      SDValue Hi = DAG.getNode(AlphaISD::GPRelHi,  dl, MVT::i64, GA,
                                 DAG.getGLOBAL_OFFSET_TABLE(MVT::i64));
      SDValue Lo = DAG.getNode(AlphaISD::GPRelLo, dl, MVT::i64, GA, Hi);
      return Lo;
    } else
      return DAG.getNode(AlphaISD::RelLit, dl, MVT::i64, GA,
                         DAG.getGLOBAL_OFFSET_TABLE(MVT::i64));
  }
  case ISD::ExternalSymbol: {
    SDValue ES = DAG.getTargetExternalSymbol(
        cast<ExternalSymbolSDNode>(Op)->getSymbol(), MVT::i64);
    if (DAG.getMachineFunction().getTarget().getTargetTriple()
            .isOSBinFormatCOFF()) {
      SDValue Hi = DAG.getNode(AlphaISD::GPRelHi, dl, MVT::i64, ES,
                               DAG.getGLOBAL_OFFSET_TABLE(MVT::i64));
      return DAG.getNode(AlphaISD::GPRelLo, dl, MVT::i64, ES, Hi);
    }
    return DAG.getNode(AlphaISD::RelLit, dl, MVT::i64, ES,
                       DAG.getGLOBAL_OFFSET_TABLE(MVT::i64));
  }

  case ISD::UREM:
  case ISD::SREM:
    //Expand only on constant case
    if (Op.getOperand(1).getOpcode() == ISD::Constant) {
      EVT VT = Op.getNode()->getValueType(0);
      SmallVector<SDNode *, 4> Created;
      SDValue Tmp1 = Op.getNode()->getOpcode() == ISD::UREM ?
        BuildUDIV(Op.getNode(), DAG, false, false, Created) :
        BuildSDIV(Op.getNode(), DAG, false, false, Created);
      Tmp1 = DAG.getNode(ISD::MUL, dl, VT, Tmp1, Op.getOperand(1));
      Tmp1 = DAG.getNode(ISD::SUB, dl, VT, Op.getOperand(0), Tmp1);
      return Tmp1;
    }
    [[fallthrough]];
  case ISD::SDIV:
  case ISD::UDIV:
    if (Op.getValueType().isInteger()) {
      if (Op.getOperand(1).getOpcode() == ISD::Constant)
      {
        SmallVector<SDNode *, 4> Created;
        return Op.getOpcode() == ISD::SDIV
                   ? BuildSDIV(Op.getNode(), DAG, false, false, Created)
                   : BuildUDIV(Op.getNode(), DAG, false, false, Created);
      }
      const char* opstr = 0;
      switch (Op.getOpcode()) {
      case ISD::UREM: opstr = "_OtsRemainder64Unsigned"; break;
      case ISD::SREM: opstr = "_OtsRemainder64"; break;
      case ISD::UDIV: opstr = "_OtsDivide64Unsigned"; break;
      case ISD::SDIV: opstr = "_OtsDivide64"; break;
      }
      SDValue Tmp1 = Op.getOperand(0),
        Tmp2 = Op.getOperand(1),
        Addr = DAG.getExternalSymbol(opstr, MVT::i64);
      return DAG.getNode(AlphaISD::DivCall, dl, MVT::i64, Addr, Tmp1, Tmp2);
    }
    break;

  case ISD::VAARG: {
    SDValue Chain, DataPtr;
    LowerVAARG(Op.getNode(), Chain, DataPtr, DAG);

    SDValue Result;
    if (Op.getValueType() == MVT::i32)
      Result = DAG.getExtLoad(ISD::SEXTLOAD, dl, MVT::i64, Chain, DataPtr,
                              MachinePointerInfo(), MVT::i32);
    else
      Result = DAG.getLoad(Op.getValueType(), dl, Chain, DataPtr,
                           MachinePointerInfo());
    return Result;
  }
  case ISD::VACOPY: {
    SDValue Chain = Op.getOperand(0);
    SDValue DestP = Op.getOperand(1);
    SDValue SrcP = Op.getOperand(2);
    const Value *DestS = cast<SrcValueSDNode>(Op.getOperand(3))->getValue();
    const Value *SrcS = cast<SrcValueSDNode>(Op.getOperand(4))->getValue();

    SDValue Val = loadPointer(DAG, dl, Chain, SrcP, MachinePointerInfo(SrcS));
    return storePointer(DAG, dl, Val.getValue(1), Val, DestP,
                        MachinePointerInfo(DestS));
  }
  case ISD::VASTART: {
    MachineFunction &MF = DAG.getMachineFunction();
    AlphaMachineFunctionInfo *FuncInfo = MF.getInfo<AlphaMachineFunctionInfo>();

    SDValue Chain = Op.getOperand(0);
    SDValue VAListP = Op.getOperand(1);
    const Value *VAListS = cast<SrcValueSDNode>(Op.getOperand(2))->getValue();

    // The Microsoft Alpha CRT exposes va_list as { char *a0; int offset; }.
    // Its headers use a three-argument __builtin_va_start, which Clang marks
    // with alpha-ms-va-list. Initialize the register-save-area base and the
    // byte offset of the first unnamed argument exactly as those macros expect.
    SDValue FR  = DAG.getFrameIndex(FuncInfo->getVarArgsBase(), MVT::i64);
    if (MF.getFunction().hasFnAttribute("alpha-ms-va-list")) {
      SDValue BaseStore = storePointer(DAG, dl, Chain, FR, VAListP,
                                       MachinePointerInfo(VAListS));
      unsigned PointerBytes = has32BitPointers(DAG) ? 4 : 8;
      SDValue OffsetP = DAG.getNode(
          ISD::ADD, dl, MVT::i64, VAListP,
          DAG.getConstant(PointerBytes, dl, MVT::i64));
      return DAG.getTruncStore(
          BaseStore, dl,
          DAG.getConstant(FuncInfo->getVarArgsOffset(), dl, MVT::i64), OffsetP,
          MachinePointerInfo(VAListS, PointerBytes), MVT::i32);
    }

    // Clang's native pointer va_list starts at the next unnamed argument.
    SDValue Start = DAG.getNode(ISD::ADD, dl, MVT::i64, FR,
                                DAG.getConstant(FuncInfo->getVarArgsOffset(),
                                                dl, MVT::i64));
    return storePointer(DAG, dl, Chain, Start, VAListP,
                        MachinePointerInfo(VAListS));
  }
  case ISD::RETURNADDR:
    return DAG.getNode(AlphaISD::GlobalRetAddr, dl, MVT::i64);
      //FIXME: implement
  case ISD::FRAMEADDR:          break;
  }

  return SDValue();
}

void AlphaTargetLowering::ReplaceNodeResults(SDNode *N,
                                             SmallVectorImpl<SDValue>&Results,
                                             SelectionDAG &DAG) const {
  SDLoc dl(N);
  assert(N->getValueType(0) == MVT::i32 &&
         N->getOpcode() == ISD::VAARG &&
         "Unknown node to custom promote!");

  SDValue Chain, DataPtr;
  LowerVAARG(N, Chain, DataPtr, DAG);
  SDValue Res = DAG.getLoad(N->getValueType(0), dl, Chain, DataPtr,
                            MachinePointerInfo());
  Results.push_back(Res);
  Results.push_back(SDValue(Res.getNode(), 1));
}


//Inline Asm

/// getConstraintType - Given a constraint letter, return the type of
/// constraint it is for this target.
AlphaTargetLowering::ConstraintType
AlphaTargetLowering::getConstraintType(StringRef Constraint) const {
  if (Constraint.size() == 1) {
    switch (Constraint[0]) {
    default: break;
    case 'f':
    case 'r':
      return C_RegisterClass;
    }
  }
  return TargetLowering::getConstraintType(Constraint);
}

/// Examine constraint type and operand type and determine a weight value.
/// This object must already have been set up with the operand type
/// and the current alternative constraint selected.
TargetLowering::ConstraintWeight
AlphaTargetLowering::getSingleConstraintMatchWeight(
    AsmOperandInfo &info, const char *constraint) const {
  ConstraintWeight weight = CW_Invalid;
  Value *CallOperandVal = info.CallOperandVal;
    // If we don't have a value, we can't do a match,
    // but allow it at the lowest weight.
  if (CallOperandVal == NULL)
    return CW_Default;
  // Look at the constraint type.
  switch (*constraint) {
  default:
    weight = TargetLowering::getSingleConstraintMatchWeight(info, constraint);
    break;
  case 'f':
    weight = CW_Register;
    break;
  }
  return weight;
}

/// Given a register class constraint, like 'r', if this corresponds directly
/// to an LLVM register class, return a register of 0 and the register class
/// pointer.
std::pair<unsigned, const TargetRegisterClass*> AlphaTargetLowering::
getRegForInlineAsmConstraint(const TargetRegisterInfo *TRI,
                             StringRef Constraint, MVT VT) const
{
  if (Constraint.size() == 1) {
    switch (Constraint[0]) {
    case 'r':
      return std::make_pair(0U, &Alpha::GPRCRegClass);
    case 'f':
      return VT == MVT::f64 ? std::make_pair(0U, &Alpha::F8RCRegClass) :
	std::make_pair(0U, &Alpha::F4RCRegClass);
    }
  }
  return TargetLowering::getRegForInlineAsmConstraint(TRI, Constraint, VT);
}

//===----------------------------------------------------------------------===//
//  Other Lowering Code
//===----------------------------------------------------------------------===//

MachineBasicBlock *
AlphaTargetLowering::EmitInstrWithCustomInserter(MachineInstr &MI,
                                                 MachineBasicBlock *BB) const {
  const TargetInstrInfo *TII = BB->getParent()->getSubtarget().getInstrInfo();
  assert((MI.getOpcode() == Alpha::CAS32 ||
          MI.getOpcode() == Alpha::CAS64 ||
          MI.getOpcode() == Alpha::LAS32 ||
          MI.getOpcode() == Alpha::LAS64 ||
          MI.getOpcode() == Alpha::SWAP32 ||
          MI.getOpcode() == Alpha::SWAP64) &&
         "Unexpected instr type to insert");

  bool is32 = MI.getOpcode() == Alpha::CAS32 ||
    MI.getOpcode() == Alpha::LAS32 ||
    MI.getOpcode() == Alpha::SWAP32;

  //Load locked store conditional for atomic ops take on the same form
  //start:
  //ll
  //do stuff (maybe branch to exit)
  //sc
  //test sc and maybe branck to start
  //exit:
  const BasicBlock *LLVM_BB = BB->getBasicBlock();
  DebugLoc dl = MI.getDebugLoc();
  MachineFunction::iterator It = BB->getIterator();
  ++It;

  MachineBasicBlock *thisMBB = BB;
  MachineFunction *F = BB->getParent();
  MachineBasicBlock *llscMBB = F->CreateMachineBasicBlock(LLVM_BB);
  MachineBasicBlock *sinkMBB = F->CreateMachineBasicBlock(LLVM_BB);

  sinkMBB->splice(sinkMBB->begin(), thisMBB,
                  std::next(MI.getIterator()),
                  thisMBB->end());
  sinkMBB->transferSuccessorsAndUpdatePHIs(thisMBB);

  F->insert(It, llscMBB);
  F->insert(It, sinkMBB);

  BuildMI(thisMBB, dl, TII->get(Alpha::BR)).addMBB(llscMBB);

  unsigned reg_res = MI.getOperand(0).getReg(),
    reg_ptr = MI.getOperand(1).getReg(),
    reg_v2 = MI.getOperand(2).getReg(),
    reg_store = F->getRegInfo().createVirtualRegister(&Alpha::GPRCRegClass);

  BuildMI(llscMBB, dl, TII->get(is32 ? Alpha::LDL_L : Alpha::LDQ_L),
          reg_res).addImm(0).addReg(reg_ptr);
  switch (MI.getOpcode()) {
  case Alpha::CAS32:
  case Alpha::CAS64: {
    unsigned reg_cmp
      = F->getRegInfo().createVirtualRegister(&Alpha::GPRCRegClass);
    BuildMI(llscMBB, dl, TII->get(Alpha::CMPEQ), reg_cmp)
      .addReg(reg_v2).addReg(reg_res);
    BuildMI(llscMBB, dl, TII->get(Alpha::BEQ))
      .addImm(0).addReg(reg_cmp).addMBB(sinkMBB);
    BuildMI(llscMBB, dl, TII->get(Alpha::BISr), reg_store)
      .addReg(Alpha::R31).addReg(MI.getOperand(3).getReg());
    break;
  }
  case Alpha::LAS32:
  case Alpha::LAS64: {
    BuildMI(llscMBB, dl,TII->get(is32 ? Alpha::ADDLr : Alpha::ADDQr), reg_store)
      .addReg(reg_res).addReg(reg_v2);
    break;
  }
  case Alpha::SWAP32:
  case Alpha::SWAP64: {
    BuildMI(llscMBB, dl, TII->get(Alpha::BISr), reg_store)
      .addReg(reg_v2).addReg(reg_v2);
    break;
  }
  }
  BuildMI(llscMBB, dl, TII->get(is32 ? Alpha::STL_C : Alpha::STQ_C), reg_store)
    .addReg(reg_store).addImm(0).addReg(reg_ptr);
  BuildMI(llscMBB, dl, TII->get(Alpha::BEQ))
    .addImm(0).addReg(reg_store).addMBB(llscMBB);
  BuildMI(llscMBB, dl, TII->get(Alpha::BR)).addMBB(sinkMBB);

  thisMBB->addSuccessor(llscMBB);
  llscMBB->addSuccessor(llscMBB);
  llscMBB->addSuccessor(sinkMBB);
  MI.eraseFromParent();   // The pseudo instruction is gone now.

  return sinkMBB;
}

bool
AlphaTargetLowering::isOffsetFoldingLegal(const GlobalAddressSDNode *GA) const {
  // The Alpha target isn't yet aware of offsets.
  return false;
}

bool AlphaTargetLowering::isFPImmLegal(const APFloat &Imm, EVT VT,
                                       bool ForCodeSize) const {
  if (VT != MVT::f32 && VT != MVT::f64)
    return false;
  // +0.0   F31
  // +0.0f  F31
  // -0.0  -F31
  // -0.0f -F31
  return Imm.isZero() || Imm.isNegZero();
}
