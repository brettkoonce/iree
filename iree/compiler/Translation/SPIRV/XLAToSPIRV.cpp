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

//===- XLAToSPIRV.cpp ------------------------------------------*- C++//-*-===//
//
// Implementation of SPIR-V Code-generation for xla_hlo operations within IREE
// Dispatch functions
//
//===----------------------------------------------------------------------===//
#include "iree/compiler/Translation/SPIRV/XLAToSPIRV.h"

#include "mlir/Dialect/SPIRV/SPIRVOps.h"
#include "mlir/IR/StandardTypes.h"

namespace mlir {
namespace iree_compiler {

//===----------------------------------------------------------------------===//
// ConcatenateOp
//===----------------------------------------------------------------------===//

LogicalResult XLAConcatenateOpSPIRVLowering::lowerOperation(
    Operation *op, OpBuilder &builder, AffineMap index,
    ArrayRef<Value> operands, TensorIndexToScalarValueMap &valueCache) const {
  auto concatenateOp = cast<xla_hlo::ConcatenateOp>(op);
  auto loc = concatenateOp.getLoc();
  auto i32Type = builder.getIntegerType(32);
  auto i1Type = builder.getI1Type();
  int append_dim = concatenateOp.dimension().getZExtValue();
  auto dimIndex = valueCache.getAffineExprValue(
      builder.saveInsertionPoint(), loc, index.getResult(append_dim));

  int offset = op->getOperand(0)
                   ->getType()
                   .cast<RankedTensorType>()
                   .getShape()[append_dim];
  Value resultVal = operands[0];
  for (auto operandIt : llvm::enumerate(op->getOperands())) {
    // The first operand is already saved in resultVal.
    if (operandIt.index() == 0) continue;

    // Only select values that offset <= d < offset + operand_shape[append_dim].
    // Since later values will be replaced in the later iterations, only check
    // d >= offset here.
    Value cond = spirv::ConstantOp::getOne(i1Type, loc, &builder);
    auto offsetVar = builder.create<spirv::ConstantOp>(
        loc, i32Type, builder.getI32IntegerAttr(offset));
    auto checkLb = builder.create<spirv::SGreaterThanEqualOp>(
        loc, i1Type, dimIndex, offsetVar);
    cond = builder.create<spirv::LogicalAndOp>(loc, i1Type, cond, checkLb);
    resultVal = builder.create<spirv::SelectOp>(
        loc, cond, operands[operandIt.index()], resultVal);
    auto operandShape =
        operandIt.value()->getType().cast<RankedTensorType>().getShape();
    offset += operandShape[append_dim];
  }
  valueCache.setValueAtIndex(op->getResult(0), index, resultVal);
  return success();
}

//===----------------------------------------------------------------------===//
// ConvertOp
//===----------------------------------------------------------------------===//

LogicalResult XLAConvertOpSPIRVLowering::lowerOperation(
    Operation *op, OpBuilder &builder, AffineMap index,
    ArrayRef<Value> operands, TensorIndexToScalarValueMap &valueCache) const {
  auto convertOp = cast<xla_hlo::ConvertOp>(op);
  auto loc = convertOp.getLoc();
  auto resultElemType =
      convertOp.getResult()->getType().dyn_cast<ShapedType>().getElementType();
  auto operandElemType =
      convertOp.getOperand()->getType().dyn_cast<ShapedType>().getElementType();

  if (resultElemType == operandElemType) {
    valueCache.setValueAtIndex(op->getResult(0), index, operands[0]);
  } else {
    // TODO(hanchung): Use template lambda after migrating to C++20.
    auto buildOp = [&](auto *type_ptr) {
      return builder
          .create<std::remove_pointer_t<decltype(type_ptr)>>(
              loc, resultElemType, operands, ArrayRef<NamedAttribute>())
          .getOperation();
    };
    Operation *scalarOp = nullptr;
    if (resultElemType.isa<IntegerType>()) {
      if (auto intOperandType = operandElemType.dyn_cast<IntegerType>()) {
        // spv.SConvertOp does not support converting a bool to integer, use
        // spv.SelectOp instead.
        if (intOperandType.getWidth() == 1) {
          Value zero =
              spirv::ConstantOp::getZero(resultElemType, loc, &builder);
          Value one = spirv::ConstantOp::getOne(resultElemType, loc, &builder);
          scalarOp =
              builder.create<spirv::SelectOp>(loc, operands[0], one, zero)
                  .getOperation();
        } else {
          scalarOp = buildOp(static_cast<spirv::SConvertOp *>(nullptr));
        }
      } else {
        scalarOp = buildOp(static_cast<spirv::ConvertFToSOp *>(nullptr));
      }
    } else {
      if (operandElemType.isa<FloatType>()) {
        scalarOp = buildOp(static_cast<spirv::FConvertOp *>(nullptr));
      } else {
        scalarOp = buildOp(static_cast<spirv::ConvertSToFOp *>(nullptr));
      }
    }
    valueCache.setValueAtIndex(op->getResult(0), index, scalarOp->getResult(0));
  }
  return success();
}

//===----------------------------------------------------------------------===//
// GatherOp
//===----------------------------------------------------------------------===//
LogicalResult XLAGatherOpSPIRVLowering::lowerOperation(
    Operation *op, OpBuilder &builder, AffineMap index,
    ArrayRef<Value> operands, TensorIndexToScalarValueMap &valueCache) const {
  valueCache.setValueAtIndex(op->getResult(0), index, operands[0]);
  return success();
}

//===----------------------------------------------------------------------===//
// PadOp
//===----------------------------------------------------------------------===//

LogicalResult XLAPadOpSPIRVLowering::lowerOperation(
    Operation *op, OpBuilder &builder, AffineMap index,
    ArrayRef<Value> operands, TensorIndexToScalarValueMap &valueCache) const {
  auto padOp = cast<xla_hlo::PadOp>(op);
  const auto &edgePaddingLow = padOp.edge_padding_low();
  const auto &interiorPadding = padOp.interior_padding();
  const auto &edgePaddingHigh = padOp.edge_padding_high();
  // If the `index` of the result at a particular dimension i, is d_i, check if
  //
  // (d_i >= edge_padding_low[i]) &&
  // ((d_i - edge_padding_low[i]) % (interior_padding[i]+1) == 0) &&
  // (d_i < (edge_padding_low[i] + (interior_padding[i]+1) * operand_shape[i])).
  //
  // If true, then use the value of the operand, or use the
  // padding value.
  auto loc = padOp.getLoc();
  auto i32Type = builder.getIntegerType(32);
  auto i1Type = builder.getI1Type();
  auto zero = spirv::ConstantOp::getZero(i32Type, loc, &builder);
  Value cond = spirv::ConstantOp::getOne(i1Type, loc, &builder);
  auto operandType = padOp.operand()->getType().cast<RankedTensorType>();
  if (!operandType.hasStaticShape()) {
    return padOp.emitError("pad op codegen supported only for static shapes");
  }
  auto operandShape = operandType.getShape();
  for (auto resultIndex : enumerate(index.getResults())) {
    auto i = resultIndex.index();

    // (edge_padding_low[i] + (interior_padding[i]+1) * operand_shape[i])
    int64_t paddingLow = edgePaddingLow.getValue<IntegerAttr>(i).getInt();
    int64_t paddingStride =
        interiorPadding.getValue<IntegerAttr>(i).getInt() + 1;
    int64_t paddingHigh = edgePaddingHigh.getValue<IntegerAttr>(i).getInt();
    if (paddingLow == 0 && paddingStride == 1 && paddingHigh == 0) {
      continue;
    }
    auto edgePadding = builder.create<spirv::ConstantOp>(
        loc, i32Type, builder.getI32IntegerAttr(paddingLow));
    auto stride = builder.create<spirv::ConstantOp>(
        loc, i32Type, builder.getI32IntegerAttr(paddingStride));
    auto operandExtent = builder.create<spirv::ConstantOp>(
        loc, i32Type, builder.getI32IntegerAttr(operandShape[i]));
    auto t1 =
        builder.create<spirv::IMulOp>(loc, i32Type, stride, operandExtent);
    auto bound = builder.create<spirv::IAddOp>(loc, i32Type, edgePadding, t1);

    // d_i
    auto dimIndex = valueCache.getAffineExprValue(builder.saveInsertionPoint(),
                                                  loc, resultIndex.value());

    // d_i < (edge_padding_low[i] + stride * operand_shape[i])
    auto checkUb =
        builder.create<spirv::SLessThanOp>(loc, i1Type, dimIndex, bound);
    cond = builder.create<spirv::LogicalAndOp>(loc, i1Type, cond, checkUb);

    if (paddingLow != 0) {
      // d_i >= edge_padding_low[i]
      auto checkLb = builder.create<spirv::SGreaterThanEqualOp>(
          loc, i1Type, dimIndex, edgePadding);
      cond = builder.create<spirv::LogicalAndOp>(loc, i1Type, cond, checkLb);
    }

    if (paddingStride != 1) {
      // ((d_i - edge_padding_low[i]) % (interior_padding[i]+1) == 0)
      auto t1 = builder.create<spirv::ISubOp>(loc, dimIndex->getType(),
                                              dimIndex, edgePadding);
      auto t2 = builder.create<spirv::SModOp>(loc, t1.getResult()->getType(),
                                              t1, stride);
      auto checkStride = builder.create<spirv::IEqualOp>(loc, i1Type, t2, zero);
      cond =
          builder.create<spirv::LogicalAndOp>(loc, i1Type, cond, checkStride);
    }
  }
  Value resultVal =
      builder.create<spirv::SelectOp>(loc, cond, operands[0], operands[1]);
  valueCache.setValueAtIndex(op->getResult(0), index, resultVal);
  return success();
}

}  // namespace iree_compiler
}  // namespace mlir
