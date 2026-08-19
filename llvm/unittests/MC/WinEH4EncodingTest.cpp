//===- WinEH4EncodingTest.cpp ---------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/ArrayRef.h"
#include "llvm/MC/MCWinEH4.h"
#include "gtest/gtest.h"

#include <array>
#include <cstdint>
#include <initializer_list>

using namespace llvm;

namespace {

static void checkEncoding(uint32_t Value,
                          std::initializer_list<uint8_t> Expected) {
  std::array<uint8_t, 5> Buffer{};
  unsigned Size = encodeWinEH4Unsigned(Value, Buffer.data());
  EXPECT_EQ(Size, Expected.size());
  EXPECT_EQ(ArrayRef(Buffer).take_front(Size), ArrayRef(Expected));
}

TEST(WinEH4EncodingTest, UnsignedBoundaries) {
  checkEncoding(0, {0x00});
  checkEncoding((1U << 7) - 1, {0xfe});
  checkEncoding(1U << 7, {0x01, 0x02});

  checkEncoding((1U << 14) - 1, {0xfd, 0xff});
  checkEncoding(1U << 14, {0x03, 0x00, 0x02});

  checkEncoding((1U << 21) - 1, {0xfb, 0xff, 0xff});
  checkEncoding(1U << 21, {0x07, 0x00, 0x00, 0x02});

  checkEncoding((1U << 28) - 1, {0xf7, 0xff, 0xff, 0xff});
  checkEncoding(1U << 28, {0x0f, 0x00, 0x00, 0x00, 0x10});
  checkEncoding(UINT32_MAX, {0x0f, 0xff, 0xff, 0xff, 0xff});
}

} // namespace
