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

iree_bytecode_module(
  NAME
    simple_mul_bytecode_module
  SRC
    "simple_mul.mlir"
  CC_NAMESPACE
    "iree::rt::samples"
)

# Statically link against Vulkan.
# Requires installing the Vulkan SDK from https://vulkan.lunarg.com/.
include(FindVulkan)
if(NOT Vulkan_FOUND)
  message(FATAL_ERROR "Vulkan library was not found")
endif()

iree_cc_binary(
    NAME
      vk_graphics_integration
    SRCS
      "vk_graphics_integration.cc"
    DEPS
      absl::base
      dear_imgui::dear_imgui
      dear_imgui::impl_sdl
      dear_imgui::impl_vulkan
      iree::base::api
      iree::base::logging
      iree::hal::api
      iree::hal::vulkan::api
      iree::rt::api
      iree::samples::rt::vulkan::simple_mul_bytecode_module_cc
      iree::vm::api
      SDL2-static
      Vulkan::Vulkan
)
