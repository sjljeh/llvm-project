//===--- Alpha.h - Declare Alpha target feature support ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_BASIC_TARGETS_ALPHA_H
#define LLVM_CLANG_LIB_BASIC_TARGETS_ALPHA_H

#include "OSTargets.h"
#include "clang/Basic/TargetOptions.h"

namespace clang {
namespace targets {

class LLVM_LIBRARY_VISIBILITY AlphaTargetInfo : public TargetInfo {
  static const char *const GCCRegNames[];
  static const TargetInfo::GCCRegAlias GCCRegAliases[];
  std::string CPU;

public:
  AlphaTargetInfo(const llvm::Triple &Triple, const TargetOptions &Opts)
      : TargetInfo(Triple) {
    LongDoubleWidth = LongDoubleAlign = 128;
    resetDataLayout("e-p:64:64-f128:128:128-n64");
  }

  void getTargetDefines(const LangOptions &Opts,
                        MacroBuilder &Builder) const override;

  ArrayRef<const char *> getGCCRegNames() const override;

  ArrayRef<TargetInfo::GCCRegAlias> getGCCRegAliases() const override;

  bool isValidCPUName(StringRef Name) const override;

  void fillValidCPUList(SmallVectorImpl<StringRef> &Values) const override;

  bool setCPU(StringRef Name) override {
    if (!isValidCPUName(Name))
      return false;
    CPU = Name.str();
    return true;
  }

  bool hasFeature(StringRef Feature) const override {
    return Feature == "alpha";
  }

  BuiltinVaListKind getBuiltinVaListKind() const override {
    return TargetInfo::VoidPtrBuiltinVaList;
  }

  llvm::SmallVector<Builtin::InfosShard> getTargetBuiltins() const override {
    return {};
  }

  bool validateAsmConstraint(const char *&Name,
                             TargetInfo::ConstraintInfo &Info) const override {
    return false;
  }

  std::string_view getClobbers() const override { return ""; }
};

class LLVM_LIBRARY_VISIBILITY Alpha32TargetInfo : public AlphaTargetInfo {
public:
  Alpha32TargetInfo(const llvm::Triple &Triple, const TargetOptions &Opts)
      : AlphaTargetInfo(Triple, Opts) {
    PointerWidth = PointerAlign = 32;
    LongWidth = LongAlign = 32;
    SizeType = UnsignedInt;
    PtrDiffType = IntPtrType = SignedInt;
    LongDoubleWidth = LongDoubleAlign = 64;
    LongDoubleFormat = &llvm::APFloat::IEEEdouble();
    resetDataLayout("e-p:32:32-f64:64-n32:64");
  }
};

class LLVM_LIBRARY_VISIBILITY Alpha64TargetInfo : public AlphaTargetInfo {
public:
  Alpha64TargetInfo(const llvm::Triple &Triple, const TargetOptions &Opts)
      : AlphaTargetInfo(Triple, Opts) {
    PointerWidth = PointerAlign = 64;
    LongWidth = LongAlign = 64;
    LongDoubleWidth = LongDoubleAlign = 128;
    resetDataLayout("e-p:64:64-f128:128:128-n64");
  }
};

class LLVM_LIBRARY_VISIBILITY WindowsAlphaTargetInfo
    : public WindowsTargetInfo<Alpha32TargetInfo> {
public:
  WindowsAlphaTargetInfo(const llvm::Triple &Triple, const TargetOptions &Opts)
      : WindowsTargetInfo<Alpha32TargetInfo>(Triple, Opts) {}

  BuiltinVaListKind getBuiltinVaListKind() const override {
    return TargetInfo::CharPtrBuiltinVaList;
  }

  CallingConvCheckResult
  checkCallingConvention(CallingConv CC) const override {
    switch (CC) {
    case CC_X86StdCall:
    case CC_X86FastCall:
    case CC_X86ThisCall:
      return CCCR_Ignore;
    default:
      return Alpha32TargetInfo::checkCallingConvention(CC);
    }
  }

};

class LLVM_LIBRARY_VISIBILITY MicrosoftAlphaTargetInfo
    : public WindowsAlphaTargetInfo {
public:
  MicrosoftAlphaTargetInfo(const llvm::Triple &Triple,
                           const TargetOptions &Opts)
      : WindowsAlphaTargetInfo(Triple, Opts) {
    TheCXXABI.set(TargetCXXABI::Microsoft);
  }

  void getTargetDefines(const LangOptions &Opts,
                        MacroBuilder &Builder) const override {
    WindowsAlphaTargetInfo::getTargetDefines(Opts, Builder);
    Builder.defineMacro("__ALPHA");
    Builder.defineMacro("__Alpha_AXP");
    Builder.defineMacro("_ALPHA32_");
    Builder.defineMacro("_M_ALPHA");
    Builder.defineMacro("_M_ALPHA32"); // not defined until VC6?
  }
};

class LLVM_LIBRARY_VISIBILITY WindowsAlpha64TargetInfo
    : public WindowsTargetInfo<Alpha64TargetInfo> {
public:
  WindowsAlpha64TargetInfo(const llvm::Triple &Triple,
                           const TargetOptions &Opts)
      : WindowsTargetInfo<Alpha64TargetInfo>(Triple, Opts) {
    LongWidth = LongAlign = 32;
    LongDoubleWidth = LongDoubleAlign = 64;
    LongDoubleFormat = &llvm::APFloat::IEEEdouble();
    SizeType = UnsignedLongLong;
    PtrDiffType = IntPtrType = SignedLongLong;
    IntMaxType = Int64Type = SignedLongLong;
    resetDataLayout("e-p:64:64-f64:64-n32:64");
  }

  BuiltinVaListKind getBuiltinVaListKind() const override {
    return TargetInfo::CharPtrBuiltinVaList;
  }

  CallingConvCheckResult
  checkCallingConvention(CallingConv CC) const override {
    switch (CC) {
    case CC_X86StdCall:
    case CC_X86FastCall:
    case CC_X86ThisCall:
      return CCCR_Ignore;
    default:
      return Alpha64TargetInfo::checkCallingConvention(CC);
    }
  }

  void getTargetDefines(const LangOptions &Opts,
                        MacroBuilder &Builder) const override {
    WindowsTargetInfo<Alpha64TargetInfo>::getTargetDefines(Opts, Builder);
    Builder.defineMacro("_WIN64");
  }
};

class LLVM_LIBRARY_VISIBILITY MicrosoftAlpha64TargetInfo
    : public WindowsAlpha64TargetInfo {
public:
  MicrosoftAlpha64TargetInfo(const llvm::Triple &Triple,
                             const TargetOptions &Opts)
      : WindowsAlpha64TargetInfo(Triple, Opts) {
    TheCXXABI.set(TargetCXXABI::Microsoft);
  }

  void getTargetDefines(const LangOptions &Opts,
                        MacroBuilder &Builder) const override {
    WindowsAlpha64TargetInfo::getTargetDefines(Opts, Builder);
    Builder.defineMacro("__ALPHA");
    Builder.defineMacro("__Alpha_AXP");

    Builder.defineMacro("_M_ALPHA");
    Builder.defineMacro("_M_ALPHA64");
  }
};

} // namespace targets
} // namespace clang

#endif // LLVM_CLANG_LIB_BASIC_TARGETS_ALPHA_H
