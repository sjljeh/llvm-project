add_llvm_component_library(LLVMAlphaDesc
  AlphaAsmBackend.cpp
  AlphaAsmParser.cpp
  AlphaELFObjectWriter.cpp
  AlphaInstPrinter.cpp
  AlphaMCCodeEmitter.cpp
  AlphaMCTargetDesc.cpp
  AlphaMCAsmInfo.cpp
  AlphaWinCOFFObjectWriter.cpp
  AlphaWinCOFFStreamer.cpp

  LINK_COMPONENTS
  AlphaInfo
  MC
  MCParser
  Support
  TargetParser

  ADD_TO_COMPONENT
  Alpha
  )
