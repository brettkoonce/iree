// Copyright 2019 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "iree/compiler/Dialect/Flow/IR/FlowOps.h"

#include "iree/compiler/Dialect/IREE/IR/IREETypes.h"
#include "llvm/ADT/StringExtras.h"
#include "mlir/IR/Attributes.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/Matchers.h"
#include "mlir/IR/OpDefinition.h"
#include "mlir/IR/OpImplementation.h"
#include "mlir/IR/PatternMatch.h"
#include "mlir/IR/StandardTypes.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/IR/TypeUtilities.h"
#include "mlir/Support/LogicalResult.h"

namespace mlir {
namespace iree_compiler {
namespace IREE {
namespace Flow {

// Returns true if the given |accessType| is compatible with the |variableType|.
// For example, this will return true if the variable type is a tensor<?xf32>
// and the access is tensor<4xf32>.
static bool isVariableTypeCompatible(Type variableType, Type accessType) {
  return succeeded(mlir::verifyCompatibleShape(variableType, accessType));
}

//===----------------------------------------------------------------------===//
// flow.variable
//===----------------------------------------------------------------------===//

static ParseResult parseVariableOp(OpAsmParser &parser,
                                   OperationState *result) {
  StringAttr nameAttr;
  if (failed(parser.parseSymbolName(nameAttr,
                                    mlir::SymbolTable::getSymbolAttrName(),
                                    result->attributes))) {
    return failure();
  }

  if (succeeded(parser.parseOptionalKeyword("mutable"))) {
    result->addAttribute("is_mutable", UnitAttr::get(result->getContext()));
  }

  if (succeeded(parser.parseOptionalKeyword("init"))) {
    FlatSymbolRefAttr initializerAttr;
    if (failed(parser.parseLParen()) ||
        failed(parser.parseAttribute(initializerAttr, "initializer",
                                     result->attributes)) ||
        failed(parser.parseRParen())) {
      return failure();
    }
  }

  if (failed(parser.parseOptionalColon())) {
    Attribute initialValueAttr;
    if (failed(parser.parseAttribute(initialValueAttr, "initial_value",
                                     result->attributes))) {
      return failure();
    }
    result->addAttribute("type", TypeAttr::get(initialValueAttr.getType()));
  } else {
    Type type;
    if (failed(parser.parseType(type))) {
      return failure();
    }
    result->addAttribute("type", TypeAttr::get(type));
  }

  return success();
}

static void printVariableOp(OpAsmPrinter &p, VariableOp op) {
  p << op.getOperationName() << ' ';
  p.printSymbolName(op.sym_name());
  if (op.is_mutable()) {
    p << " mutable";
  }
  if (op.initializer().hasValue()) {
    p << " init(";
    p.printSymbolName(op.initializer().getValue());
    p << ')';
  }
  if (op.initial_value().hasValue()) {
    p << ' ';
    p.printAttribute(op.initial_value().getValue());
  } else {
    p << " : ";
    p.printType(op.type());
  }
}

static LogicalResult verifyVariableOp(VariableOp op) {
  if (op.initializer().hasValue() && op.initial_value().hasValue()) {
    return op.emitOpError()
           << "variables can have either an initializer or an initial value";
  } else if (op.initializer().hasValue()) {
    // Ensure initializer returns the same type as the variable.
    auto *symbolOp =
        SymbolTable::lookupNearestSymbolFrom(op, op.initializer().getValue());
    if (!symbolOp) {
      return op.emitOpError() << "initializer function "
                              << op.initializer().getValue() << " not found";
    }
    auto initializerOp = dyn_cast<FuncOp>(symbolOp);
    if (initializerOp.getNumArguments() != 0 ||
        initializerOp.getNumResults() != 1 ||
        initializerOp.getType().getResult(0) != op.type()) {
      return op.emitOpError()
             << "initializer type mismatch; variable " << op.sym_name()
             << " is " << op.type() << " but initializer function "
             << initializerOp.getName() << " is " << initializerOp.getType();
    }
  } else if (op.initial_value().hasValue()) {
    // Ensure the value is something we can store in the variable
    if (!isVariableTypeCompatible(op.type(), op.initial_value()->getType())) {
      return op.emitOpError()
             << "initial value type mismatch; variable " << op.sym_name()
             << " is " << op.type() << " but initial value provided is "
             << op.initial_value()->getType();
    }
  }
  return success();
}

void VariableOp::build(Builder *builder, OperationState &state, StringRef name,
                       bool isMutable, FuncOp initializer,
                       ArrayRef<NamedAttribute> attrs) {
  state.addAttribute(SymbolTable::getSymbolAttrName(),
                     builder->getStringAttr(name));
  if (isMutable) {
    state.addAttribute("is_mutable", builder->getUnitAttr());
  }
  state.addAttribute("initializer", builder->getSymbolRefAttr(initializer));
  state.addAttribute("type", TypeAttr::get(initializer.getType().getResult(0)));
  state.attributes.append(attrs.begin(), attrs.end());
}

void VariableOp::build(Builder *builder, OperationState &result, StringRef name,
                       bool isMutable, Type type, Attribute initialValue,
                       ArrayRef<NamedAttribute> attrs) {
  result.addAttribute(SymbolTable::getSymbolAttrName(),
                      builder->getStringAttr(name));
  if (isMutable) {
    result.addAttribute("is_mutable", builder->getUnitAttr());
  }
  result.addAttribute("initial_value", initialValue);
  result.addAttribute("type", TypeAttr::get(type));
  result.attributes.append(attrs.begin(), attrs.end());
}

void VariableOp::build(Builder *builder, OperationState &result, StringRef name,
                       bool isMutable, Type type,
                       ArrayRef<NamedAttribute> attrs) {
  result.addAttribute(SymbolTable::getSymbolAttrName(),
                      builder->getStringAttr(name));
  if (isMutable) {
    result.addAttribute("is_mutable", builder->getUnitAttr());
  }
  result.addAttribute("type", TypeAttr::get(type));
  result.attributes.append(attrs.begin(), attrs.end());
}

//===----------------------------------------------------------------------===//
// flow.variable.load
//===----------------------------------------------------------------------===//

static ParseResult parseVariableLoadOp(OpAsmParser &parser,
                                       OperationState *result) {
  FlatSymbolRefAttr variableAttr;
  Type valueType;
  if (failed(parser.parseAttribute(variableAttr, "variable",
                                   result->attributes)) ||
      failed(parser.parseOptionalAttrDict(result->attributes)) ||
      failed(parser.parseColonType(valueType))) {
    return failure();
  }
  result->addTypes({valueType});
  return success();
}

static void printVariableLoadOp(OpAsmPrinter &p, VariableLoadOp &op) {
  p << op.getOperationName() << ' ';
  p.printSymbolName(op.variable());
  p.printOptionalAttrDict(op.getAttrs(), /*elidedAttrs=*/{"variable"});
  p << " : ";
  p.printType(op.result()->getType());
}

static LogicalResult verifyVariableLoadOp(VariableLoadOp &op) {
  auto *symbolOp = SymbolTable::lookupNearestSymbolFrom(op, op.variable());
  if (!symbolOp) {
    return op.emitOpError() << "undefined variable: " << op.variable();
  }
  auto variableOp = dyn_cast<VariableOp>(symbolOp);
  auto loadType = op.result()->getType();
  if (!isVariableTypeCompatible(variableOp.type(), loadType)) {
    return op.emitOpError()
           << "variable type mismatch; variable " << op.variable() << " is "
           << variableOp.type() << " but load is " << loadType;
  }
  return success();
}

//===----------------------------------------------------------------------===//
// flow.variable.store
//===----------------------------------------------------------------------===//

static ParseResult parseVariableStoreOp(OpAsmParser &parser,
                                        OperationState *result) {
  OpAsmParser::OperandType value;
  FlatSymbolRefAttr variableAttr;
  Type valueType;
  if (failed(parser.parseOperand(value)) || failed(parser.parseComma()) ||
      failed(parser.parseAttribute(variableAttr, "variable",
                                   result->attributes)) ||
      failed(parser.parseOptionalAttrDict(result->attributes)) ||
      failed(parser.parseColonType(valueType)) ||
      failed(parser.resolveOperand(value, valueType, result->operands))) {
    return failure();
  }
  return success();
}

static void printVariableStoreOp(OpAsmPrinter &p, VariableStoreOp &op) {
  p << op.getOperationName() << ' ';
  p.printOperand(op.value());
  p << ", ";
  p.printSymbolName(op.variable());
  p.printOptionalAttrDict(op.getAttrs(), /*elidedAttrs=*/{"variable"});
  p << " : ";
  p.printType(op.value()->getType());
}

static LogicalResult verifyVariableStoreOp(VariableStoreOp &op) {
  auto *symbolOp = SymbolTable::lookupNearestSymbolFrom(op, op.variable());
  if (!symbolOp) {
    return op.emitOpError() << "undefined variable: " << op.variable();
  }
  auto variableOp = dyn_cast<VariableOp>(symbolOp);
  auto storeType = op.value()->getType();
  if (!isVariableTypeCompatible(variableOp.type(), storeType)) {
    return op.emitOpError()
           << "variable type mismatch; variable " << op.variable() << " is "
           << variableOp.type() << " but store is " << storeType;
  }
  if (!variableOp.is_mutable()) {
    return op.emitOpError() << "variable " << op.variable()
                            << " is not mutable and cannot be stored to";
  }
  return success();
}

//===----------------------------------------------------------------------===//
// flow.dispatch.region
//===----------------------------------------------------------------------===//

void DispatchRegionOp::build(Builder *builder, OperationState &state,
                             ArrayRef<Type> resultTypes, Value workload,
                             ValueRange operands,
                             ArrayRef<NamedAttribute> attributes) {
  state.addTypes(resultTypes);
  state.addOperands({workload});
  state.addOperands(operands);
  state.addAttributes(attributes);
  state.addRegion();
  state.setOperandListToResizable();
}

ParseResult parseDispatchRegionOp(OpAsmParser &parser, OperationState *result) {
  // Parse required workload.
  OpAsmParser::OperandType workloadArg;
  Type workloadArgType;
  if (failed(parser.parseLSquare()) ||
      failed(parser.parseOperand(workloadArg)) ||
      failed(parser.parseColonType(workloadArgType)) ||
      failed(parser.parseRSquare()) ||
      failed(parser.resolveOperand(workloadArg, workloadArgType,
                                   result->operands))) {
    return failure();
  }

  // Parse (optional) args.
  SmallVector<OpAsmParser::OperandType, 16> regionArgs;
  SmallVector<Type, 16> regionArgTypes;
  if (failed(parser.parseLParen())) {
    return failure();
  }
  if (failed(parser.parseOptionalRParen())) {
    SmallVector<OpAsmParser::OperandType, 16> regionOperands;
    auto argsLoc = parser.getCurrentLocation();
    do {
      // Reserve entries in the lists.
      regionArgs.emplace_back();
      regionOperands.emplace_back();
      regionArgTypes.emplace_back();
      if (failed(parser.parseRegionArgument(regionArgs.back())) ||
          failed(parser.parseEqual()) ||
          failed(parser.parseOperand(regionOperands.back())) ||
          failed(parser.parseColonType(regionArgTypes.back()))) {
        return failure();
      }
    } while (succeeded(parser.parseOptionalComma()));
    if (failed(parser.parseRParen()) ||
        failed(parser.resolveOperands(regionOperands, regionArgTypes, argsLoc,
                                      result->operands))) {
      return failure();
    }
  }
  result->setOperandListToResizable();

  // Parse (optional) results.
  if (failed(parser.parseOptionalArrowTypeList(result->types))) {
    return failure();
  }

  // Parse region body.
  Region *body = result->addRegion();
  if (failed(parser.parseRegion(*body, regionArgs, regionArgTypes)) ||
      failed(parser.parseOptionalAttrDict(result->attributes))) {
    return failure();
  }
  return success();
}

void printDispatchRegionOp(OpAsmPrinter &p, DispatchRegionOp op) {
  p << op.getOperationName();

  // Print the workload argument.
  p << "[";
  p.printOperand(op.workload());
  p << " : ";
  p.printType(op.workload()->getType());
  p << "]";

  // Print the data argument remapping.
  p << "(";
  interleaveComma(llvm::zip(op.body().front().getArguments(), op.args()), p,
                  [&](std::tuple<BlockArgument, Value> it) {
                    p << *std::get<0>(it) << " = " << *std::get<1>(it);
                    p << " : ";
                    p << std::get<1>(it)->getType();
                  });
  p << ")";

  // Print the result types, if any.
  if (op.getNumResults() > 0) {
    p << " -> ";
    if (op.getNumResults() > 1) p << "(";
    interleaveComma(op.getResultTypes(), p);
    if (op.getNumResults() > 1) p << ")";
  }

  p.printRegion(op.body(), /*printEntryBlockArgs=*/false);
  p.printOptionalAttrDict(op.getAttrs(),
                          /*elidedAttrs=*/{});
}

//===----------------------------------------------------------------------===//
// flow.reduction.region
//===----------------------------------------------------------------------===//

void ReductionRegionOp::build(Builder *builder, OperationState &state,
                              ArrayRef<Type> resultTypes, Value workload,
                              ValueRange operands, ValueRange initialValues,
                              ArrayRef<int32_t> dimensions,
                              ArrayRef<NamedAttribute> attributes) {
  state.addTypes(resultTypes);
  state.addOperands({workload});
  state.addOperands(operands);
  state.addOperands(initialValues);
  state.addAttribute(
      "dimensions",
      DenseIntElementsAttr::get(
          VectorType::get({static_cast<int32_t>(dimensions.size())},
                          builder->getIntegerType(32)),
          dimensions));
  state.addAttributes(attributes);
  state.addRegion();
  state.setOperandListToResizable();
}

ParseResult parseReductionRegionOp(OpAsmParser &parser,
                                   OperationState *result) {
  OpAsmParser::OperandType workloadArg;
  Type workloadArgType;
  if (failed(parser.parseLSquare()) ||
      failed(parser.parseOperand(workloadArg)) ||
      failed(parser.parseColonType(workloadArgType)) ||
      failed(parser.parseRSquare()) ||
      failed(parser.resolveOperand(workloadArg, workloadArgType,
                                   result->operands))) {
    return failure();
  }

  SmallVector<OpAsmParser::OperandType, 8> reductionOperands;
  Type reductionType;
  auto operandsLoc = parser.getCurrentLocation();
  if (failed(parser.parseLParen()) ||
      failed(parser.parseOperandList(reductionOperands)) ||
      failed(parser.parseRParen()) ||
      failed(parser.parseColonType(reductionType)) ||
      failed(parser.resolveOperands(
          reductionOperands, reductionType.cast<FunctionType>().getInputs(),
          operandsLoc, result->operands))) {
    return failure();
  }
  for (auto type : reductionType.cast<FunctionType>().getResults()) {
    result->types.push_back(type);
  }
  result->setOperandListToResizable();

  SmallVector<OpAsmParser::OperandType, 8> regionArgs;
  SmallVector<Type, 8> regionArgTypes;
  if (failed(parser.parseKeyword("invocation")) ||
      failed(parser.parseLParen())) {
    return failure();
  }
  do {
    Type argType;
    SmallVector<OpAsmParser::OperandType, 2> reductionRegionArgs;
    OpAsmParser::OperandType initialValue;
    if (failed(parser.parseLParen()) ||
        failed(parser.parseOperandList(reductionRegionArgs, 2)) ||
        failed(parser.parseRParen()) || failed(parser.parseEqual()) ||
        failed(parser.parseOperand(initialValue)) ||
        failed(parser.parseColonType(argType)) ||
        failed(
            parser.resolveOperand(initialValue, argType, result->operands))) {
      return failure();
    }
    regionArgs.push_back(reductionRegionArgs[0]);
    regionArgTypes.push_back(argType);
    regionArgs.push_back(reductionRegionArgs[1]);
    regionArgTypes.push_back(argType);
  } while (succeeded(parser.parseOptionalComma()));
  if (failed(parser.parseRParen())) {
    return failure();
  }

  // Parse region body.
  Region *body = result->addRegion();
  if (failed(parser.parseRegion(*body, regionArgs, regionArgTypes)) ||
      failed(parser.parseOptionalAttrDict(result->attributes))) {
    return failure();
  }

  return success();
}

void printReductionRegionOp(OpAsmPrinter &p, ReductionRegionOp op) {
  p << op.getOperationName();

  // Print the workload argument.
  p << "[";
  p.printOperand(op.workload());
  p << " : ";
  p.printType(op.workload()->getType());
  p << "]";

  p << "(";
  p.printOperands(op.operands());
  p << ")";
  if (op.getNumResults() > 0) {
    p << " : (";
    interleaveComma(op.operands(), p,
                    [&](Value operand) { p.printType(operand->getType()); });
    p << ")";
    p << " -> ";
    if (op.getNumResults() > 1) p << "(";
    interleaveComma(op.getResultTypes(), p);
    if (op.getNumResults() > 1) p << ")";
  }
  p << "\n";

  p << "      invocation(";
  auto &entryBlock = op.body().getBlocks().front();
  int regionArgIndex = 0;
  interleaveComma(op.initial_values(), p, [&](Value operand) {
    p << "(";
    p.printOperand(entryBlock.getArgument(regionArgIndex++));
    p << ", ";
    p.printOperand(entryBlock.getArgument(regionArgIndex++));
    p << ") = ";
    p.printOperand(operand);
    p << " : ";
    p.printType(operand->getType());
  });
  p << ") ";

  p.printRegion(op.body(), /*printEntryBlockArgs=*/false);
  p.printOptionalAttrDict(op.getAttrs(),
                          /*elidedAttrs=*/{});
}

//===----------------------------------------------------------------------===//
// flow.windowed_reduction.region
//===----------------------------------------------------------------------===//

void WindowedReductionRegionOp::build(
    Builder *builder, OperationState &state, ArrayRef<Type> resultTypes,
    Value workload, ValueRange operands, ValueRange initialValues,
    ArrayRef<int32_t> windowDimensions, ArrayRef<int32_t> windowStrides,
    ArrayRef<int32_t> baseDilations, ArrayRef<int32_t> windowDilations,
    PaddingMode paddingMode, ArrayRef<NamedAttribute> attributes) {
  state.addTypes(resultTypes);
  state.addOperands({workload});
  state.addOperands(operands);
  state.addOperands(initialValues);
  state.addAttribute(
      "window_dimensions",
      DenseIntElementsAttr::get(
          VectorType::get({static_cast<int32_t>(windowDimensions.size())},
                          builder->getIntegerType(32)),
          windowDimensions));
  state.addAttribute(
      "window_strides",
      DenseIntElementsAttr::get(
          VectorType::get({static_cast<int32_t>(windowStrides.size())},
                          builder->getIntegerType(32)),
          windowStrides));
  state.addAttribute(
      "base_dilations",
      DenseIntElementsAttr::get(
          VectorType::get({static_cast<int32_t>(baseDilations.size())},
                          builder->getIntegerType(32)),
          baseDilations));
  state.addAttribute(
      "window_dilations",
      DenseIntElementsAttr::get(
          VectorType::get({static_cast<int32_t>(windowDilations.size())},
                          builder->getIntegerType(32)),
          windowDilations));
  state.addAttribute("padding_mode", builder->getI32IntegerAttr(
                                         static_cast<int32_t>(paddingMode)));
  state.addAttributes(attributes);
  state.addRegion();
  state.setOperandListToResizable();
}

ParseResult parseWindowedReductionRegionOp(OpAsmParser &parser,
                                           OperationState *result) {
  return parseReductionRegionOp(parser, result);
}

void printWindowedReductionRegionOp(OpAsmPrinter &p,
                                    WindowedReductionRegionOp op) {
  p << op.getOperationName();

  // Print the workload argument.
  p << "[";
  p.printOperand(op.workload());
  p << " : ";
  p.printType(op.workload()->getType());
  p << "]";

  p << "(";
  p.printOperands(op.operands());
  p << ")";
  if (op.getNumResults() > 0) {
    p << " : (";
    interleaveComma(op.operands(), p,
                    [&](Value operand) { p.printType(operand->getType()); });
    p << ")";
    p << " -> (";
    interleaveComma(op.getResultTypes(), p);
    p << ")";
  }
  p << "\n";

  p << "      invocation(";
  auto &entryBlock = op.body().getBlocks().front();
  int regionArgIndex = 0;
  interleaveComma(op.initial_values(), p, [&](Value operand) {
    p << "(";
    p.printOperand(entryBlock.getArgument(regionArgIndex++));
    p << ", ";
    p.printOperand(entryBlock.getArgument(regionArgIndex++));
    p << ") = ";
    p.printOperand(operand);
    p << " : ";
    p.printType(operand->getType());
  });
  p << ") ";

  p.printRegion(op.body(), /*printEntryBlockArgs=*/false);
  p.printOptionalAttrDict(op.getAttrs(),
                          /*elidedAttrs=*/{});
}

//===----------------------------------------------------------------------===//
// flow.return
//===----------------------------------------------------------------------===//

static ParseResult parseReturnOp(OpAsmParser &parser, OperationState *result) {
  SmallVector<OpAsmParser::OperandType, 2> opInfo;
  SmallVector<Type, 2> types;
  llvm::SMLoc loc = parser.getCurrentLocation();
  return failure(parser.parseOperandList(opInfo) ||
                 (!opInfo.empty() && parser.parseColonTypeList(types)) ||
                 parser.resolveOperands(opInfo, types, loc, result->operands));
}

static void printReturnOp(OpAsmPrinter &p, ReturnOp op) {
  p << op.getOperationName();
  if (op.getNumOperands() > 0) {
    p << ' ';
    p.printOperands(op.operand_begin(), op.operand_end());
    p << " : ";
    interleaveComma(op.getOperandTypes(), p);
  }
}

//===----------------------------------------------------------------------===//
// flow.executable
//===----------------------------------------------------------------------===//

void ExecutableOp::build(Builder *builder, OperationState &state,
                         StringRef name) {
  ensureTerminator(*state.addRegion(), *builder, state.location);
  state.addAttribute(mlir::SymbolTable::getSymbolAttrName(),
                     builder->getStringAttr(name));
}

static ParseResult parseExecutableOp(OpAsmParser &parser,
                                     OperationState *result) {
  StringAttr nameAttr;
  if (failed(parser.parseSymbolName(nameAttr,
                                    mlir::SymbolTable::getSymbolAttrName(),
                                    result->attributes)) ||
      failed(parser.parseOptionalAttrDictWithKeyword(result->attributes))) {
    return failure();
  }

  // Parse the module body.
  auto *body = result->addRegion();
  if (failed(parser.parseRegion(*body, llvm::None, llvm::None))) {
    return failure();
  }

  // Ensure that this module has a valid terminator.
  ExecutableOp::ensureTerminator(*body, parser.getBuilder(), result->location);
  return success();
}

static void printExecutableOp(OpAsmPrinter &p, ExecutableOp op) {
  p << op.getOperationName() << ' ';
  p.printSymbolName(op.sym_name());
  p.printOptionalAttrDictWithKeyword(
      op.getAttrs(),
      /*elidedAttrs=*/{mlir::SymbolTable::getSymbolAttrName()});
  p.printRegion(op.body(), /*printEntryBlockArgs=*/false,
                /*printBlockTerminators=*/false);
}

static LogicalResult verifyExecutableOp(ExecutableOp op) {
  // TODO(benvanik): check export name conflicts.
  return success();
}

static ParseResult parseRegionEndOp(OpAsmParser &parser,
                                    OperationState *result) {
  return parser.parseOptionalAttrDict(result->attributes);
}

static void printRegionEndOp(OpAsmPrinter &p, Operation *op) {
  p << op->getName();
  p.printOptionalAttrDict(op->getAttrs());
}

//===----------------------------------------------------------------------===//
// flow.dispatch.entry
//===----------------------------------------------------------------------===//

static ParseResult parseDispatchEntryOp(OpAsmParser &parser,
                                        OperationState *result) {
  FlatSymbolRefAttr functionRefAttr;
  if (failed(parser.parseAttribute(functionRefAttr, "function_ref",
                                   result->attributes))) {
    return failure();
  }

  if (succeeded(parser.parseOptionalKeyword("as"))) {
    StringAttr exportNameAttr;
    if (failed(parser.parseLParen()) ||
        failed(parser.parseAttribute(exportNameAttr, "sym_name",
                                     result->attributes)) ||
        failed(parser.parseRParen())) {
      return failure();
    }
  } else {
    result->addAttribute("sym_name", parser.getBuilder().getStringAttr(
                                         functionRefAttr.getValue()));
  }

  if (failed(parser.parseOptionalAttrDictWithKeyword(result->attributes))) {
    return failure();
  }

  return success();
}

static void printDispatchEntryOp(OpAsmPrinter &p, DispatchEntryOp op) {
  p << op.getOperationName() << ' ';
  p.printSymbolName(op.function_ref());
  if (op.sym_name() != op.function_ref()) {
    p << " as(\"" << op.sym_name() << "\")";
  }
  p.printOptionalAttrDictWithKeyword(
      op.getAttrs(), /*elidedAttrs=*/{"function_ref", "sym_name"});
}

//===----------------------------------------------------------------------===//
// flow.reduction.entry / flow.windowed_reduction.entry
//===----------------------------------------------------------------------===//

static ParseResult parseReductionEntryOp(OpAsmParser &parser,
                                         OperationState *result) {
  FlatSymbolRefAttr functionRefAttr;
  FlatSymbolRefAttr applyRefAttr;
  if (failed(parser.parseAttribute(functionRefAttr, "function_ref",
                                   result->attributes)) ||
      failed(parser.parseKeyword("apply")) || failed(parser.parseLParen()) ||
      failed(parser.parseAttribute(applyRefAttr, "apply_ref",
                                   result->attributes)) ||
      failed(parser.parseRParen())) {
    return failure();
  }

  if (succeeded(parser.parseOptionalKeyword("as"))) {
    StringAttr exportNameAttr;
    if (failed(parser.parseLParen()) ||
        failed(parser.parseAttribute(exportNameAttr, "sym_name",
                                     result->attributes)) ||
        failed(parser.parseRParen())) {
      return failure();
    }
  } else {
    result->addAttribute("sym_name", parser.getBuilder().getStringAttr(
                                         functionRefAttr.getValue()));
  }

  if (failed(parser.parseOptionalAttrDictWithKeyword(result->attributes))) {
    return failure();
  }

  return success();
}

static void printReductionEntryOp(OpAsmPrinter &p, ReductionEntryOp op) {
  p << op.getOperationName() << ' ';
  p.printSymbolName(op.function_ref());
  p << " apply(";
  p.printSymbolName(op.apply_ref());
  p << ")";
  if (op.sym_name() != op.function_ref()) {
    p << " as(\"" << op.sym_name() << "\")";
  }
  p.printOptionalAttrDictWithKeyword(
      op.getAttrs(), /*elidedAttrs=*/{"apply_ref", "function_ref", "sym_name"});
}

static ParseResult parseWindowedReductionEntryOp(OpAsmParser &parser,
                                                 OperationState *result) {
  return parseReductionEntryOp(parser, result);
}

static void printWindowedReductionEntryOp(OpAsmPrinter &p,
                                          WindowedReductionEntryOp op) {
  p << op.getOperationName() << ' ';
  p.printSymbolName(op.function_ref());
  p << " apply(";
  p.printSymbolName(op.apply_ref());
  p << ")";
  if (op.sym_name() != op.function_ref()) {
    p << " as(\"" << op.sym_name() << "\")";
  }
  p.printOptionalAttrDictWithKeyword(
      op.getAttrs(), /*elidedAttrs=*/{"apply_ref", "function_ref", "sym_name"});
}

//===----------------------------------------------------------------------===//
// flow.dispatch
//===----------------------------------------------------------------------===//

static ParseResult parseDispatchOp(OpAsmParser &parser,
                                   OperationState *result) {
  auto executableLoc = parser.getNameLoc();

  // TODO(benvanik): replace with SymbolRefAttr.
  StringAttr executableAttr;
  StringAttr entryPointAttr;
  if (failed(parser.parseSymbolName(executableAttr, "executable",
                                    result->attributes)) ||
      failed(parser.parseColon()) || failed(parser.parseColon()) ||
      failed(parser.parseSymbolName(entryPointAttr, "entry_point",
                                    result->attributes))) {
    return failure();
  }
  result->attributes[0].second =
      parser.getBuilder().getSymbolRefAttr(executableAttr.getValue());
  result->attributes[1].second =
      parser.getBuilder().getSymbolRefAttr(entryPointAttr.getValue());

  OpAsmParser::OperandType workloadArg;
  Type workloadArgType;
  if (failed(parser.parseLSquare()) ||
      failed(parser.parseOperand(workloadArg)) ||
      failed(parser.parseColonType(workloadArgType)) ||
      failed(parser.parseRSquare()) ||
      failed(parser.resolveOperand(workloadArg, workloadArgType,
                                   result->operands))) {
    return failure();
  }

  SmallVector<OpAsmParser::OperandType, 4> operands;
  FunctionType entryPointType;
  if (failed(
          parser.parseOperandList(operands, OpAsmParser::Delimiter::Paren)) ||
      failed(parser.parseOptionalAttrDict(result->attributes)) ||
      failed(parser.parseColonType(entryPointType)) ||
      failed(
          parser.addTypesToList(entryPointType.getResults(), result->types)) ||
      failed(parser.resolveOperands(operands, entryPointType.getInputs(),
                                    executableLoc, result->operands))) {
    return failure();
  }
  return success();
}

static void printDispatchOp(OpAsmPrinter &p, DispatchOp op) {
  p << op.getOperationName() << ' ';
  // TODO(benvanik): replace with SymbolRefAttr.
  p.printSymbolName(op.executable());
  p << "::";
  p.printSymbolName(op.entry_point());
  p << "[";
  p.printOperand(op.workload());
  p << " : ";
  p.printType(op.workload()->getType());
  p << "](";
  p.printOperands(op.operands());
  p << ')';
  p.printOptionalAttrDict(op.getAttrs(), /*elidedAttrs=*/{
                              "executable",
                              "entry_point",
                          });
  p << " : ";
  p.printType(op.getEntryPointType());
}

FunctionType DispatchOp::getEntryPointType() {
  SmallVector<Type, 4> resultTypes(getResultTypes());
  SmallVector<Type, 8> argTypes(operand_type_range{operands()});
  return FunctionType::get(argTypes, resultTypes, getContext());
}

//===----------------------------------------------------------------------===//
// flow.tensor.reshape
//===----------------------------------------------------------------------===//

static ParseResult parseTensorReshapeOp(OpAsmParser &parser,
                                        OperationState *result) {
  OpAsmParser::OperandType sourceOperand;
  ShapedType sourceType;
  ShapedType resultType;
  if (failed(parser.parseOperand(sourceOperand)) ||
      failed(parser.parseColonType(sourceType)) ||
      failed(parser.parseArrow()) || failed(parser.parseType(resultType)) ||
      failed(parser.parseOptionalAttrDictWithKeyword(result->attributes))) {
    return failure();
  }
  if (failed(
          parser.resolveOperand(sourceOperand, sourceType, result->operands))) {
    return failure();
  }
  result->addTypes({resultType});
  return success();
}

static void printTensorReshapeOp(OpAsmPrinter &p, TensorReshapeOp &op) {
  p << op.getOperationName() << ' ';
  p.printOperand(op.source());
  p << " : ";
  p.printType(op.source()->getType());
  p << " -> ";
  p.printType(op.result()->getType());
  p.printOptionalAttrDictWithKeyword(op.getAttrs());
}

//===----------------------------------------------------------------------===//
// flow.tensor.load
//===----------------------------------------------------------------------===//

static ParseResult parseTensorLoadOp(OpAsmParser &parser,
                                     OperationState *result) {
  OpAsmParser::OperandType sourceOperand;
  SmallVector<OpAsmParser::OperandType, 4> indexOperands;
  ShapedType sourceType;
  if (failed(parser.parseOperand(sourceOperand)) ||
      failed(parser.parseOperandList(indexOperands,
                                     OpAsmParser::Delimiter::OptionalSquare)) ||
      failed(parser.parseColonType(sourceType)) ||
      failed(parser.parseOptionalAttrDictWithKeyword(result->attributes)) ||
      failed(
          parser.resolveOperand(sourceOperand, sourceType, result->operands)) ||
      failed(parser.resolveOperands(indexOperands,
                                    parser.getBuilder().getIntegerType(32),
                                    result->operands))) {
    return failure();
  }
  result->addTypes({sourceType.getElementType()});
  return success();
}

static void printTensorLoadOp(OpAsmPrinter &p, TensorLoadOp &op) {
  p << op.getOperationName() << ' ';
  p.printOperand(op.source());
  if (!op.indices().empty()) {
    p << '[';
    p.printOperands(op.indices());
    p << ']';
  }
  p << " : ";
  p.printType(op.source()->getType());
  p.printOptionalAttrDictWithKeyword(op.getAttrs());
}

//===----------------------------------------------------------------------===//
// flow.tensor.store
//===----------------------------------------------------------------------===//

static ParseResult parseTensorStoreOp(OpAsmParser &parser,
                                      OperationState *result) {
  OpAsmParser::OperandType valueOperand;
  OpAsmParser::OperandType targetOperand;
  SmallVector<OpAsmParser::OperandType, 4> indexOperands;
  ShapedType targetType;
  if (failed(parser.parseOperand(valueOperand)) ||
      failed(parser.parseComma()) ||
      failed(parser.parseOperand(targetOperand)) ||
      failed(parser.parseOperandList(indexOperands,
                                     OpAsmParser::Delimiter::OptionalSquare)) ||
      failed(parser.parseColonType(targetType)) ||
      failed(parser.parseOptionalAttrDictWithKeyword(result->attributes)) ||
      failed(parser.resolveOperand(valueOperand, targetType.getElementType(),
                                   result->operands)) ||
      failed(
          parser.resolveOperand(targetOperand, targetType, result->operands)) ||
      failed(parser.resolveOperands(indexOperands,
                                    parser.getBuilder().getIntegerType(32),
                                    result->operands))) {
    return failure();
  }
  result->addTypes({targetType});
  return success();
}

static void printTensorStoreOp(OpAsmPrinter &p, TensorStoreOp &op) {
  p << op.getOperationName() << ' ';
  p.printOperand(op.value());
  p << ", ";
  p.printOperand(op.target());
  if (!op.indices().empty()) {
    p << '[';
    p.printOperands(op.indices());
    p << ']';
  }
  p << " : ";
  p.printType(op.target()->getType());
  p.printOptionalAttrDictWithKeyword(op.getAttrs());
}

//===----------------------------------------------------------------------===//
// flow.tensor.splat
//===----------------------------------------------------------------------===//

static ParseResult parseTensorSplatOp(OpAsmParser &parser,
                                      OperationState *result) {
  OpAsmParser::OperandType valueOperand;
  ShapedType targetType;
  if (failed(parser.parseOperand(valueOperand)) ||
      failed(parser.parseColonType(targetType)) ||
      failed(parser.parseOptionalAttrDictWithKeyword(result->attributes)) ||
      failed(parser.resolveOperand(valueOperand, targetType.getElementType(),
                                   result->operands))) {
    return failure();
  }
  result->addTypes({targetType});
  return success();
}

static void printTensorSplatOp(OpAsmPrinter &p, TensorSplatOp &op) {
  p << op.getOperationName() << ' ';
  p.printOperand(op.value());
  p << " : ";
  p.printType(op.result()->getType());
  p.printOptionalAttrDictWithKeyword(op.getAttrs());
}

//===----------------------------------------------------------------------===//
// flow.tensor.clone
//===----------------------------------------------------------------------===//

static ParseResult parseTensorCloneOp(OpAsmParser &parser,
                                      OperationState *result) {
  OpAsmParser::OperandType operand;
  ShapedType type;
  if (failed(parser.parseOperand(operand)) ||
      failed(parser.parseColonType(type)) ||
      failed(parser.parseOptionalAttrDictWithKeyword(result->attributes)) ||
      failed(parser.resolveOperand(operand, type, result->operands))) {
    return failure();
  }
  result->addTypes({type});
  return success();
}

static void printTensorCloneOp(OpAsmPrinter &p, TensorCloneOp &op) {
  p << op.getOperationName() << ' ';
  p.printOperand(op.operand());
  p << " : ";
  p.printType(op.result()->getType());
  p.printOptionalAttrDictWithKeyword(op.getAttrs());
}

//===----------------------------------------------------------------------===//
// flow.tensor.slice
//===----------------------------------------------------------------------===//

static ParseResult parseTensorSliceOp(OpAsmParser &parser,
                                      OperationState *result) {
  OpAsmParser::OperandType sourceOperand;
  SmallVector<OpAsmParser::OperandType, 4> indexOperands;
  SmallVector<OpAsmParser::OperandType, 4> lengthOperands;
  ShapedType sourceType;
  ShapedType resultType;
  if (failed(parser.parseOperand(sourceOperand)) ||
      failed(parser.parseLSquare()) ||
      failed(parser.parseOperandList(indexOperands,
                                     OpAsmParser::Delimiter::None)) ||
      failed(parser.parseKeyword("for")) ||
      failed(parser.parseOperandList(lengthOperands,
                                     OpAsmParser::Delimiter::None)) ||
      failed(parser.parseRSquare()) ||
      failed(parser.parseColonType(sourceType)) ||
      failed(parser.parseArrow()) || failed(parser.parseType(resultType)) ||
      failed(parser.parseOptionalAttrDictWithKeyword(result->attributes)) ||
      failed(
          parser.resolveOperand(sourceOperand, sourceType, result->operands)) ||
      failed(parser.resolveOperands(indexOperands,
                                    parser.getBuilder().getIntegerType(32),
                                    result->operands)) ||
      failed(parser.resolveOperands(lengthOperands,
                                    parser.getBuilder().getIntegerType(32),
                                    result->operands))) {
    return failure();
  }
  result->addTypes({resultType});
  return success();
}

static void printTensorSliceOp(OpAsmPrinter &p, TensorSliceOp &op) {
  p << op.getOperationName() << ' ';
  p.printOperand(op.source());
  p << '[';
  p.printOperands(op.start_indices());
  p << " for ";
  p.printOperands(op.lengths());
  p << "] : ";
  p.printType(op.source()->getType());
  p << " -> ";
  p.printType(op.result()->getType());
  p.printOptionalAttrDictWithKeyword(op.getAttrs());
}

//===----------------------------------------------------------------------===//
// flow.tensor.update
//===----------------------------------------------------------------------===//

static ParseResult parseTensorUpdateOp(OpAsmParser &parser,
                                       OperationState *result) {
  OpAsmParser::OperandType updateOperand;
  OpAsmParser::OperandType targetOperand;
  SmallVector<OpAsmParser::OperandType, 4> indexOperands;
  ShapedType updateType;
  ShapedType targetType;
  if (failed(parser.parseOperand(updateOperand)) ||
      failed(parser.parseComma()) ||
      failed(parser.parseOperand(targetOperand)) ||
      failed(parser.parseOperandList(indexOperands,
                                     OpAsmParser::Delimiter::Square)) ||
      failed(parser.parseColonType(updateType)) ||
      failed(parser.parseArrow()) || failed(parser.parseType(targetType)) ||
      failed(parser.parseOptionalAttrDictWithKeyword(result->attributes)) ||
      failed(
          parser.resolveOperand(updateOperand, updateType, result->operands)) ||
      failed(
          parser.resolveOperand(targetOperand, targetType, result->operands)) ||
      failed(parser.resolveOperands(indexOperands,
                                    parser.getBuilder().getIntegerType(32),
                                    result->operands))) {
    return failure();
  }
  result->addTypes({targetType});
  return success();
}

static void printTensorUpdateOp(OpAsmPrinter &p, TensorUpdateOp &op) {
  p << op.getOperationName() << ' ';
  p.printOperand(op.update());
  p << ", ";
  p.printOperand(op.target());
  p << '[';
  p.printOperands(op.start_indices());
  p << "] : ";
  p.printType(op.update()->getType());
  p << " -> ";
  p.printType(op.result()->getType());
  p.printOptionalAttrDictWithKeyword(op.getAttrs());
}

//===----------------------------------------------------------------------===//
// flow.ex.stream.fragment
//===----------------------------------------------------------------------===//

void ExStreamFragmentOp::build(Builder *builder, OperationState &state,
                               ArrayRef<Type> resultTypes, ValueRange operands,
                               ArrayRef<NamedAttribute> attributes) {
  state.addTypes(resultTypes);
  state.addOperands(operands);
  state.addAttributes(attributes);
  state.addRegion();
  state.setOperandListToResizable();
}

ParseResult parseExStreamFragmentOp(OpAsmParser &parser,
                                    OperationState *result) {
  SmallVector<OpAsmParser::OperandType, 16> regionArgs;
  SmallVector<Type, 16> regionArgTypes;
  if (failed(parser.parseLParen())) {
    return failure();
  }
  if (failed(parser.parseOptionalRParen())) {
    SmallVector<OpAsmParser::OperandType, 16> regionOperands;
    auto argsLoc = parser.getCurrentLocation();
    do {
      // Reserve entries in the lists.
      regionArgs.emplace_back();
      regionOperands.emplace_back();
      regionArgTypes.emplace_back();
      if (failed(parser.parseRegionArgument(regionArgs.back())) ||
          failed(parser.parseEqual()) ||
          failed(parser.parseOperand(regionOperands.back())) ||
          failed(parser.parseColonType(regionArgTypes.back()))) {
        return failure();
      }
    } while (succeeded(parser.parseOptionalComma()));
    if (failed(parser.parseRParen()) ||
        failed(parser.resolveOperands(regionOperands, regionArgTypes, argsLoc,
                                      result->operands))) {
      return failure();
    }
  }
  result->setOperandListToResizable();

  // Parse (optional) results.
  if (failed(parser.parseOptionalArrowTypeList(result->types))) {
    return failure();
  }

  // Parse region body.
  Region *body = result->addRegion();
  if (failed(parser.parseRegion(*body, regionArgs, regionArgTypes)) ||
      failed(parser.parseOptionalAttrDict(result->attributes))) {
    return failure();
  }
  return success();
}

void printExStreamFragmentOp(OpAsmPrinter &p, ExStreamFragmentOp op) {
  p << op.getOperationName();

  // Print the data argument remapping.
  p << "(";
  interleaveComma(llvm::zip(op.body().front().getArguments(), op.args()), p,
                  [&](std::tuple<BlockArgument, Value> it) {
                    p << *std::get<0>(it) << " = " << *std::get<1>(it);
                    p << " : ";
                    p << std::get<1>(it)->getType();
                  });
  p << ")";

  // Print the result types, if any.
  if (op.getNumResults() > 0) {
    p << " -> ";
    if (op.getNumResults() > 1) p << "(";
    interleaveComma(op.getResultTypes(), p);
    if (op.getNumResults() > 1) p << ")";
  }

  p.printRegion(op.body(), /*printEntryBlockArgs=*/false);
  p.printOptionalAttrDict(op.getAttrs(),
                          /*elidedAttrs=*/{});
}

//===----------------------------------------------------------------------===//
// TableGen definitions (intentionally last)
//===----------------------------------------------------------------------===//

#define GET_OP_CLASSES
#include "iree/compiler/Dialect/Flow/IR/FlowOps.cpp.inc"

}  // namespace Flow
}  // namespace IREE
}  // namespace iree_compiler
}  // namespace mlir
