//===- PPCABIKindTest.cpp - PowerPC ABI classification tests --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "PPCSubtarget.h"
#include "PPCTargetMachine.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/TargetParser/Triple.h"
#include "gtest/gtest.h"
#include <optional>

using namespace llvm;

namespace {

struct ABIProperties {
  PPCABIKind Kind;
  bool UsesSVR4Registers;
  bool UsesPPC32SVR4Registers;
  bool UsesPPC64SVR4Registers;
  bool UsesWindowsCallingConvention;
  bool UsesWindowsVarArgs;
  bool UsesFunctionDescriptors;
  bool FunctionDescriptorHasEnvironmentPointer;
  bool UsesTOCBase;
};

class PPCABIKindTest : public ::testing::Test {
protected:
  static void SetUpTestCase() {
    LLVMInitializePowerPCTargetInfo();
    LLVMInitializePowerPCTarget();
    LLVMInitializePowerPCTargetMC();
  }

  static std::optional<ABIProperties> getABIProperties(StringRef TripleName) {
    Triple TT(Triple::normalize(TripleName));
    std::string Error;
    const Target *TheTarget = TargetRegistry::lookupTarget("", TT, Error);
    if (!TheTarget) {
      ADD_FAILURE() << Error;
      return std::nullopt;
    }

    TargetOptions Options;
    std::unique_ptr<TargetMachine> TM(TheTarget->createTargetMachine(
        TT, /*CPU=*/"", /*Features=*/"", Options, /*RM=*/std::nullopt,
        /*CM=*/CodeModel::Small, CodeGenOptLevel::Default));
    if (!TM) {
      ADD_FAILURE() << "Could not allocate target machine for " << TripleName;
      return std::nullopt;
    }

    LLVMContext Context;
    Module M("test", Context);
    Function *F =
        Function::Create(FunctionType::get(Type::getVoidTy(Context), false),
                         GlobalValue::ExternalLinkage, "f", M);
    const auto &PPCTM = static_cast<const PPCTargetMachine &>(*TM);
    const PPCSubtarget &ST = *PPCTM.getSubtargetImpl(*F);
    return ABIProperties{
        ST.getABIKind(),
        ST.usesSVR4RegisterConvention(),
        ST.usesPPC32SVR4RegisterConvention(),
        ST.usesPPC64SVR4RegisterConvention(),
        ST.usesWindowsCallingConvention(),
        ST.usesWindowsVarArgs(),
        ST.usesFunctionDescriptors(),
        ST.functionDescriptorHasEnvironmentPointer(),
        ST.usesTOCBase(),
    };
  }
};

TEST_F(PPCABIKindTest, ELF32) {
  std::optional<ABIProperties> P =
      getABIProperties("powerpc-unknown-linux-gnu");
  ASSERT_TRUE(P);
  EXPECT_EQ(PPCABIKind::ELF32, P->Kind);
  EXPECT_TRUE(P->UsesSVR4Registers);
  EXPECT_TRUE(P->UsesPPC32SVR4Registers);
  EXPECT_FALSE(P->UsesPPC64SVR4Registers);
  EXPECT_FALSE(P->UsesWindowsCallingConvention);
  EXPECT_FALSE(P->UsesWindowsVarArgs);
  EXPECT_FALSE(P->UsesFunctionDescriptors);
  EXPECT_FALSE(P->FunctionDescriptorHasEnvironmentPointer);
  EXPECT_FALSE(P->UsesTOCBase);
}

TEST_F(PPCABIKindTest, ELF64v1) {
  std::optional<ABIProperties> P =
      getABIProperties("powerpc64-unknown-linux-gnu");
  ASSERT_TRUE(P);
  EXPECT_EQ(PPCABIKind::ELF64v1, P->Kind);
  EXPECT_TRUE(P->UsesSVR4Registers);
  EXPECT_FALSE(P->UsesPPC32SVR4Registers);
  EXPECT_TRUE(P->UsesPPC64SVR4Registers);
  EXPECT_FALSE(P->UsesWindowsCallingConvention);
  EXPECT_FALSE(P->UsesWindowsVarArgs);
  EXPECT_TRUE(P->UsesFunctionDescriptors);
  EXPECT_TRUE(P->FunctionDescriptorHasEnvironmentPointer);
  EXPECT_TRUE(P->UsesTOCBase);
}

TEST_F(PPCABIKindTest, ELF64v2) {
  std::optional<ABIProperties> P =
      getABIProperties("powerpc64le-unknown-linux-gnu");
  ASSERT_TRUE(P);
  EXPECT_EQ(PPCABIKind::ELF64v2, P->Kind);
  EXPECT_TRUE(P->UsesSVR4Registers);
  EXPECT_FALSE(P->UsesPPC32SVR4Registers);
  EXPECT_TRUE(P->UsesPPC64SVR4Registers);
  EXPECT_FALSE(P->UsesWindowsCallingConvention);
  EXPECT_FALSE(P->UsesWindowsVarArgs);
  EXPECT_FALSE(P->UsesFunctionDescriptors);
  EXPECT_FALSE(P->FunctionDescriptorHasEnvironmentPointer);
  EXPECT_TRUE(P->UsesTOCBase);
}

TEST_F(PPCABIKindTest, Darwin) {
  std::optional<ABIProperties> P = getABIProperties("powerpc-apple-darwin");
  ASSERT_TRUE(P);
  EXPECT_EQ(PPCABIKind::Darwin, P->Kind);
  EXPECT_TRUE(P->UsesSVR4Registers);
  EXPECT_TRUE(P->UsesPPC32SVR4Registers);
  EXPECT_FALSE(P->UsesPPC64SVR4Registers);
  EXPECT_FALSE(P->UsesWindowsCallingConvention);
  EXPECT_FALSE(P->UsesWindowsVarArgs);
  EXPECT_FALSE(P->UsesFunctionDescriptors);
  EXPECT_FALSE(P->FunctionDescriptorHasEnvironmentPointer);
  EXPECT_FALSE(P->UsesTOCBase);
}

TEST_F(PPCABIKindTest, AIX) {
  std::optional<ABIProperties> P = getABIProperties("powerpc-ibm-aix");
  ASSERT_TRUE(P);
  EXPECT_EQ(PPCABIKind::AIX, P->Kind);
  EXPECT_FALSE(P->UsesSVR4Registers);
  EXPECT_FALSE(P->UsesPPC32SVR4Registers);
  EXPECT_FALSE(P->UsesPPC64SVR4Registers);
  EXPECT_FALSE(P->UsesWindowsCallingConvention);
  EXPECT_FALSE(P->UsesWindowsVarArgs);
  EXPECT_TRUE(P->UsesFunctionDescriptors);
  EXPECT_TRUE(P->FunctionDescriptorHasEnvironmentPointer);
  EXPECT_TRUE(P->UsesTOCBase);
}

TEST_F(PPCABIKindTest, Win32) {
  std::optional<ABIProperties> P =
      getABIProperties("powerpcle-pc-windows-msvc");
  ASSERT_TRUE(P);
  EXPECT_EQ(PPCABIKind::Win32, P->Kind);
  EXPECT_TRUE(P->UsesSVR4Registers);
  EXPECT_TRUE(P->UsesPPC32SVR4Registers);
  EXPECT_FALSE(P->UsesPPC64SVR4Registers);
  EXPECT_TRUE(P->UsesWindowsCallingConvention);
  EXPECT_TRUE(P->UsesWindowsVarArgs);
  EXPECT_TRUE(P->UsesFunctionDescriptors);
  EXPECT_FALSE(P->FunctionDescriptorHasEnvironmentPointer);
  EXPECT_TRUE(P->UsesTOCBase);
}

} // end of anonymous namespace
