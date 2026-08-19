set(LLVM_LINK_COMPONENTS
  Option
  Support
  )

set(LLVM_TARGET_DEFINITIONS Opts.td)

tablegen(LLVM Opts.inc -gen-opt-parser-defs)
add_public_tablegen_target(MsMcOptsTableGen)

add_llvm_tool(llvm-ms-mc
  llvm-ms-mc.cpp
  DEPENDS
  MsMcOptsTableGen
  GENERATE_DRIVER
  )
