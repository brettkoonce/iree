// RUN: iree-opt -split-input-file -iree-index-computation -simplify-spirv-affine-exprs=false -convert-iree-to-spirv -verify-diagnostics -o - %s | IreeFileCheck %s

module {
  // CHECK:spv.module "Logical" "GLSL450"
  // CHECK-DAG: spv.globalVariable [[GLOBALIDVAR:@.*]] built_in("GlobalInvocationId") : !spv.ptr<vector<3xi32>, Input>
  // CHECK: func [[FN:@broadcast_in_dim_2D_3D]]
  // CHECK-SAME: [[ARG0:%.*]]: !spv.ptr<!spv.struct<!spv.array<12 x !spv.array<42 x i32 [4]> [168]> [0]>, StorageBuffer>
  // CHECK-SAME: [[ARG1:%.*]]: !spv.ptr<!spv.struct<!spv.array<3 x !spv.array<12 x !spv.array<42 x i32 [4]> [168]> [2016]> [0]>, StorageBuffer>
  func @broadcast_in_dim_2D_3D(%arg0: memref<12x42xi32>, %arg1: memref<3x12x42xi32>)
  attributes  {iree.executable.export, iree.executable.workload = dense<[42, 12, 3]> : tensor<3xi32>, iree.executable.workgroup_size = dense<[32, 1, 1]> : tensor<3xi32>, iree.ordinal = 0 : i32} {
    // CHECK: [[GLOBALIDPTR:%.*]] = spv._address_of [[GLOBALIDVAR]]
    // CHECK: [[GLOBALID:%.*]] = spv.Load "Input" [[GLOBALIDPTR]]
    // CHECK: [[GLOBALIDX:%.*]] = spv.CompositeExtract [[GLOBALID]][0 : i32]
    // CHECK: [[GLOBALIDY:%.*]] = spv.CompositeExtract [[GLOBALID]][1 : i32]
    // CHECK: [[GLOBALIDZ:%.*]] = spv.CompositeExtract [[GLOBALID]][2 : i32]
    // CHECK: spv.selection
    // CHECK: [[ZERO1:%.*]] = spv.constant 0 : i32
    // CHECK: [[ARG0LOADPTR:%.*]] = spv.AccessChain [[ARG0]]{{\[}}[[ZERO1]], [[GLOBALIDY]], [[GLOBALIDX]]{{\]}}
    // CHECK: [[VAL:%.*]] = spv.Load "StorageBuffer" [[ARG0LOADPTR]]
    %0 = iree.load_input(%arg0 : memref<12x42xi32>) : tensor<12x42xi32>
    %1 = "xla_hlo.broadcast_in_dim"(%0) {broadcast_dimensions = dense<[1, 2]> : tensor<2xi64>} : (tensor<12x42xi32>) -> tensor<3x12x42xi32>
    // CHECK: [[ZERO2:%.*]] = spv.constant 0 : i32
    // CHECK: [[ARG1STOREPTR:%.*]] = spv.AccessChain [[ARG1]]{{\[}}[[ZERO2]], [[GLOBALIDZ]], [[GLOBALIDY]], [[GLOBALIDX]]{{\]}}
    // CHECK: spv.Store "StorageBuffer" [[ARG1STOREPTR]], [[VAL]]
    iree.store_output(%1 : tensor<3x12x42xi32>, %arg1 : memref<3x12x42xi32>)
    iree.return
  }
}

// -----

module {
  // CHECK:spv.module "Logical" "GLSL450"
  // CHECK-DAG: spv.globalVariable [[GLOBALIDVAR:@.*]] built_in("GlobalInvocationId") : !spv.ptr<vector<3xi32>, Input>
  // CHECK: func [[FN:@broadcast_in_dim_scalar_3D]]
  // CHECK-SAME: [[ARG0:%.*]]: !spv.ptr<!spv.struct<i32 [0]>, StorageBuffer>
  // CHECK-SAME: [[ARG1:%.*]]: !spv.ptr<!spv.struct<!spv.array<3 x !spv.array<12 x !spv.array<42 x i32 [4]> [168]> [2016]> [0]>, StorageBuffer>
  func @broadcast_in_dim_scalar_3D(%arg0: memref<i32>, %arg1: memref<3x12x42xi32>)
  attributes  {iree.executable.export, iree.executable.workload = dense<[42, 12, 3]> : tensor<3xi32>, iree.executable.workgroup_size = dense<[32, 1, 1]> : tensor<3xi32>, iree.ordinal = 0 : i32} {
    // CHECK: [[GLOBALIDPTR:%.*]] = spv._address_of [[GLOBALIDVAR]]
    // CHECK: [[GLOBALID:%.*]] = spv.Load "Input" [[GLOBALIDPTR]]
    // CHECK: [[GLOBALIDX:%.*]] = spv.CompositeExtract [[GLOBALID]][0 : i32]
    // CHECK: [[GLOBALIDY:%.*]] = spv.CompositeExtract [[GLOBALID]][1 : i32]
    // CHECK: [[GLOBALIDZ:%.*]] = spv.CompositeExtract [[GLOBALID]][2 : i32]
    // CHECK: spv.selection
    // CHECK: [[ZERO:%.*]] = spv.constant 0 : i32
    // CHECK: [[ARG0LOADPTR:%.*]] = spv.AccessChain [[ARG0]]{{\[}}[[ZERO]]{{\]}}
    // CHECK: [[VAL:%.*]] = spv.Load "StorageBuffer" [[ARG0LOADPTR]]
    %0 = iree.load_input(%arg0 : memref<i32>) : tensor<i32>
    %1 = "xla_hlo.broadcast_in_dim"(%0) : (tensor<i32>) -> tensor<3x12x42xi32>
    // CHECK: [[ZERO1:%.*]] = spv.constant 0 : i32
    // CHECK: [[ARG1STOREPTR:%.*]] = spv.AccessChain [[ARG1]]{{\[}}[[ZERO1]], [[GLOBALIDZ]], [[GLOBALIDY]], [[GLOBALIDX]]{{\]}}
    // CHECK: spv.Store "StorageBuffer" [[ARG1STOREPTR]], [[VAL]]
    iree.store_output(%1 : tensor<3x12x42xi32>, %arg1 : memref<3x12x42xi32>)
    iree.return
  }
}

// -----

module {
  func @const_float_splat(%arg0: memref<12x42xf32>)
  attributes  {iree.executable.export, iree.executable.workload = dense<[42, 12, 1]> : tensor<3xi32>, iree.executable.workgroup_size = dense<[32, 1, 1]> : tensor<3xi32>, iree.ordinal = 0 : i32} {
    // CHECK: spv.constant 1.000000e+00 : f32
    %0 = constant dense<1.0> : tensor<12xf32>
    %1 = "xla_hlo.broadcast_in_dim"(%0) {broadcast_dimensions = dense<[0]> : tensor<1xi64>} : (tensor<12xf32>) -> tensor<12x42xf32>
    iree.store_output(%1 : tensor<12x42xf32>, %arg0 : memref<12x42xf32>)
    iree.return
  }
}

// -----

module {
  func @const_int_splat(%arg0: memref<12x42xi32>)
  attributes  {iree.executable.export, iree.executable.workload = dense<[42, 12, 1]> : tensor<3xi32>, iree.executable.workgroup_size = dense<[32, 1, 1]> : tensor<3xi32>, iree.ordinal = 0 : i32} {
    // CHECK: spv.constant 42 : i32
    %0 = constant dense<42> : tensor<12xi32>
    %1 = "xla_hlo.broadcast_in_dim"(%0) {broadcast_dimensions = dense<[0]> : tensor<1xi64>} : (tensor<12xi32>) -> tensor<12x42xi32>
    iree.store_output(%1 : tensor<12x42xi32>, %arg0 : memref<12x42xi32>)
    iree.return
  }
}

// -----

module {
  func @const_int_splat_err(%arg0: memref<2x12x42xi32>)
  attributes  {iree.executable.export, iree.executable.workload = dense<[42, 12, 2]> : tensor<3xi32>, iree.executable.workgroup_size = dense<[32, 1, 1]> : tensor<3xi32>, iree.ordinal = 0 : i32} {
    // CHECK: spv.constant dense<[42, 21]> : tensor<2xi32> : !spv.array<2 x i32 [4]>
    %0 = constant dense<[42, 21]> : tensor<2xi32>
    %1 = "xla_hlo.broadcast_in_dim"(%0) {broadcast_dimensions = dense<[0]> : tensor<1xi64>} : (tensor<2xi32>) -> tensor<2x12x42xi32>
    iree.store_output(%1 : tensor<2x12x42xi32>, %arg0 : memref<2x12x42xi32>)
    iree.return
  }
}
