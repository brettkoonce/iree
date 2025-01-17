// A simple while loop example.

// RUN: iree-run-mlir2 %s -iree-hal-target-backends=interpreter-bytecode -input-value="f32=[1]" -input-value="f32=[3]" --export-all=false | IreeFileCheck %s --implicit-check-not="[" --implicit-check-not="]"
// RUN: [[ $IREE_VULKAN_DISABLE == 1 ]] || (iree-run-mlir2 %s -iree-hal-target-backends=vulkan-spirv -input-value="f32=[1]" -input-value="f32=[3]" --export-all=false | IreeFileCheck %s --implicit-check-not="[" --implicit-check-not="]")

// CHECK-LABEL: EXEC @main
func @main(%arg0: tensor<f32>, %arg1: tensor<f32>) -> tensor<f32> attributes { iree.module.export }  {
  %0 = "xla_hlo.while"(%arg0) ( {
  ^bb0(%arg2: tensor<f32>):
    %1 = "xla_hlo.compare"(%arg2, %arg1) {comparison_direction = "LT", name = "compare.2"} : (tensor<f32>, tensor<f32>) -> tensor<i1>
    "xla_hlo.return"(%1) : (tensor<i1>) -> ()
  },  {
  ^bb0(%arg2: tensor<f32>):
    %1 = xla_hlo.add %arg2, %arg2 {name = "compare.0"} : tensor<f32>
    "xla_hlo.return"(%1) : (tensor<f32>) -> ()
  }) : (tensor<f32>) -> tensor<f32>

  return %0 : tensor<f32>
}

// CHECK: f32=4
