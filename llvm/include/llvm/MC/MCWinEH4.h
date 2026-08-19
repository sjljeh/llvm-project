//===- MCWinEH4.h - Microsoft C++ EH4 encoding helpers ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCWINEH4_H
#define LLVM_MC_MCWINEH4_H

#include <cstdint>

namespace llvm {

/// Encode the variable-length unsigned integer used by __CxxFrameHandler4.
/// \p Buffer must have space for five bytes.
inline unsigned encodeWinEH4Unsigned(uint32_t Value, uint8_t *Buffer) {
  if (Value < (1U << 7)) {
    Buffer[0] = uint8_t(Value << 1);
    return 1;
  }
  if (Value < (1U << 14)) {
    Buffer[0] = uint8_t((Value << 2) | 1);
    Buffer[1] = uint8_t(Value >> 6);
    return 2;
  }
  if (Value < (1U << 21)) {
    Buffer[0] = uint8_t((Value << 3) | 3);
    Buffer[1] = uint8_t(Value >> 5);
    Buffer[2] = uint8_t(Value >> 13);
    return 3;
  }
  if (Value < (1U << 28)) {
    Buffer[0] = uint8_t((Value << 4) | 7);
    Buffer[1] = uint8_t(Value >> 4);
    Buffer[2] = uint8_t(Value >> 12);
    Buffer[3] = uint8_t(Value >> 20);
    return 4;
  }
  Buffer[0] = 0x0f;
  Buffer[1] = uint8_t(Value);
  Buffer[2] = uint8_t(Value >> 8);
  Buffer[3] = uint8_t(Value >> 16);
  Buffer[4] = uint8_t(Value >> 24);
  return 5;
}

} // namespace llvm

#endif // LLVM_MC_MCWINEH4_H
