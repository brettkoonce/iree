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

#include "iree/rt/source_location.h"

#include <sstream>

#include "iree/rt/source_resolver.h"

namespace iree {
namespace rt {

std::string SourceLocation::DebugStringShort() const {
  if (is_unknown()) return "(unknown)";
  std::ostringstream stream;
  resolver_->PrintSourceLocation(resolver_args_, &stream);
  return stream.str();
}

}  // namespace rt
}  // namespace iree