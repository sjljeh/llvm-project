add_llvm_component_group(Alpha)

set(LLVM_TARGET_DEFINITIONS Alpha.td)

tablegen(LLVM AlphaGenRegisterInfo.inc -gen-register-info)
tablegen(LLVM AlphaGenInstrInfo.inc -gen-instr-info)
tablegen(LLVM AlphaGenSubtargetInfo.inc -gen-subtarget)
tablegen(LLVM AlphaGenAsmWriter.inc -gen-asm-writer)
tablegen(LLVM AlphaGenCallingConv.inc -gen-callingconv)
tablegen(LLVM AlphaGenDAGISel.inc -gen-dag-isel)
add_public_tablegen_target(AlphaCommonTableGen)

set(LLVM_OPTIONAL_SOURCES
  AlphaAsmPrinter.cpp
  AlphaBranchSelector.cpp
  AlphaFrameLowering.cpp
  AlphaInstrInfo.cpp
  AlphaISelDAGToDAG.cpp
  AlphaISelLowering.cpp
  AlphaLLRP.cpp
  AlphaRegisterInfo.cpp
  AlphaSelectionDAGInfo.cpp
  AlphaSubtarget.cpp
  )

add_llvm_target(AlphaCodeGen
  AlphaAsmPrinter.cpp
  AlphaBranchSelector.cpp
  AlphaFrameLowering.cpp
  AlphaInstrInfo.cpp
  AlphaISelDAGToDAG.cpp
  AlphaISelLowering.cpp
  AlphaLLRP.cpp
  AlphaRegisterInfo.cpp
  AlphaSelectionDAGInfo.cpp
  AlphaSubtarget.cpp
  AlphaTargetMachine.cpp

  LINK_COMPONENTS
  AlphaDesc
  AlphaInfo
  AsmPrinter
  CodeGen
  Core
  MC
  SelectionDAG
  Support
  Target
  TargetParser

  ADD_TO_COMPONENT
  Alpha
  )

add_subdirectory(TargetInfo)
add_subdirectory(MCTargetDesc)
