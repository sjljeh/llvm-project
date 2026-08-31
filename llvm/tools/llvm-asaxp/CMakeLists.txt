if(NOT "Alpha" IN_LIST LLVM_TARGETS_TO_BUILD)
  return()
endif()

set(LLVM_LINK_COMPONENTS
  AlphaDesc
  AlphaInfo
  MC
  MCParser
  Option
  Support
  TargetParser
  )

set(LLVM_TARGET_DEFINITIONS Opts.td)
tablegen(LLVM Opts.inc -gen-opt-parser-defs)
add_public_tablegen_target(AsAxpOptsTableGen)

add_llvm_tool(llvm-asaxp
  llvm-asaxp.cpp
  DEPENDS
  AsAxpOptsTableGen
  GENERATE_DRIVER
  )
