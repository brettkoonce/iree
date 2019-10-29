// RUN: iree-run-mlir --target_backends=interpreter-bytecode %s --input_values="2x2x3xf32= 1 2 3 4 5 6 7 8 9 10 11 12" | FileCheck %s
// TODO(b/143512082): Test --target_backends=vulkan-spirv as well.

// CHECK-LABEL: EXEC @reshape_3D_1D
func @reshape_3D_1D(%arg : tensor<2x2x3xf32>) -> tensor<12xf32> {
  %result = "xla_hlo.reshape"(%arg) : (tensor<2x2x3xf32>) -> tensor<12xf32>
  return %result : tensor<12xf32>
}
// CHECK: 12xf32=1 2 3 4 5 6 7 8 9 10 11 12

// CHECK-LABEL: EXEC @reshape_3D_2D
func @reshape_3D_2D(%arg : tensor<2x2x3xf32>) -> tensor<3x4xf32> {
  %result = "xla_hlo.reshape"(%arg) : (tensor<2x2x3xf32>) -> tensor<3x4xf32>
  return %result : tensor<3x4xf32>
}
// CHECK: 3x4xf32=[1 2 3 4][5 6 7 8][9 10 11 12]