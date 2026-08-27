//===--- Alpha.cpp - Implement Alpha target feature support ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Alpha.h"
#include "clang/Basic/MacroBuilder.h"

using namespace clang;
using namespace clang::targets;

const char *const AlphaTargetInfo::GCCRegNames[] = {
    "$0",  "$1",  "$2",  "$3",  "$4",  "$5",  "$6",  "$7",
    "$8",  "$9",  "$10", "$11", "$12", "$13", "$14", "$15",
    "$16", "$17", "$18", "$19", "$20", "$21", "$22", "$23",
    "$24", "$25", "$26", "$27", "$28", "$29", "$30", "$31"};

const TargetInfo::GCCRegAlias AlphaTargetInfo::GCCRegAliases[] = {
    {{"v0"}, "$0"}, {{"gp"}, "$29"}, {{"sp"}, "$30"}, {{"zero"}, "$31"}};

ArrayRef<const char *> AlphaTargetInfo::getGCCRegNames() const {
  return llvm::ArrayRef(GCCRegNames);
}

ArrayRef<TargetInfo::GCCRegAlias> AlphaTargetInfo::getGCCRegAliases() const {
  return llvm::ArrayRef(GCCRegAliases);
}

static constexpr llvm::StringLiteral ValidCPUNames[] = {
    {"generic"}, {"ev4"}, {"ev5"}, {"ev56"}, {"pca56"}, {"ev6"}, {"ev7"},
    {"ev67"}};

bool AlphaTargetInfo::isValidCPUName(StringRef Name) const {
  return llvm::is_contained(ValidCPUNames, Name);
}

void AlphaTargetInfo::fillValidCPUList(SmallVectorImpl<StringRef> &Values) const {
  Values.append(std::begin(ValidCPUNames), std::end(ValidCPUNames));
}

void AlphaTargetInfo::getTargetDefines(const LangOptions &Opts,
                                       MacroBuilder &Builder) const {
  Builder.defineMacro("__alpha");
  Builder.defineMacro("__alpha__");
}
