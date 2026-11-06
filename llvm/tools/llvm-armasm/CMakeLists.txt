if(NOT "AArch64" IN_LIST LLVM_TARGETS_TO_BUILD AND
   NOT "ARM" IN_LIST LLVM_TARGETS_TO_BUILD)
  return()
endif()

set(LLVM_LINK_COMPONENTS
  MC
  MCParser
  Option
  Support
  TargetParser
  )
foreach(target AArch64 ARM)
  if(target IN_LIST LLVM_TARGETS_TO_BUILD)
    list(APPEND LLVM_LINK_COMPONENTS
      ${target}AsmParser ${target}Desc ${target}Info)
  endif()
endforeach()

set(LLVM_TARGET_DEFINITIONS Opts.td)
llvm_tablegen(Opts.inc -gen-opt-parser-defs)
add_public_tablegen_target(ARMASMTableGen)

add_llvm_tool(llvm-armasm
  llvm-armasm.cpp
  DEPENDS
  ARMASMTableGen
  GENERATE_DRIVER
  )

# The program name selects the mode: llvm-armasm64 assembles AArch64.
add_llvm_tool_symlink(llvm-armasm64 llvm-armasm)
