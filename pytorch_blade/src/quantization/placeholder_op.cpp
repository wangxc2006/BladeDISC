// Copyright 2022 The BladeDISC Authors. All rights reserved.
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// http://www.apache.org/licenses/LICENSE-2.0
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "placeholder_op.h"
#include "common_utils/logging.h"

#include <torch/script.h>

namespace torch {
namespace blade {
namespace quantization {

// The type of input cannot be a reference. Because in lower
// torch version, it will not automatically generate a function
// whose input is a reference, which will lead to a "no match"
// compilation error.
torch::Tensor placeholder(torch::Tensor input) {
  LOG(WARNING) << "Placeholder op is only used for prevent the fake_quant "
               << "op being folded by the constant propagation pass. "
               << "You should not inference the graph with this op.";
  return input;
}

TORCH_LIBRARY(torch_blade_quantization, m) {
  m.def("placeholder", placeholder);
}

} // namespace quantization
} // namespace blade
} // namespace torch