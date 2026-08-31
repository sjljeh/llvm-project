//===- Alpha.td - Describe the Alpha Target Machine --------*- tablegen -*-===//
// 
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
// 
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//

// Get the target-independent interfaces which we are implementing...
//
include "llvm/Target/Target.td"

//Alpha is little endian

//===----------------------------------------------------------------------===//
// Subtarget Features
//===----------------------------------------------------------------------===//

def FeatureCIX : SubtargetFeature<"cix", "HasCT", "true",
                                  "Enable CIX extensions">;
def FeatureTASO : SubtargetFeature<"taso", "IsTASO", "true",
                                   "Constrain Alpha pointers to low address space">;

//===----------------------------------------------------------------------===//
// Register File Description
//===----------------------------------------------------------------------===//

include "AlphaRegisterInfo.td"

defm : RemapAllTargetPseudoPointerOperands<GPRC>;

//===----------------------------------------------------------------------===//
// Calling Convention Description
//===----------------------------------------------------------------------===//

include "AlphaCallingConv.td"

//===----------------------------------------------------------------------===//
// Schedule Description
//===----------------------------------------------------------------------===//

include "AlphaSchedule.td"

//===----------------------------------------------------------------------===//
// Instruction Descriptions
//===----------------------------------------------------------------------===//

include "AlphaInstrInfo.td"

def AlphaInstrInfo : InstrInfo;

//===----------------------------------------------------------------------===//
// Alpha Processor Definitions
//===----------------------------------------------------------------------===//

def : ProcessorModel<"generic", Alpha21264Model, []>;
def : ProcessorModel<"ev4", Alpha21064Model, []>;
def : ProcessorModel<"ev5", Alpha21164Model, []>;
def : ProcessorModel<"ev56", Alpha21164Model, []>;
def : ProcessorModel<"pca56", Alpha21164Model, []>;
def : ProcessorModel<"ev6", Alpha21264Model, []>;
def : ProcessorModel<"ev7", Alpha21364Model, [FeatureCIX]>;
def : ProcessorModel<"ev67", Alpha21264Model, [FeatureCIX]>;

//===----------------------------------------------------------------------===//
// The Alpha Target
//===----------------------------------------------------------------------===//


def Alpha : Target {
  // Pull in Instruction Info:
  let InstructionSet = AlphaInstrInfo;
}
