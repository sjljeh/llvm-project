//===-- AlphaISelLowering.h - Alpha DAG Lowering Interface ------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the interfaces that Alpha uses to lower LLVM code into a
// selection DAG.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TARGET_ALPHA_ALPHAISELLOWERING_H
#define LLVM_TARGET_ALPHA_ALPHAISELLOWERING_H

#include "llvm/CodeGen/SelectionDAG.h"
#include "llvm/CodeGen/TargetLowering.h"
#include "Alpha.h"

namespace llvm {
  class AlphaSubtarget;

  namespace AlphaISD {
  enum NodeType {
    // Start the numbering where the builting ops and target ops leave off.
    FIRST_NUMBER = ISD::BUILTIN_OP_END,
    // These corrospond to the identical Instruction
    CVTQT_,
    CVTQS_,
    CVTTQ_,

    /// GPRelHi/GPRelLo - These represent the high and low 16-bit
    /// parts of a global address respectively.
    GPRelHi,
    GPRelLo,

    /// RetLit - Literal Relocation of a Global
    RelLit,

    /// Alpha ELF TLS address materialization.
    TPRelHi,
    TPRelLo,
    GOTTPRel,
    RDUNIQUE,

    /// GlobalRetAddr - used to restore the return address
    GlobalRetAddr,

    /// CALL - Normal call.
    CALL,

    /// DIVCALL - used for special library calls for div and rem
    DivCall,

    /// return flag operand
    RET_FLAG,

    /// CHAIN = COND_BRANCH CHAIN, OPC, (G|F)PRC, DESTBB [, INFLAG] - This
    /// corresponds to the COND_BRANCH pseudo instruction.
    /// *PRC is the input register to compare to zero,
    /// OPC is the branch opcode to use (e.g. Alpha::BEQ),
    /// DESTBB is the destination block to branch to, and INFLAG is
    /// an optional input flag argument.
    COND_BRANCH_I,
    COND_BRANCH_F,

    /// CHAIN = PROBED_ALLOCA CHAIN, NEWSP - Touch every page between $30
    /// and NEWSP, then set $30 to NEWSP (Windows stack probes).
    PROBED_ALLOCA
  };
  }

  class AlphaTargetLowering : public TargetLowering {
  public:
    explicit AlphaTargetLowering(const TargetMachine &TM,
                                 const AlphaSubtarget &STI);

    /// NT RtlUnwind enters an SEH __except block with the exception code in
    /// $0.
    Register
    getExceptionPointerRegister(ExceptionHandling EH,
                                const Constant *PersonalityFn) const override {
      return EH == ExceptionHandling::WinEH ? Register(Alpha::R0) : Register();
    }

    /// The NT C++ runtime builds a catch object at EstablisherFrame +
    /// dispCatchObj, a CFA-relative offset known once the frame is laid out.
    bool needsFixedCatchObjects() const override { return false; }

    SDValue lowerWinEHIntrinsic(SDNode *N, SelectionDAG &DAG) const;

    /// Windows Alpha touches every page of a large frame or alloca before
    /// moving $30 past it, for functions with "probe-stack"="inline-asm"
    /// (clang's default on Windows).
    bool hasInlineStackProbe(const MachineFunction &MF) const override;
    /// The probe interval: "stack-probe-size" (default 4096), at least the
    /// stack alignment and at most 16384, so that every probe offset fits an
    /// lda or stq displacement.
    unsigned getStackProbeSize(const MachineFunction &MF) const;

    MVT getPointerTy(const DataLayout &DL, uint32_t AS = 0) const override {
      return MVT::i64;
    }

    MVT getScalarShiftAmountTy(const DataLayout &, EVT) const override {
      return MVT::i64;
    }

    /// getSetCCResultType - Get the SETCC result ValueType
    EVT getSetCCResultType(const DataLayout &DL, LLVMContext &Context,
                           EVT VT) const override;

    /// LowerOperation - Provide custom lowering hooks for some operations.
    ///
    SDValue LowerOperation(SDValue Op, SelectionDAG &DAG) const override;

    /// ReplaceNodeResults - Replace the results of node with an illegal result
    /// type with new values built out of custom code.
    ///
    void ReplaceNodeResults(SDNode *N, SmallVectorImpl<SDValue> &Results,
                            SelectionDAG &DAG) const override;

    // Friendly names for dumps
    const char *getTargetNodeName(unsigned Opcode) const override;

    SDValue LowerCallResult(SDValue Chain, SDValue InFlag,
                            CallingConv::ID CallConv, bool isVarArg,
                            const SmallVectorImpl<ISD::InputArg> &Ins,
                            const SDLoc &dl, SelectionDAG &DAG,
                            SmallVectorImpl<SDValue> &InVals) const;

    ConstraintType getConstraintType(StringRef Constraint) const override;

    /// Examine constraint string and operand type and determine a weight value.
    /// The operand object must already have been set up with the operand type.
    ConstraintWeight getSingleConstraintMatchWeight(
      AsmOperandInfo &info, const char *constraint) const override;

    std::pair<unsigned, const TargetRegisterClass *>
    getRegForInlineAsmConstraint(const TargetRegisterInfo *TRI,
                                 StringRef Constraint, MVT VT) const override;

    MachineBasicBlock *
      EmitInstrWithCustomInserter(MachineInstr &MI,
                                  MachineBasicBlock *BB) const override;

    bool isOffsetFoldingLegal(const GlobalAddressSDNode *GA) const override;

    /// Only +/-0.0 can be selected natively (from F31); other FP immediates
    /// are materialized as a load from a constant pool.
    FPImmAction getFPImmAction(const APFloat &Imm, EVT VT,
                               bool ForCodeSize) const override;

  private:
    // Helpers for custom lowering.
    void LowerVAARG(SDNode *N, SDValue &Chain, SDValue &DataPtr,
                    SelectionDAG &DAG) const;
    SDValue lowerDYNAMIC_STACKALLOC(SDValue Op, SelectionDAG &DAG) const;
    MachineBasicBlock *emitProbedAlloca(MachineInstr &MI,
                                        MachineBasicBlock *BB) const;

    SDValue
      LowerFormalArguments(SDValue Chain,
                           CallingConv::ID CallConv, bool isVarArg,
                           const SmallVectorImpl<ISD::InputArg> &Ins,
                           const SDLoc &dl, SelectionDAG &DAG,
                           SmallVectorImpl<SDValue> &InVals) const override;

    SDValue LowerCall(TargetLowering::CallLoweringInfo &CLI,
                      SmallVectorImpl<SDValue> &InVals) const override;

    SDValue
      LowerReturn(SDValue Chain,
                  CallingConv::ID CallConv, bool isVarArg,
                  const SmallVectorImpl<ISD::OutputArg> &Outs,
                  const SmallVectorImpl<SDValue> &OutVals,
                  const SDLoc &dl, SelectionDAG &DAG) const override;
  };
}

#endif   // LLVM_TARGET_ALPHA_ALPHAISELLOWERING_H
