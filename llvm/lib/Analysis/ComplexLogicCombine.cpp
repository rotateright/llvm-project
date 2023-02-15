//===-------- ComplexLogicalOpsCombine.cpp -------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file help to find the simplest expression for a complex logic
// operation chain. We canonicalize all other ops to and/xor.
// For example:
//    a | b --> (a & b) ^ a ^ b
//    c ? a : b --> (c & a) ^ ((c ^ true) & b)
// We use a mask set to represent the expression. Any value that is not a logic
// operation is a leaf node. Leaf node is 1 bit in the mask. For example, we
// have source a, b, c. The mask for a is 1, b is 2 ,c is 4.
//     a & b & c --> {7}
//     a & b ^ c & a --> {3, 5}
//     a & b ^ c & a ^ b --> {3, 5, 2}
// Every mask is an and chain. The set of masks is a xor chain.
// Based on boolean ring, We can treat & as ring multiplication and ^ as ring
// addition. After that, any logic value can be represented by a unsigned set.
// For example:
//     r1 = (a | b) & c -> r1 = (a * b * c) + (a * c) + (b * c) -> {7, 5, 6}
// Final we need to rebuild the simplest pattern from the expression. For now,
// we only simplify the code when the expression is leaf or null.
//
// Reference: https://en.wikipedia.org/wiki/Boolean_ring
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/ComplexLogicCombine.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/IR/Constants.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"

using namespace llvm;

#define DEBUG_TYPE "complex-logic-combine"

STATISTIC(NumComplexLogicalOpsSimplified,
          "Number of complex logical operations simplified");

static cl::opt<unsigned> MaxLogicOpLeafsToScan(
    "clc-max-logic-leafs", cl::init(8), cl::Hidden,
    cl::desc("Max leafs of logic ops to scan for complex logical combine."));

static cl::opt<unsigned> MaxDepthLogicOpsToScan(
    "clc-max-depth", cl::init(8), cl::Hidden,
    cl::desc("Max depth of logic ops to scan for complex logical combine."));

void LogicalOpNode::printValue(raw_ostream &OS, Value *Val) const {
  if (auto *ConstVal = dyn_cast<Constant>(Val))
    OS << *ConstVal;
  else
    OS << Val->getName();
}

void LogicalOpNode::printAndChain(raw_ostream &OS, uint64_t Mask) const {
  if (Mask == LogicalExpr::ExprAllOne) {
    OS << "-1";
    return;
  }

  unsigned MulElementCnt = llvm::popcount(Mask);
  if (((Mask & LogicalExpr::ExprZero) != 0) || MulElementCnt == 0)
    return;

  if (MulElementCnt == 1) {
    printValue(OS, Helper->LeafValues[llvm::Log2_64(Mask)]);
    return;
  }

  unsigned MaskIdx;
  for (unsigned I = 1; I < MulElementCnt; I++) {
    MaskIdx = llvm::countr_zero(Mask);
    printValue(OS, Helper->LeafValues[MaskIdx]);
    OS << " * ";
    Mask -= (1ULL << MaskIdx);
  }
  MaskIdx = llvm::countr_zero(Mask);
  printValue(OS, Helper->LeafValues[MaskIdx]);
}

void LogicalOpNode::print(raw_ostream &OS) const {
  OS << *Val << " --> ";
  if (Expr.size() == 0) {
    OS << "0\n";
    return;
  }

  for (auto I = ++Expr.begin(); I != Expr.end(); I++) {
    printAndChain(OS, *I);
    OS << " + ";
  }

  printAndChain(OS, *Expr.begin());
  OS << "\n";
}

void LogicalOpsHelper::clear() {
  for (auto node : LogicalOpNodes)
    delete node.second;
  LogicalOpNodes.clear();
  LeafSet.clear();
  LeafValues.clear();
}

LogicalOpNode *LogicalOpsHelper::visitLeafNode(Value *Val, unsigned Depth) {
  // Depth is 0 means the root is not logical operation. We can't
  // do anything for that.
  if (Depth == 0 || LeafSet.size() > MaxLogicOpLeafsToScan)
    return nullptr;

  uint64_t ExprVal = 1ULL << LeafSet.size();
  // Constant Zero,AllOne are special leaf nodes. They involve
  // LogicalExpr's calculation so we must detect them at first.
  if (auto ConstVal = dyn_cast<ConstantInt>(Val)) {
    if (ConstVal->isZero())
      ExprVal = LogicalExpr::ExprZero;
    else if (ConstVal->isAllOnesValue())
      ExprVal = LogicalExpr::ExprAllOne;
  }
  if (ExprVal != LogicalExpr::ExprAllOne && ExprVal != LogicalExpr::ExprZero &&
      LeafSet.insert(Val).second)
    LeafValues.push_back(Val);
  LogicalOpNode *Node = new LogicalOpNode(this, Val, LogicalExpr(ExprVal));
  LogicalOpNodes[Val] = Node;
  return Node;
}

LogicalOpNode *LogicalOpsHelper::visitBinOp(BinaryOperator *BO,
                                            unsigned Depth) {
  // We can only to simpilfy and, or , xor in the binary operator
  if (BO->getOpcode() != Instruction::And &&
      BO->getOpcode() != Instruction::Or && BO->getOpcode() != Instruction::Xor)
    return visitLeafNode(BO, Depth);

  LogicalOpNode *LHS = getLogicalOpNode(BO->getOperand(0), Depth + 1);
  if (LHS == nullptr)
    return nullptr;

  LogicalOpNode *RHS = getLogicalOpNode(BO->getOperand(1), Depth + 1);
  if (RHS == nullptr)
    return nullptr;

  LogicalOpNode *Node;
  if (BO->getOpcode() == Instruction::And)
    Node = new LogicalOpNode(this, BO, LHS->getExpr() & RHS->getExpr());
  else if (BO->getOpcode() == Instruction::Or)
    Node = new LogicalOpNode(this, BO, LHS->getExpr() | RHS->getExpr());
  else
    Node = new LogicalOpNode(this, BO, LHS->getExpr() ^ RHS->getExpr());
  LogicalOpNodes[BO] = Node;
  return Node;
}

LogicalOpNode *LogicalOpsHelper::getLogicalOpNode(Value *Val, unsigned Depth) {
  if (Depth == MaxDepthLogicOpsToScan)
    return nullptr;

  if (LogicalOpNodes.find(Val) == LogicalOpNodes.end()) {
    LogicalOpNode *Node;

    // TODO: add select instruction support
    if (auto *BO = dyn_cast<BinaryOperator>(Val))
      Node = visitBinOp(BO, Depth);
    else
      Node = visitLeafNode(Val, Depth);

    if (!Node)
      return nullptr;
    LLVM_DEBUG(dbgs() << *Node);
  }
  return LogicalOpNodes[Val];
}

Value *LogicalOpsHelper::logicalOpToValue(LogicalOpNode *Node) {
  const LogicalExpr &Expr = Node->getExpr();
  // Empty happen when all masks are earsed from the set because of  a ^ a = 0.
  if (Expr.size() == 0)
    return Constant::getNullValue(Node->getValue()->getType());

  if (Expr.size() == 1) {
    uint64_t ExprMask = *Expr.begin();
    // ExprZero/ExprAllOne is not in the LeafValues
    if (ExprMask == LogicalExpr::ExprZero)
      return Constant::getNullValue(Node->getValue()->getType());
    if (ExprMask == LogicalExpr::ExprAllOne)
      return Constant::getAllOnesValue(Node->getValue()->getType());

    if (llvm::popcount(ExprMask) == 1)
      return LeafValues[llvm::Log2_64(ExprMask)];
  }

  // TODO: complex pattern simpilify

  return nullptr;
}

Value *LogicalOpsHelper::simplify(Value *Root) {
  assert(MaxLogicOpLeafsToScan <= 62 &&
         "Logical leaf node can't larger than 62.");
  LogicalOpNode *RootNode = getLogicalOpNode(Root);
  if (RootNode == nullptr)
    return nullptr;

  Value *NewRoot = logicalOpToValue(RootNode);
  if (NewRoot == Root)
    return nullptr;

  if (NewRoot != nullptr)
    NumComplexLogicalOpsSimplified++;
  return NewRoot;
}
