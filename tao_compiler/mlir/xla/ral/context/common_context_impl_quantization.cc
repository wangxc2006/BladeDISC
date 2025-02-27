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

#if defined(TAO_CPU_ONLY) && defined(TAO_AARCH64)

#include <sstream>
#include <thread>

#include "arm_compute/core/utils/quantization/AsymmHelpers.h"
#include "tensorflow/compiler/mlir/xla/ral/context/common_context_impl_mkldnn.h"

namespace tao {
namespace ral {

using namespace arm_compute;

namespace {

template <int NDims>
void ral_qconv_s8_s8_s8(
    ExecutionContext* ctx, opaque_t /*stream_handle*/,
    MemRefType<int8_t, NDims> input, MemRefType<int8_t, NDims> kernel,
    MemRefType<int32_t, 1> padding, MemRefType<float, 0> inputScales,
    MemRefType<float, 1> filterScales, MemRefType<float, 0> outputScales,
    MemRefType<int8_t, NDims> output, MemRefType<int32_t, 1> metadata) {
  CpuTimer timer("ral_qconv_s8_s8_s8");
  if (isEmptyMemref(input) || isEmptyMemref(kernel) || isEmptyMemref(output)) {
    TAO_VLOG(1) << "ral_qconv_s8_s8_s8: early return for empty tensor";
    return;
  }
  ConvParams params;
  if (!parseConvParams(ctx, input, kernel, padding, output, metadata,
                       &params)) {
    ctx->signalError(Context::FAILURE, "invalid conv params");
  }

  if (TAO_VLOG_IS_ON(1)) {
    TAO_VLOG(0) << "input scale = " << inputScales.data[0];
    TAO_VLOG(0) << "output scale = " << outputScales.data[0];
    for (int i = 0; i < filterScales.sizes[0]; ++i)
      TAO_VLOG(0) << "filter_scale[" << i << "] = " << filterScales.data[i];
  }

  if (params.groups > 1) {
    ctx->signalError(Context::FAILURE, "invalid conv params");
  }

  auto src_dims = params.src.get_dims();
  auto dst_dims = params.dst.get_dims();
  auto weight_dims = params.weight.get_dims();
  int N = src_dims[0];
  int Ci = src_dims[1];
  int Ih = src_dims[2];
  int Iw = src_dims[3];
  int Co = dst_dims[1];
  int Oh = dst_dims[2];
  int Ow = dst_dims[3];
  int Kh = weight_dims[2];
  int Kw = weight_dims[3];

  if (TAO_VLOG_IS_ON(1)) {
    TAO_VLOG(1) << "N = " << N;
    TAO_VLOG(1) << "Ih = " << Ih;
    TAO_VLOG(1) << "Iw = " << Iw;
    TAO_VLOG(1) << "Ci = " << Ci;
    TAO_VLOG(1) << "Oh = " << Oh;
    TAO_VLOG(1) << "Ow = " << Ow;
    TAO_VLOG(1) << "Co = " << Co;
    TAO_VLOG(1) << "Kh = " << Kh;
    TAO_VLOG(1) << "Kw = " << Kw;
    TAO_VLOG(0) << "params.strides[1] = " << params.strides[1];
    TAO_VLOG(0) << "params.strides[0] = " << params.strides[0];
    TAO_VLOG(0) << "params.padding_l[1] = " << params.padding_l[1];
    TAO_VLOG(0) << "params.padding_l[0] = " << params.padding_l[0];
    TAO_VLOG(0) << "params.padding_r[1] = " << params.padding_r[1];
    TAO_VLOG(0) << "params.padding_r[0] = " << params.padding_r[0];
    TAO_VLOG(0) << "params.dilates[1] = " << params.dilates[1];
    TAO_VLOG(0) << "params.dilates[0] = " << params.dilates[0];
  }

  auto AclQconvCreator = [&](const arm_compute::ITensorPack* pack) {
    std::shared_ptr<AclConvInfo> info(new AclConvInfo);
    auto src_shape = TensorShape(Ci, Iw, Ih, N);
    auto weights_shape = TensorShape(Ci, Kw, Kh, Co);
    auto dst_shape = TensorShape(Co, Ow, Oh, N);

    DataLayout data_layout = DataLayout::NHWC;
    DataType data_type = DataType::QASYMM8_SIGNED;
    TensorInfo src_info = TensorInfo(src_shape, 1, data_type, data_layout);
    TensorInfo weights_info =
        TensorInfo(weights_shape, 1, DataType::QSYMM8_PER_CHANNEL, data_layout);
    TensorInfo dst_info = TensorInfo(dst_shape, 1, data_type, data_layout);

    const QuantizationInfo src_qinfo = QuantizationInfo();
    src_info.set_quantization_info(QuantizationInfo(*inputScales.data, 0));
    std::vector<float> scales(filterScales.data,
                              filterScales.data + filterScales.sizes[0]);
    weights_info.set_quantization_info(QuantizationInfo(std::move(scales)));
    dst_info.set_quantization_info(QuantizationInfo(*outputScales.data, 0));

    info->src.allocator()->init(src_info);
    info->weights.allocator()->init(weights_info);
    info->dst.allocator()->init(dst_info);
    info->src.allocator()->import_memory(input.data);
    info->weights.allocator()->import_memory(kernel.data);
    info->dst.allocator()->import_memory(output.data);

    if (!info->op.validate(
            &src_info, &weights_info, nullptr, &dst_info,
            PadStrideInfo{params.strides[1], params.strides[0],
                          params.padding_l[1], params.padding_r[1],
                          params.padding_l[0], params.padding_r[0],
                          DimensionRoundingType::FLOOR},
            WeightsInfo{}, Size2D{params.dilates[1], params.dilates[0]})) {
      ctx->signalError(Context::FAILURE, "fail to validate acl depthwise conv");
    } else {
      info->op.configure(&info->src, &info->weights, nullptr, &info->dst,
                         PadStrideInfo{params.strides[1], params.strides[0],
                                       params.padding_l[1], params.padding_r[1],
                                       params.padding_l[0], params.padding_r[0],
                                       DimensionRoundingType::FLOOR},
                         WeightsInfo{},
                         Size2D{params.dilates[1], params.dilates[0]});
    }
    if (pack) info->op.reuse_packed_weight(*pack);
    info->op.prepare(&info->src, &info->weights, nullptr, &info->dst);
    return info;
  };

  std::shared_ptr<AclConvInfo> info;
  std::shared_ptr<AclConvThreadSafeInfo> thread_safe_info;
  if (isWeightPrePackingEnabled() && params.weight_is_const) {
    std::string unique_name = "tao_ral.cpu.acl_qconv_s8s8s8";
    auto state = ctx->getOrCreateResource<AclConvState>(
        unique_name, []() { return new AclConvState; });
    auto key = makeConvParamsKey(input, kernel, padding, output, metadata,
                                 kDiscCpuDefaultThreadId);
    auto dynamicKey = makeDynamicShapeConvParamsKey(
        input, kernel, padding, output, metadata, kDiscCpuDefaultThreadId);
    thread_safe_info = state->getOrCreate(dynamicKey);
    info = thread_safe_info->getOrCreate(key, AclQconvCreator);
  } else {
    info = AclQconvCreator(nullptr);
  }

  // TOOD(disc): re-import quantization info when online-quantization is
  // supported.
  info->src.allocator()->import_memory(input.data);
  info->dst.allocator()->import_memory(output.data);
  info->op.run(&info->src, &info->weights, nullptr, &info->dst);

  timer.Stop();
  if (isProfilingEnabled()) {
    dumpConvLikeKernelProflingInfo<int8_t>(params, timer.GetNanoSeconds(),
                                           "ral_qconv_s8_s8_s8");
  }
}

template <int NDims>
void ral_qconv_acl_s8_s8_s8_per_channel(
    ExecutionContext* ctx, opaque_t /*stream_handle*/,
    MemRefType<int8_t, NDims> input, MemRefType<int8_t, NDims> weight,
    MemRefType<int32_t, 1> padding, MemRefType<float, 0> inputScales,
    MemRefType<int32_t, 0> inputZeroPoints, MemRefType<float, 1> weightScales,
    MemRefType<int32_t, 1> weightZeroPoints, MemRefType<float, 0> resultScales,
    MemRefType<int32_t, 0> resultZeroPoints, MemRefType<int8_t, NDims> result,
    MemRefType<int32_t, 1> metadata) {
  CpuTimer timer("ral_qconv_acl_s8_s8_s8_per_channel");
  if (isEmptyMemref(input) || isEmptyMemref(weight) || isEmptyMemref(result)) {
    TAO_VLOG(1)
        << "ral_qconv_acl_s8_s8_s8_per_channel: early return for empty tensor";
    return;
  }
  ConvParams params;
  if (!parseConvParams(ctx, input, weight, padding, result, metadata,
                       &params)) {
    ctx->signalError(Context::FAILURE, "invalid conv params");
  }

  applyACLThreadPoolConfigIfNotSet();

  if (TAO_VLOG_IS_ON(2)) {
    for (int i = 0; i < Size(input); ++i) {
      TAO_VLOG(0) << "input[" << i
                  << "] = " << static_cast<int32_t>(input.data[i]);
    }
    for (int i = 0; i < Size(weight); ++i) {
      TAO_VLOG(0) << "weight[" << i
                  << "] = " << static_cast<int32_t>(weight.data[i]);
    }
  }

  if (TAO_VLOG_IS_ON(1)) {
    TAO_VLOG(0) << "input scale = " << inputScales.data[0];
    TAO_VLOG(0) << "input zero point = " << inputZeroPoints.data[0];
    TAO_VLOG(0) << "result scale = " << resultScales.data[0];
    TAO_VLOG(0) << "result zero point = " << resultZeroPoints.data[0];
    for (int i = 0; i < weightScales.sizes[0]; ++i)
      TAO_VLOG(0) << "weight_scale[" << i << "] = " << weightScales.data[i];
    for (int i = 0; i < weightZeroPoints.sizes[0]; ++i)
      TAO_VLOG(0) << "weight_zero_point[" << i
                  << "] = " << weightZeroPoints.data[i];
  }

  if (params.groups > 1) {
    ctx->signalError(Context::FAILURE, "invalid conv params");
  }

  auto src_dims = params.src.get_dims();
  auto dst_dims = params.dst.get_dims();
  auto weight_dims = params.weight.get_dims();
  int N = src_dims[0];
  int Ci = src_dims[1];
  int Ih = src_dims[2];
  int Iw = src_dims[3];
  int Co = dst_dims[1];
  int Oh = dst_dims[2];
  int Ow = dst_dims[3];
  int Kh = weight_dims[2];
  int Kw = weight_dims[3];

  if (TAO_VLOG_IS_ON(1)) {
    TAO_VLOG(1) << "N = " << N;
    TAO_VLOG(1) << "Ih = " << Ih;
    TAO_VLOG(1) << "Iw = " << Iw;
    TAO_VLOG(1) << "Ci = " << Ci;
    TAO_VLOG(1) << "Oh = " << Oh;
    TAO_VLOG(1) << "Ow = " << Ow;
    TAO_VLOG(1) << "Co = " << Co;
    TAO_VLOG(1) << "Kh = " << Kh;
    TAO_VLOG(1) << "Kw = " << Kw;
    TAO_VLOG(0) << "params.strides[1] = " << params.strides[1];
    TAO_VLOG(0) << "params.strides[0] = " << params.strides[0];
    TAO_VLOG(0) << "params.padding_l[1] = " << params.padding_l[1];
    TAO_VLOG(0) << "params.padding_l[0] = " << params.padding_l[0];
    TAO_VLOG(0) << "params.padding_r[1] = " << params.padding_r[1];
    TAO_VLOG(0) << "params.padding_r[0] = " << params.padding_r[0];
    TAO_VLOG(0) << "params.dilates[1] = " << params.dilates[1];
    TAO_VLOG(0) << "params.dilates[0] = " << params.dilates[0];
  }

  auto AclQconvCreator = [&](const arm_compute::ITensorPack* pack) {
    std::shared_ptr<AclConvInfo> info(new AclConvInfo);
    auto src_shape = TensorShape(Ci, Iw, Ih, N);
    auto weights_shape = TensorShape(Ci, Kw, Kh, Co);
    auto dst_shape = TensorShape(Co, Ow, Oh, N);

    DataLayout data_layout = DataLayout::NHWC;
    DataType data_type = DataType::QASYMM8_SIGNED;
    TensorInfo src_info = TensorInfo(src_shape, 1, data_type, data_layout);
    TensorInfo weights_info =
        TensorInfo(weights_shape, 1, DataType::QSYMM8_PER_CHANNEL, data_layout);
    TensorInfo dst_info = TensorInfo(dst_shape, 1, data_type, data_layout);

    const QuantizationInfo src_qinfo = QuantizationInfo();
    src_info.set_quantization_info(
        QuantizationInfo(*inputScales.data, *inputZeroPoints.data));
    std::vector<float> scales(weightScales.data,
                              weightScales.data + weightScales.sizes[0]);
    std::vector<int32_t> zero_points(
        weightZeroPoints.data,
        weightZeroPoints.data + weightZeroPoints.sizes[0]);
    weights_info.set_quantization_info(
        QuantizationInfo(std::move(scales), std::move(zero_points)));
    dst_info.set_quantization_info(
        QuantizationInfo(*resultScales.data, *resultZeroPoints.data));

    info->src.allocator()->init(src_info);
    info->weights.allocator()->init(weights_info);
    info->dst.allocator()->init(dst_info);
    info->src.allocator()->import_memory(input.data);
    info->weights.allocator()->import_memory(weight.data);
    info->dst.allocator()->import_memory(result.data);

    if (!info->op.validate(
            &src_info, &weights_info, nullptr, &dst_info,
            PadStrideInfo{params.strides[1], params.strides[0],
                          params.padding_l[1], params.padding_r[1],
                          params.padding_l[0], params.padding_r[0],
                          DimensionRoundingType::FLOOR},
            WeightsInfo{}, Size2D{params.dilates[1], params.dilates[0]})) {
      ctx->signalError(
          Context::FAILURE,
          "fail to validate ral_qconv_acl_s8_s8_s8_per_channel conv");
    } else {
      info->op.configure(&info->src, &info->weights, nullptr, &info->dst,
                         PadStrideInfo{params.strides[1], params.strides[0],
                                       params.padding_l[1], params.padding_r[1],
                                       params.padding_l[0], params.padding_r[0],
                                       DimensionRoundingType::FLOOR},
                         WeightsInfo{},
                         Size2D{params.dilates[1], params.dilates[0]});
    }
    if (pack) info->op.reuse_packed_weight(*pack);
    info->op.prepare(&info->src, &info->weights, nullptr, &info->dst);
    return info;
  };

  std::shared_ptr<AclConvInfo> info;
  std::shared_ptr<AclConvThreadSafeInfo> thread_safe_info;
  if (isWeightPrePackingEnabled() && params.weight_is_const) {
    std::string unique_name = "disc.ral_qconv_acl_s8_s8_s8_per_channel";
    auto state = ctx->getOrCreateResource<AclConvState>(
        unique_name, []() { return new AclConvState; });
    auto key = makeConvParamsKey(input, weight, padding, result, metadata,
                                 kDiscCpuDefaultThreadId);
    auto dynamicKey = makeDynamicShapeConvParamsKey(
        input, weight, padding, result, metadata, kDiscCpuDefaultThreadId);
    thread_safe_info = state->getOrCreate(dynamicKey);
    info = thread_safe_info->getOrCreate(key, AclQconvCreator);
  } else {
    info = AclQconvCreator(nullptr);
  }

  // TOOD(disc): re-import quantization info when online-quantization is
  // supported.
  info->src.allocator()->import_memory(input.data);
  info->dst.allocator()->import_memory(result.data);
  info->op.run(&info->src, &info->weights, nullptr, &info->dst);

  if (TAO_VLOG_IS_ON(2)) {
    for (int i = 0; i < Size(result); ++i) {
      TAO_VLOG(0) << "result[" << i
                  << "] = " << static_cast<int32_t>(result.data[i]);
    }
  }

  timer.Stop();
  if (isProfilingEnabled()) {
    dumpConvLikeKernelProflingInfo<int8_t>(
        params, timer.GetNanoSeconds(), "ral_qconv_acl_s8_s8_s8_per_channel");
  }
}

void ral_qgemm_acl_s8_s8_s8_per_channel(
    ExecutionContext* ctx, opaque_t /*stream_handle*/,
    MemRefType<int8_t, 2> input, MemRefType<int8_t, 2> weight,
    MemRefType<float, 0> inputScales, MemRefType<int32_t, 0> inputZeroPoints,
    MemRefType<float, 1> weightScales, MemRefType<int32_t, 1> weightZeroPoints,
    MemRefType<float, 0> resultScales, MemRefType<int32_t, 0> resultZeroPoints,
    MemRefType<int8_t, 2> result, bool tp_a, bool tp_b, bool weight_is_const) {
  CpuTimer timer("ral_cpu_qgemm");
  if (isEmptyMemref(input) || isEmptyMemref(weight) || isEmptyMemref(result)) {
    TAO_VLOG(1) << "ral_cpu_batch_gemm: early return for empty tensor";
    return;
  }

  // ACL GEMM kernel does not support transpose.
  // TODO(disc): use standalone ACL transpose kernel to imitate.
  if (tp_a || tp_b) {
    ctx->signalError(Context::FAILURE,
                     "not supported ral_qgemm with transpose");
  }

  // Make sure thread pool configuration is set.
  applyACLThreadPoolConfigIfNotSet();

  int64_t m = tp_a ? input.sizes[1] : input.sizes[0];
  int64_t k = tp_a ? input.sizes[0] : input.sizes[1];
  if (k != (tp_b ? weight.sizes[1] : weight.sizes[0])) {
    ctx->signalError(Context::FAILURE, "mismatch contraction dim for gemm");
    return;
  }
  int64_t n = (tp_b ? weight.sizes[0] : weight.sizes[1]);

  auto AclQGemmCreator = [&](const arm_compute::ITensorPack* pack) {
    std::shared_ptr<AclQGemmInfo> info(new AclQGemmInfo);
    auto src_shape = TensorShape(k, m);
    auto weights_shape = TensorShape(n, k);
    auto dst_shape = TensorShape(n, m);

    DataType data_type = DataType::QASYMM8_SIGNED;
    TensorInfo src_info = TensorInfo(src_shape, 1, data_type);
    TensorInfo weights_info =
        TensorInfo(weights_shape, 1, DataType::QSYMM8_PER_CHANNEL);
    TensorInfo dst_info = TensorInfo(dst_shape, 1, data_type);

    src_info.set_quantization_info(
        QuantizationInfo(*inputScales.data, *inputZeroPoints.data));
    std::vector<float> scales(weightScales.data,
                              weightScales.data + weightScales.sizes[0]);
    std::vector<int32_t> zero_points(
        weightZeroPoints.data,
        weightZeroPoints.data + weightZeroPoints.sizes[0]);
    weights_info.set_quantization_info(
        QuantizationInfo(std::move(scales), std::move(zero_points)));
    dst_info.set_quantization_info(
        QuantizationInfo(*resultScales.data, *resultZeroPoints.data));

    info->src.allocator()->init(src_info);
    info->weights.allocator()->init(weights_info);
    info->dst.allocator()->init(dst_info);
    info->src.allocator()->import_memory(input.data);
    info->weights.allocator()->import_memory(weight.data);
    info->dst.allocator()->import_memory(result.data);

    arm_compute::GEMMLowpOutputStageInfo osInfo;
    osInfo.type =
        arm_compute::GEMMLowpOutputStageType::QUANTIZE_DOWN_FIXEDPOINT;
    osInfo.gemmlowp_offset = *resultZeroPoints.data;
    osInfo.output_data_type = data_type;
    osInfo.is_quantized_per_channel = true;
    arm_compute::quantization::calculate_quantized_multipliers(
        src_info.quantization_info(), weights_info.quantization_info(),
        dst_info.quantization_info(), osInfo);
    arm_compute::GEMMInfo gemmInfo;
    gemmInfo.set_gemmlowp_output_stage(osInfo);

    auto status = info->op.validate(&src_info, &weights_info, nullptr,
                                    &dst_info, gemmInfo);
    if (!status) {
      ctx->signalError(
          Context::FAILURE,
          "fail to validate ral_qgemm_acl_s8_s8_s8_per_channel conv: " +
              status.error_description());
    } else {
      info->op.configure(&info->src, &info->weights, nullptr, &info->dst,
                         gemmInfo);
    }
    if (pack) info->op.reuse_packed_weight(*pack);
    info->op.prepare(&info->src, &info->weights, nullptr, &info->dst);
    return info;
  };

  std::shared_ptr<AclQGemmInfo> info;
  std::shared_ptr<AclQGemmThreadSafeInfo> thread_safe_info;
  if (isWeightPrePackingEnabled() && weight_is_const) {
    std::string unique_name = "disc.ral_qgemm_acl_s8_s8_s8_per_channel";
    auto state = ctx->getOrCreateResource<AclQGemmState>(
        unique_name, []() { return new AclQGemmState; });
    auto key = makeGEMMParamsKey(input, weight, result, tp_a, tp_b,
                                 weight_is_const, kDiscCpuDefaultThreadId);
    auto dynamicKey =
        makeDynamicShapeGEMMParamsKey(input, weight, result, tp_a, tp_b,
                                      weight_is_const, kDiscCpuDefaultThreadId);
    thread_safe_info = state->getOrCreate(dynamicKey);
    info = thread_safe_info->getOrCreate(key, AclQGemmCreator);
  } else {
    info = AclQGemmCreator(nullptr);
  }

  // TOOD(disc): re-import quantization info when online-quantization is
  // supported.
  info->src.allocator()->import_memory(input.data);
  info->dst.allocator()->import_memory(result.data);
  info->op.run(&info->src, &info->weights, nullptr, &info->dst);

  timer.Stop();

  if (isProfilingEnabled()) {
    int64_t bytes = sizeof(int8_t) * m * k + sizeof(int8_t) * k * n +
                    sizeof(int8_t) * m * n;
    TAO_VLOG(0) << "ral_cpu_gemm:\n"
                << "\tpa = " << input.data << "\n"
                << "\tpb = " << weight.data << "\n"
                << "\tpc = " << result.data << "\n"
                << "\tm = " << m << "\n"
                << "\tn = " << n << "\n"
                << "\tk = " << k << "\n"
                << "\ttp_a = " << tp_a << "\n"
                << "\ttp_b = " << tp_b << "\n"
                << "\tweight_is_const = " << weight_is_const << "\n"
                << "\tMath Ops = " << 2 * m * n * k << "\n"
                << "\tBytes = " << bytes << "\n"
                << "\tBandwidth = "
                << double(bytes) / double(timer.GetNanoSeconds()) << " GB\n"
                << "\tGFLOPS = "
                << double(2 * m * n * k) / double(timer.GetNanoSeconds())
                << "\n";
  }
}

}  // namespace

// Deprecated, remove such implementation once refactor is done.
TAO_RAL_API("ral_qconv_s8_s8_s8", "cpu", ral_qconv_s8_s8_s8<4>);

// new fake_quant based implementation.
TAO_RAL_API("ral_qconv", "cpu", ral_qconv_acl_s8_s8_s8_per_channel<4>);

TAO_RAL_API("ral_qgemm", "cpu", ral_qgemm_acl_s8_s8_s8_per_channel);

}  // namespace ral
}  // namespace tao
#endif
