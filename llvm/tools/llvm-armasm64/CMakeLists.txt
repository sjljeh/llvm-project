if(NOT "AArch64" IN_LIST LLVM_TARGETS_TO_BUILD)
  return()
endif()

set(LLVM_LINK_COMPONENTS
  AArch64AsmParser
  AArch64Desc
  AArch64Info
  MC
  MCParser
  Option
  Support
  TargetParser
  )

set(LLVM_TARGET_DEFINITIONS Opts.td)
tablegen(LLVM Opts.inc -gen-opt-parser-defs)
add_public_tablegen_target(ARMAsm64TableGen)

add_llvm_tool(llvm-armasm64
  llvm-armasm64.cpp
  DEPENDS
  ARMAsm64TableGen
  GENERATE_DRIVER
  )
