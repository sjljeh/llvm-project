## Hello! 
This is my LLVM fork in which I aim to improve compatibility with Microsoft tools broadly.

Work is happening here on the main branch. I update the patch set when I see fit with upstream LLVM, which often sees fixes trickle in for the same work that happens here. A typical rebase averages 3 merge conflicts per week of upstream work.

### Work items done:
* New program llvm-armasm64. Aims for compatibility with the Microsoft ARM64 assembler (armasm64.exe).
* Much improved compatibility in llvm-ml64 against Microsoft's AMD64 assembler (ml64.exe).
* Well-featured C++ EH4 / FH4 support.
* MSVC-flavored PowerPC, MIPS and Alpha Windows support with legacy __CxxFrameHandler C++ SEH.
* GNU-flavored PowerPC, MIPS and Alpha Windows assemblers.
* lld-link compatibility fixes.
* New program llvm-ms-mc. Aims for compatibility with the Microsoft Message Compiler (mc.exe).
* Improved compatibility in llvm-rc against Microsoft's Resource Compiler (rc.exe).
* Misc improvements with MSVC-like template handling
* Misc improvements with MSVC-like token parsing
* Misc improvements with MSVC-like SEH

### What is tested:
* Removing all references to Clang in ReactOS and fully pretending to be MSVC and successfully assembling, compiling, linking and running AMD64 ReactOS, with the full suite of tools (llvm-ms-mc, llvm-rc, llvm-ml64, lld-link, clang-cl, ...)
* Compiling and running userspace Windows SDK sample programs from the Alpha and MIPS SDKs and running them on emulated machines

### What is NOT tested:
* Utilizing the llvm-armasm64 assembler in real-world programs
* Anything related to PowerPC

### Items to be worked on in the future
* llvm-ms-asaxp as the Microsoft-flavored Alpha assembler.
* llvm-ms-asmips as the Microsoft-flavored MIPS assembler.
* llvm-ms-asppc as the Microsoft-flavored PowerPC assembler.
And any other items that come up in my work/interests.

### Disclaimer.
These improvements are highly assisted by LLM. However I have done my best in ensuring excellent input and testing where possible. My goal here is to leave everyone with a much better fork to work with than nothing at all. Feel free to file bugs or feature requests.

# The LLVM Compiler Infrastructure

[![OpenSSF Scorecard](https://api.securityscorecards.dev/projects/github.com/llvm/llvm-project/badge)](https://securityscorecards.dev/viewer/?uri=github.com/llvm/llvm-project)
[![OpenSSF Best Practices](https://www.bestpractices.dev/projects/8273/badge)](https://www.bestpractices.dev/projects/8273)
[![libc++](https://github.com/llvm/llvm-project/actions/workflows/libcxx-pr-conformance-tests.yaml/badge.svg?branch=main&event=schedule)](https://github.com/llvm/llvm-project/actions/workflows/libcxx-pr-conformance-tests.yaml?query=event%3Aschedule)

Welcome to the LLVM project!

This repository contains the source code for LLVM, a toolkit for the
construction of highly optimized compilers, optimizers, and run-time
environments.

The LLVM project has multiple components. The core of the project is
itself called "LLVM". This contains all of the tools, libraries, and header
files needed to process intermediate representations and convert them into
object files. Tools include an assembler, disassembler, bitcode analyzer, and
bitcode optimizer.

C-like languages use the [Clang](https://clang.llvm.org/) frontend. This
component compiles C, C++, Objective-C, and Objective-C++ code into LLVM bitcode
-- and from there into object files, using LLVM.

Other components include:
the [libc++ C++ standard library](https://libcxx.llvm.org),
the [LLD linker](https://lld.llvm.org), and more.

## Getting the Source Code and Building LLVM

Consult the
[Getting Started with LLVM](https://llvm.org/docs/GettingStarted.html#getting-the-source-code-and-building-llvm)
page for information on building and running LLVM.

For information on how to contribute to the LLVM project, please take a look at
the [Contributing to LLVM](https://llvm.org/docs/Contributing.html) guide.

## Getting in touch

Join the [LLVM Discourse forums](https://discourse.llvm.org/), [Discord
chat](https://discord.gg/xS7Z362),
[LLVM Office Hours](https://llvm.org/docs/GettingInvolved.html#office-hours) or
[Regular sync-ups](https://llvm.org/docs/GettingInvolved.html#online-sync-ups).

The LLVM project has adopted a [code of conduct](https://llvm.org/docs/CodeOfConduct.html) for
participants to all modes of communication within the project.
