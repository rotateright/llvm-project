//===----- ComplexLogicalOpsCombine.h --------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "LogicalExpr.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"

namespace llvm {

class LogicalOpsHelper;

class LogicalOpNode {
private:
  LogicalOpsHelper *Helper;
  Value *Val;
  LogicalExpr Expr;
  // TODO: Add weight to measure cost for more than one use value

  void printValue(raw_ostream &OS, Value *Val) const;
  void printAndChain(raw_ostream &OS, uint64_t Mask) const;

public:
  LogicalOpNode(LogicalOpsHelper *OpsHelper, Value *SrcVal,
                const LogicalExpr &SrcExpr)
      : Helper(OpsHelper), Val(SrcVal), Expr(SrcExpr) {}
  ~LogicalOpNode() {}

  Value *getValue() const { return Val; }
  const LogicalExpr &getExpr() const { return Expr; }
  void print(raw_ostream &OS) const;
};

class LogicalOpsHelper {
public:
  LogicalOpsHelper() {}
  ~LogicalOpsHelper() { clear(); }

  Value *simplify(Value *Root);

private:
  friend class LogicalOpNode;

  SmallDenseMap<Value *, LogicalOpNode *, 16> LogicalOpNodes;
  SmallPtrSet<Value *, 8> LeafSet;
  SmallVector<Value *, 8> LeafValues;

  void clear();

  LogicalOpNode *visitLeafNode(Value *Val, unsigned Depth);
  LogicalOpNode *visitBinOp(BinaryOperator *BO, unsigned Depth);
  LogicalOpNode *getLogicalOpNode(Value *Val, unsigned Depth = 0);
  Value *logicalOpToValue(LogicalOpNode *Node);
};

inline raw_ostream &operator<<(raw_ostream &OS, const LogicalOpNode &I) {
  I.print(OS);
  return OS;
}

} // namespace llvm
