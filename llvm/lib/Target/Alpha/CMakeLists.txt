add_llvm_component_group(Alpha)

set(LLVM_TARGET_DEFINITIONS Alpha.td)

llvm_tablegen(AlphaGenRegisterInfo.inc -gen-register-info)
llvm_tablegen(AlphaGenInstrInfo.inc -gen-instr-info)
llvm_tablegen(AlphaGenSubtargetInfo.inc -gen-subtarget)
llvm_tablegen(AlphaGenAsmWriter.inc -gen-asm-writer)
llvm_tablegen(AlphaGenCallingConv.inc -gen-callingconv)
llvm_tablegen(AlphaGenDAGISel.inc -gen-dag-isel)
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
