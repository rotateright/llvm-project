//===------------------- LogicalExpr.h --------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// For the logical expression represent by mask, "*"/"&" is actually or the
// mask. For example, pattern "a & b" can be represented by logical expression
// "01 * 10". And the result for "a & b" can be represented by logical
// expression "11". So the operation "*"/"&" between two basic logical
// expressions(no "+"/"^", only and chain) is actually or their masks. There is
// one exception, if one of the operand is constant 0, the mask also represent
// as 0. We should force the result to 0 not or the masks.

// The calculation of pattern with "+"/"^" follow one rule, if their are the
// same masks, we can remove both of them. Like a ^ a, the logical expression is
// 1 + 1. We can eliminate them from the logical expression then the expression
// is empty that means it is constant zero.
//
// And we can use commutative, associative and distributive laws to caculate. A
// example for the LogicalExpr caculation:
//     ((a & b) | (a ^ c)) ^ (!(b & c) & a)
// Mask for the leafs are: a --> 001, b --> 010, c -->100
// First step is expand the pattern to:
//      (((a & b) & (a ^ c)) ^ (a & b) ^ (a ^ c)) ^ (((b & c) ^ -1) & a)
// Use logical expression to represent the pattern:
//      001 * 010 * (001 + 100) + 001 * 010 + 001 + 100 +
//      (010 * 100 + -1C) * 001
// Expression after distributive laws:
//      011 + 111 + 011 + 001 + 100 + 111 + 001
// Eliminate same masks
//      100
// Restore to value
//      c
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/DenseSet.h"

namespace llvm {
typedef SmallDenseSet<uint64_t, 8> ExprAddChain;

class LogicalExpr {
private:
  ExprAddChain AddChain;
  // TODO: can we use APInt define the mask to enlarge the max leaf number?
  uint64_t LeafMask;

  inline void updateLeafMask() {
    LeafMask = 0;
    for (auto Mask : AddChain)
      LeafMask |= Mask;
  }

public:
  static const uint64_t ExprAllOne = 0x8000000000000000;

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
      if (LHSMask == 0)
        continue;
      for (auto RHSMask : RHS.AddChain) {
        // 0 & a -> 0
        if (RHSMask == 0)
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
