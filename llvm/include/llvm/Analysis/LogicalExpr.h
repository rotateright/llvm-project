//===------------------- LogicalExpr.h --------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// A example for the LogicalExpr caculation: For source values {a,b,c,d}, we can
// represent them as a bitmask with 'a' as the least-significant-bit: {dcba}.
// LHS is (a * b * c * d + a * d + b + a * c * d), RHS is (a + a * c).
// Use bit mask to represent the expression:
// {0b1111, 0b1001, 0b0010 , 0b1101} * {0b0001, 0b0101}
// -->
// (0b1111 + 0b1001 + 0b0010 + 0b1101) * (0b0001 + 0b0101)
// -->
// (0b1111 + 0b1001 + 0b0010 + 0b1101) * 0b0001+ (0b1111 + 0b1001 + 0b0010 +
// 0b1101) * 0b0101
// -->
// (0b1111 | 0b0001) + (0b1001 | 0b0001) + (0b0010 | 0b0001) + (0b1101 | 0b0001)
// + (0b1111 | 0b0101) + (0b1001 | 0b0101) + (0b0010 | 0b0101) + (0b1101 |
// 0b0101)
// -->
// 0b1111 + 0b1001 + 0b0010 + 0b1101 + 0b1111 + 0b1101 + 0b0111 + 0b1101
// -->
// 0b1001 + 0b0010 + 0b1101 + 0b0111
// -->
// {0b1001, 0b0010, 0b1101, 0b0111}
// -->
// a * d + b + a * c * d + a * b * c
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/DenseSet.h"

namespace llvm {
typedef SmallDenseSet<uint64_t, 8> ExprAddChain;

class LogicalExpr {
private:
  ExprAddChain AddChain;
  uint64_t LeafMask;

  inline void updateLeafMask() {
    LeafMask = 0;
    for (auto Mask : AddChain)
      LeafMask |= Mask;
  }

public:
  static const uint64_t ExprAllOne = 0x8000000000000000;
  static const uint64_t ExprZero = 0x4000000000000000;

  LogicalExpr() {}
  LogicalExpr(uint64_t Mask) {
    AddChain.insert(Mask);
    LeafMask = Mask;
  }
  LogicalExpr(const ExprAddChain &SrcAddChain) : AddChain(SrcAddChain) {
    updateLeafMask();
  }

  unsigned size() const { return AddChain.size(); }
  ExprAddChain::iterator begin() { return AddChain.begin(); }
  ExprAddChain::iterator end() { return AddChain.end(); }
  ExprAddChain::const_iterator begin() const { return AddChain.begin(); }
  ExprAddChain::const_iterator end() const { return AddChain.end(); }

  LogicalExpr &operator*=(const LogicalExpr &RHS) {
    ExprAddChain NewChain;
    for (auto LHSMask : AddChain) {
      // a & 0 -> 0
      if (LHSMask & ExprZero)
        continue;
      for (auto RHSMask : RHS.AddChain) {
        // 0 & a -> 0
        if (RHSMask & ExprZero)
          continue;
        uint64_t NewMask = LHSMask | RHSMask;
        // a & 1 -> a
        if (NewMask != ExprAllOne && ((NewMask & ExprAllOne) != 0))
          NewMask &= ~ExprAllOne;
        // a ^ a -> 0
        if (!NewChain.insert(NewMask).second)
          NewChain.erase(NewMask);
      }
    }

    AddChain = NewChain;
    updateLeafMask();
    return *this;
  }

  LogicalExpr &operator+=(const LogicalExpr &RHS) {
    for (auto RHSMask : RHS.AddChain) {
      // a ^ a -> 0
      if (!AddChain.insert(RHSMask).second)
        AddChain.erase(RHSMask);
    }
    updateLeafMask();
    return *this;
  }
};

inline LogicalExpr operator*(LogicalExpr a, const LogicalExpr &b) {
  a *= b;
  return a;
}

inline LogicalExpr operator+(LogicalExpr a, const LogicalExpr &b) {
  a += b;
  return a;
}

inline LogicalExpr operator&(const LogicalExpr &a, const LogicalExpr &b) {
  return a * b;
}

inline LogicalExpr operator^(const LogicalExpr &a, const LogicalExpr &b) {
  return a + b;
}

inline LogicalExpr operator|(const LogicalExpr &a, const LogicalExpr &b) {
  return a * b + a + b;
}

inline LogicalExpr operator~(const LogicalExpr &a) {
  LogicalExpr AllOneExpr(LogicalExpr::ExprAllOne);
  return a + AllOneExpr;
}

} // namespace llvm
