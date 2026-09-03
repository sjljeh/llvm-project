//===- PPCWin32ABIInfoTest.cpp - Windows PowerPC ABI policy tests ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "PPCWin32ABIInfo.h"
#include "PPCCallingConv.h"
#include "PPCRegisterInfo.h"
#include "gtest/gtest.h"

using namespace llvm;

namespace {

TEST(PPCWin32ABIInfoTest, FrameLayout) {
  EXPECT_EQ(4u, PPCWin32ABIInfo::WordSize);
  EXPECT_EQ(4u, PPCWin32ABIInfo::ReturnSaveOffset);
  EXPECT_EQ(8u, PPCWin32ABIInfo::TOCSaveOffset);
  EXPECT_EQ(24u, PPCWin32ABIInfo::ParameterAreaOffset);
  EXPECT_EQ(56u, PPCWin32ABIInfo::MinimumFrameSize);
  EXPECT_EQ(56u, PPCWin32ABIInfo::ensureMinimumFrameSize(24));
  EXPECT_EQ(72u, PPCWin32ABIInfo::ensureMinimumFrameSize(72));
}

TEST(PPCWin32ABIInfoTest, PositionalArgumentGPRs) {
  static const MCPhysReg Expected[] = {PPC::R3, PPC::R4, PPC::R5, PPC::R6,
                                       PPC::R7, PPC::R8, PPC::R9, PPC::R10};
  EXPECT_EQ(ArrayRef<MCPhysReg>(Expected), PPCWin32ABIInfo::getArgumentGPRs());

  for (unsigned I = 0; I != std::size(Expected); ++I)
    EXPECT_EQ(Expected[I], PPCWin32ABIInfo::getArgumentGPR(
                               PPCWin32ABIInfo::ParameterAreaOffset +
                               I * PPCWin32ABIInfo::WordSize));
  EXPECT_FALSE(PPCWin32ABIInfo::getArgumentGPR(20));
  EXPECT_FALSE(PPCWin32ABIInfo::getArgumentGPR(25));
  EXPECT_FALSE(PPCWin32ABIInfo::getArgumentGPR(56));
}

TEST(PPCWin32ABIInfoTest, ParameterStreamAlignment) {
  unsigned Offset = PPCWin32ABIInfo::ParameterAreaOffset;
  EXPECT_EQ(24u, PPCWin32ABIInfo::allocateArgument(Offset, MVT::i32));
  EXPECT_EQ(28u, Offset);
  EXPECT_EQ(32u, PPCWin32ABIInfo::allocateArgument(Offset, MVT::f64));
  EXPECT_EQ(40u, Offset);
  EXPECT_EQ(40u, PPCWin32ABIInfo::allocateArgument(Offset, MVT::f32));
  EXPECT_EQ(44u, Offset);
  EXPECT_EQ(48u, PPCWin32ABIInfo::allocateArgument(Offset, MVT::i64));
  EXPECT_EQ(56u, Offset);
}

TEST(PPCWin32ABIInfoTest, ReturnAssignment) {
  EXPECT_EQ(RetCC_PPC, PPCWin32ABIInfo::getReturnAssignFn(CallingConv::C));
  EXPECT_EQ(RetCC_PPC_Cold,
            PPCWin32ABIInfo::getReturnAssignFn(CallingConv::Cold));
}

} // end anonymous namespace
