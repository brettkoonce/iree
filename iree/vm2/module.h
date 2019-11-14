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

#ifndef IREE_VM_MODULE_H_
#define IREE_VM_MODULE_H_

#include <stdint.h>

#include "iree/base/api.h"

#ifdef __cplusplus
extern "C" {
#endif  // __cplusplus

typedef struct iree_vm_module iree_vm_module_t;
typedef struct iree_vm_stack iree_vm_stack_t;
typedef struct iree_vm_stack_frame iree_vm_stack_frame_t;

// Describes the type of a function reference.
typedef enum {
  // Function is internal to the module and may not be reflectable.
  IREE_VM_FUNCTION_LINKAGE_INTERNAL = 0,
  // Function is an import from another module.
  IREE_VM_FUNCTION_LINKAGE_IMPORT = 1,
  // Function is an export from the module.
  IREE_VM_FUNCTION_LINKAGE_EXPORT = 2,
} iree_vm_function_linkage_t;

// A function reference that can be used with the iree_vm_function_* methods.
// These should be treated as opaque and the accessor functions should be used
// instead.
typedef struct {
  // Module the function is contained within.
  iree_vm_module_t* module;
  // Linkage of the function. Note that IREE_VM_FUNCTION_LINKAGE_INTERNAL
  // functions may be missing reflection information.
  iree_vm_function_linkage_t linkage;
  // Ordinal within the module in the linkage scope.
  int32_t ordinal;
} iree_vm_function_t;

// Describes the expected calling convention and arguments/results of a
// function.
typedef struct {
  // Total number of arguments to the function.
  int32_t argument_count;
  // Total number of results from the function.
  int32_t result_count;
} iree_vm_function_signature_t;

// Describes the imports, exports, and capabilities of a module.
typedef struct {
  // Total number of imported functions.
  int32_t import_function_count;
  // Total number of exported functions.
  int32_t export_function_count;
  // Total number of internal functions, if debugging info is present and they
  // can be queried.
  int32_t internal_function_count;
} iree_vm_module_signature_t;

// Internal storage for the module state.
// Thread-compatible; it's expected that only one thread at a time is executing
// VM functions and accessing this state.
typedef struct iree_vm_module_state iree_vm_module_state_t;

// Results of an iree_vm_module_execute request.
typedef struct {
  // TODO(benvanik): yield information.
  // Yield modes:
  // - yield (yield instruction)
  // - await (with 1+ wait handles)
  // - break
} iree_vm_execution_result_t;

// Defines an interface that can be used to reflect and execute functions on a
// module.
//
// Module implementations must be thread-safe as lookups and executions may
// occur in any order from any thread.
// TODO(benvanik): version this interface.
typedef struct iree_vm_module {
  void* self;

  // Destroys |self| when all references to the module have been released.
  iree_status_t(IREE_API_PTR* destroy)(void* self);

  // Returns the name of the module (used during resolution).
  iree_string_view_t(IREE_API_PTR* name)(void* self);

  // Returns the reflected signature of the module.
  iree_vm_module_signature_t(IREE_API_PTR* signature)(void* self);

  // Gets one or more pieces of function information:
  // - |out_function| set to the function reference.
  // - |out_name| set to the function name.
  // - |out_signature| set to the function signature.
  iree_status_t(IREE_API_PTR* get_function)(
      void* self, iree_vm_function_linkage_t linkage, int32_t ordinal,
      iree_vm_function_t* out_function, iree_string_view_t* out_name,
      iree_vm_function_signature_t* out_signature);

  // Looks up a function with the given name and linkage in the module.
  // This may perform a linear scan and results should be cached.
  iree_status_t(IREE_API_PTR* lookup_function)(
      void* self, iree_vm_function_linkage_t linkage, iree_string_view_t name,
      iree_vm_function_t* out_function);

  // Allocates module state data.
  iree_status_t(IREE_API_PTR* alloc_state)(
      void* self, iree_allocator_t allocator,
      iree_vm_module_state_t** out_module_state);

  // Frees module state data.
  iree_status_t(IREE_API_PTR* free_state)(void* self,
                                          iree_vm_module_state_t* module_state);

  // Resolves the import with the given ordinal to |function|.
  // The function is guaranteed to remain valid for the lifetime of the module
  // state.
  iree_status_t(IREE_API_PTR* resolve_import)(
      void* self, iree_vm_module_state_t* module_state, int32_t ordinal,
      iree_vm_function_t function);

  // Asynchronously executes the function specified in the |frame|.
  // This may be called repeatedly for the same frame if the execution
  // previously yielded. The offset within the frame is preserved across calls.
  iree_status_t(IREE_API_PTR* execute)(void* self, iree_vm_stack_t* stack,
                                       iree_vm_stack_frame_t* frame,
                                       iree_vm_execution_result_t* out_result);
} iree_vm_module_t;

#ifdef __cplusplus
}  // extern "C"
#endif  // __cplusplus

#endif  // IREE_VM_MODULE_H_