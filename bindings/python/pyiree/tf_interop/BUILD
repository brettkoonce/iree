# Copyright 2019 Google LLC
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      https://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

load(
    "//iree:build_defs.bzl",
    "INTREE_TENSORFLOW_PY_DEPS",
)

package(
    default_visibility = ["//visibility:public"],
    licenses = ["notice"],  # Apache 2.0
)

DEFAULT_COPTS = [
    "-fexceptions",
]

DEFAULT_FEATURES = [
    "-use_header_modules",  # Incompatible with exceptions builds.
]

cc_library(
    name = "tf_interop",
    hdrs = [
        "register_tensorflow.h",
    ],
    copts = DEFAULT_COPTS,
    defines = select({
        "//iree:enable_tensorflow": [
            "IREE_TENSORFLOW_ENABLED",
        ],
        "//conditions:default": [],
    }),
    features = DEFAULT_FEATURES,
    # TODO(b/145815906) Get this running in OSS CI.
    tags = ["nokokoro"],
    deps = select({
        "//iree:enable_tensorflow": [
            ":tensorflow_impl",
        ],
        "//conditions:default": [
            ":tensorflow_disabled",
        ],
    }) + [
        "//bindings/python/pyiree:base",
    ],
)

# Runtime deps needed to compile the tensorflow compiler.
SAVED_MODEL_TF_RUNTIME_DEPS = [
    "@org_tensorflow//tensorflow/core:ops",
]

TF_XLA_PASS_DEPS = [
    "//integrations/tensorflow/compiler:tensorflow",
    "@org_tensorflow//tensorflow/compiler/mlir/xla:xla_legalize_tf",
]

cc_library(
    name = "tensorflow_impl",
    srcs = [
        "register_tensorflow.cc",
    ],
    hdrs = [
        "register_tensorflow.h",
    ],
    copts = DEFAULT_COPTS,
    features = DEFAULT_FEATURES,
    # TODO(b/145815906) Get this running in OSS CI.
    tags = ["nokokoro"],
    visibility = ["//visibility:private"],
    deps = [
        "@local_config_mlir//:IR",
        "@llvm//:support",
        "@org_tensorflow//tensorflow/cc/saved_model:loader_lite",
        "@org_tensorflow//tensorflow/compiler/mlir/tensorflow",
        "@org_tensorflow//tensorflow/compiler/mlir/tensorflow:convert_graphdef",
        "//bindings/python/pyiree:base",
    ] + SAVED_MODEL_TF_RUNTIME_DEPS + TF_XLA_PASS_DEPS,
)

cc_library(
    name = "tensorflow_disabled",
    srcs = [
        "register_tensorflow_noop.cc",
    ],
    hdrs = [
        "register_tensorflow.h",
    ],
    copts = DEFAULT_COPTS,
    features = DEFAULT_FEATURES,
    # TODO(b/145815906) Get this running in OSS CI.
    tags = ["nokokoro"],
    deps = [
        "//bindings/python/pyiree:base",
    ],
)

py_library(
    name = "tf_test_driver",
    srcs = ["tf_test_driver.py"],
    # TODO(b/145815906) Get this running in OSS CI.
    tags = ["nokokoro"],
    deps = INTREE_TENSORFLOW_PY_DEPS + [
        "//bindings/python/pyiree:binding",
    ],
)

py_library(
    name = "test_utils",
    srcs = ["test_utils.py"],
    # TODO(b/145815906) Get this running in OSS CI.
    tags = ["nokokoro"],
    deps = INTREE_TENSORFLOW_PY_DEPS + [
        "//bindings/python/pyiree:compiler",
        "//bindings/python/pyiree:binding",
        "//bindings/python/pyiree:system_api",
    ],
)

py_test(
    name = "saved_model_test",
    srcs = ["saved_model_test.py"],
    python_version = "PY3",
    # TODO(b/145815906) Get this running in OSS CI.
    tags = ["nokokoro"],
    deps = [
        "//bindings/python/pyiree",
        "//bindings/python:pathsetup",  # build_cleaner: keep
    ] + INTREE_TENSORFLOW_PY_DEPS,
)
