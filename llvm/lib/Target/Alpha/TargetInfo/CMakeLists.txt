add_llvm_component_library(LLVMAlphaInfo
  AlphaTargetInfo.cpp

  LINK_COMPONENTS
  MC
  Support

  ADD_TO_COMPONENT
  Alpha
  )
