//===- AlphaSchedule.td - Alpha Scheduling Definitions -----*- tablegen -*-===//
// 
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
// 
//===----------------------------------------------------------------------===//

//This is table 2-2 from the 21264 compiler writers guide
//modified some

//Pipelines

def L0   : FuncUnit;
def L1   : FuncUnit;
def FST0 : FuncUnit;
def FST1 : FuncUnit;
def U0   : FuncUnit;
def U1   : FuncUnit;
def FA   : FuncUnit;
def FM   : FuncUnit;

def s_ild   : InstrItinClass;
def s_fld   : InstrItinClass;
def s_ist   : InstrItinClass;
def s_fst   : InstrItinClass;
def s_lda   : InstrItinClass;
def s_rpcc  : InstrItinClass;
def s_rx    : InstrItinClass;
def s_mxpr  : InstrItinClass;
def s_icbr  : InstrItinClass;
def s_ubr   : InstrItinClass;
def s_jsr   : InstrItinClass;
def s_iadd  : InstrItinClass;
def s_ilog  : InstrItinClass;
def s_ishf  : InstrItinClass;
def s_cmov  : InstrItinClass;
def s_imul  : InstrItinClass;
def s_imisc : InstrItinClass;
def s_fbr   : InstrItinClass;
def s_fadd  : InstrItinClass;
def s_fmul  : InstrItinClass;
def s_fcmov : InstrItinClass;
def s_fdivt : InstrItinClass;
def s_fdivs : InstrItinClass;
def s_fsqrts: InstrItinClass;
def s_fsqrtt: InstrItinClass;
def s_ftoi  : InstrItinClass;
def s_itof  : InstrItinClass;
def s_pseudo : InstrItinClass;

// Table 2-4 separates functional-unit occupancy from result latency. The
// integer and floating add/multiply pipelines accept a new operation every
// cycle; divide and square-root retain their documented reuse intervals.
def Alpha21264Itineraries : ProcessorItineraries<
  [L0, L1, FST0, FST1, U0, U1, FA, FM], [], [
  InstrItinData<s_ild,    [InstrStage<1, [L0, L1]>], [3]>,
  InstrItinData<s_fld,    [InstrStage<1, [L0, L1]>], [4]>,
  InstrItinData<s_ist,    [InstrStage<1, [L0, L1]>]>,
  InstrItinData<s_fst,    [InstrStage<1, [FST0, FST1, L0, L1]>]>,
  InstrItinData<s_lda,    [InstrStage<1, [L0, L1, U0, U1]>], [1]>,
  InstrItinData<s_rpcc,   [InstrStage<1, [L1]>], [1]>,
  InstrItinData<s_rx,     [InstrStage<1, [L1]>], [1]>,
  InstrItinData<s_mxpr,   [InstrStage<1, [L0, L1]>], [1]>,
  InstrItinData<s_icbr,   [InstrStage<1, [U0, U1]>]>,
  InstrItinData<s_ubr,    [InstrStage<1, [U0, U1]>]>,
  InstrItinData<s_jsr,    [InstrStage<1, [L0]>]>,
  InstrItinData<s_iadd,   [InstrStage<1, [L0, U0, L1, U1]>], [1]>,
  InstrItinData<s_ilog,   [InstrStage<1, [L0, U0, L1, U1]>], [1]>,
  InstrItinData<s_ishf,   [InstrStage<1, [U0, U1]>], [1]>,
  InstrItinData<s_cmov,   [InstrStage<1, [L0, U0, L1, U1]>], [1]>,
  InstrItinData<s_imul,   [InstrStage<1, [U1]>], [7]>,
  InstrItinData<s_imisc,  [InstrStage<1, [U0]>], [3]>,
  InstrItinData<s_fbr,    [InstrStage<1, [FA]>]>,
  InstrItinData<s_fadd,   [InstrStage<1, [FA]>], [4]>,
  InstrItinData<s_fmul,   [InstrStage<1, [FM]>], [4]>,
  InstrItinData<s_fcmov,  [InstrStage<1, [FA]>], [4]>,
  InstrItinData<s_fdivs,  [InstrStage<9, [FA]>], [12]>,
  InstrItinData<s_fdivt,  [InstrStage<12, [FA]>], [15]>,
  InstrItinData<s_fsqrts, [InstrStage<15, [FA]>], [18]>,
  InstrItinData<s_fsqrtt, [InstrStage<30, [FA]>], [33]>,
  InstrItinData<s_ftoi,   [InstrStage<1, [FST0, FST1, L0, L1]>], [3]>,
  InstrItinData<s_itof,   [InstrStage<1, [L0, L1]>], [4]>
]>;

def Alpha21064Model : SchedMachineModel {
  let IssueWidth = 2;
  let LoadLatency = 3;
  let MispredictPenalty = 7;
  let Itineraries = Alpha21264Itineraries;
  let CompleteModel = 0;
}

def Alpha21164Model : SchedMachineModel {
  let IssueWidth = 4;
  let LoadLatency = 2;
  let MispredictPenalty = 5;
  let Itineraries = Alpha21264Itineraries;
  let CompleteModel = 0;
}

def Alpha21264Model : SchedMachineModel {
  let IssueWidth = 4;
  let MicroOpBufferSize = 80;
  let LoadLatency = 3;
  let MispredictPenalty = 7;
  let Itineraries = Alpha21264Itineraries;
  let CompleteModel = 0;
}

def Alpha21364Model : SchedMachineModel {
  let IssueWidth = 4;
  let MicroOpBufferSize = 80;
  let LoadLatency = 3;
  let MispredictPenalty = 7;
  let Itineraries = Alpha21264Itineraries;
  let CompleteModel = 0;
}

// Modern instruction scheduling resources. Itinerary classes map once to
// abstract writes; each CPU model instantiates its own execution resources.
def AlphaWriteILoad : SchedWrite;
def AlphaWriteFLoad : SchedWrite;
def AlphaWriteIStore : SchedWrite;
def AlphaWriteFStore : SchedWrite;
def AlphaWriteIAny : SchedWrite;
def AlphaWriteUpper : SchedWrite;
def AlphaWriteLower : SchedWrite;
def AlphaWriteIMul : SchedWrite;
def AlphaWriteIMisc : SchedWrite;
def AlphaWriteFAdd : SchedWrite;
def AlphaWriteFMul : SchedWrite;
def AlphaWriteFDivS : SchedWrite;
def AlphaWriteFDivT : SchedWrite;
def AlphaWriteFSqrtS : SchedWrite;
def AlphaWriteFSqrtT : SchedWrite;

multiclass AlphaSchedResources<SchedMachineModel Model, int ILoadLatency> {
  let SchedModel = Model in {
    def IntAny : ProcResource<4>;
    def Upper : ProcResource<2>;
    def Lower : ProcResource<2>;
    def IntMul : ProcResource<1>;
    def FPAdd : ProcResource<1>;
    def FPMul : ProcResource<1>;
    def FPStore : ProcResource<2>;

    def : WriteRes<AlphaWriteILoad, [!cast<ProcResourceKind>(NAME # "Lower")]> {
      let Latency = ILoadLatency;
    }
    def : WriteRes<AlphaWriteFLoad, [!cast<ProcResourceKind>(NAME # "Lower")]> {
      let Latency = 4;
    }
    def : WriteRes<AlphaWriteIStore, [!cast<ProcResourceKind>(NAME # "Lower")]> {
      let Latency = 0;
    }
    def : WriteRes<AlphaWriteFStore,
                   [!cast<ProcResourceKind>(NAME # "FPStore"),
                    !cast<ProcResourceKind>(NAME # "Lower")]> {
      let Latency = 0;
    }
    def : WriteRes<AlphaWriteIAny, [!cast<ProcResourceKind>(NAME # "IntAny")]>;
    def : WriteRes<AlphaWriteUpper, [!cast<ProcResourceKind>(NAME # "Upper")]>;
    def : WriteRes<AlphaWriteLower, [!cast<ProcResourceKind>(NAME # "Lower")]>;
    def : WriteRes<AlphaWriteIMul, [!cast<ProcResourceKind>(NAME # "IntMul")]> {
      let Latency = 7;
    }
    def : WriteRes<AlphaWriteIMisc, [!cast<ProcResourceKind>(NAME # "Upper")]> {
      let Latency = 3;
    }
    def : WriteRes<AlphaWriteFAdd, [!cast<ProcResourceKind>(NAME # "FPAdd")]> {
      let Latency = 4;
    }
    def : WriteRes<AlphaWriteFMul, [!cast<ProcResourceKind>(NAME # "FPMul")]> {
      let Latency = 4;
    }
    def : WriteRes<AlphaWriteFDivS, [!cast<ProcResourceKind>(NAME # "FPAdd")]> {
      let Latency = 12;
      let ReleaseAtCycles = [9];
    }
    def : WriteRes<AlphaWriteFDivT, [!cast<ProcResourceKind>(NAME # "FPAdd")]> {
      let Latency = 15;
      let ReleaseAtCycles = [12];
    }
    def : WriteRes<AlphaWriteFSqrtS, [!cast<ProcResourceKind>(NAME # "FPAdd")]> {
      let Latency = 18;
      let ReleaseAtCycles = [15];
    }
    def : WriteRes<AlphaWriteFSqrtT, [!cast<ProcResourceKind>(NAME # "FPAdd")]> {
      let Latency = 33;
      let ReleaseAtCycles = [30];
    }

    def : ItinRW<[AlphaWriteILoad], [s_ild]>;
    def : ItinRW<[AlphaWriteFLoad], [s_fld]>;
    def : ItinRW<[AlphaWriteIStore], [s_ist]>;
    def : ItinRW<[AlphaWriteFStore], [s_fst]>;
    def : ItinRW<[AlphaWriteIAny], [s_lda, s_iadd, s_ilog, s_cmov]>;
    def : ItinRW<[AlphaWriteUpper], [s_ishf, s_icbr]>;
    def : ItinRW<[AlphaWriteLower], [s_rpcc, s_rx, s_mxpr, s_ubr, s_jsr]>;
    def : ItinRW<[AlphaWriteIMul], [s_imul]>;
    def : ItinRW<[AlphaWriteIMisc], [s_imisc, s_ftoi]>;
    def : ItinRW<[AlphaWriteFAdd], [s_fbr, s_fadd, s_fcmov, s_itof]>;
    def : ItinRW<[AlphaWriteFMul], [s_fmul]>;
    def : ItinRW<[AlphaWriteFDivS], [s_fdivs]>;
    def : ItinRW<[AlphaWriteFDivT], [s_fdivt]>;
    def : ItinRW<[AlphaWriteFSqrtS], [s_fsqrts]>;
    def : ItinRW<[AlphaWriteFSqrtT], [s_fsqrtt]>;
  }
}

defm EV4 : AlphaSchedResources<Alpha21064Model, 3>;
defm EV5 : AlphaSchedResources<Alpha21164Model, 2>;
defm EV6 : AlphaSchedResources<Alpha21264Model, 3>;
defm EV7 : AlphaSchedResources<Alpha21364Model, 3>;
