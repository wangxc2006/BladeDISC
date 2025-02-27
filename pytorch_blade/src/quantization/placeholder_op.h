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

#pragma once
#include <string>

namespace torch {
namespace blade {
namespace quantization {

static const std::string at_fake_quant_per_tensor_affine_name =
    "aten::fake_quantize_per_tensor_affine";
static const std::string at_fake_quant_per_channel_affine_name =
    "aten::fake_quantize_per_channel_affine";
static const std::string custom_placeholder_name =
    "torch_blade_quantization::placeholder";

} // namespace quantization
} // namespace blade
} // namespace torch
