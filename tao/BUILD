load("@com_google_protobuf//:protobuf.bzl", "cc_proto_library")
load("@local_config_tf//:build_defs.bzl", "tf_copts")
load("@local_config_cuda//cuda:build_defs.bzl", "cuda_library", "if_cuda")
load("//:tao_bridge.bzl", "tao_bridge_cc_test")
load("@local_config_blade_disc_helper//:build_defs.bzl",
    "disc_build_version",
    "disc_build_git_branch",
    "disc_build_git_head",
    "disc_build_host",
    "disc_build_ip",
    "disc_build_time",
    "if_platform_alibaba",
)
load("@org_tensorflow//tensorflow:tensorflow.bzl", "tf_cc_shared_object")
load("@local_config_rocm//rocm:build_defs.bzl", "if_dcu", "if_rocm")

package(default_visibility = ["//visibility:public"])

############################################################

filegroup(
    name = "input_version_header",
    srcs = [
        "tao_bridge/version.h.in"
    ],
)

genrule(
    name = "version_header_genrule",
    srcs = [
        ":input_version_header"
    ],
    outs = ["tao_bridge/version.h"],
    cmd = """pwd;
        cp $(location :input_version_header) $@;
        sed -i 's/cmakedefine/define/g' $@;
        sed -i 's/@TAO_BUILD_VERSION@/%s/g' $@;
        sed -i 's/@TAO_BUILD_GIT_BRANCH@/%s/g' $@;
        sed -i 's/@TAO_BUILD_GIT_HEAD@/%s/g' $@;
        sed -i 's/@TAO_BUILD_HOST@/%s/g' $@;
        sed -i 's/@TAO_BUILD_IP@/%s/g' $@;
        sed -i 's/@TAO_BUILD_TIME@/%s/g' $@;
        chmod +w $@
    """ % (disc_build_version(), disc_build_git_branch(), disc_build_git_head(), disc_build_host(), disc_build_ip(), disc_build_time()),
)

cc_library(
    name = "version_header",
    srcs = ["tao_bridge/version.h"],
    data = [
        ':version_header_genrule'
    ],
)

############################################################

cc_proto_library(
    name = "tao_compiler_input",
    srcs = [
        "tao_bridge/tao_compiler_input.proto",
    ],
)

cc_proto_library(
    name = "tao_compilation_result",
    srcs = [
        "tao_bridge/tao_compilation_result.proto",
    ],
)

cc_proto_library(
    name = "xla_activity",
    srcs = [
        "tao_bridge/xla_activity.proto",
    ],
)

cc_library(
    name = "tao_bridge",
    srcs = glob(
        include = [
            "tao_bridge/**/*.cc"
        ],
        exclude = [
            "tao_bridge/**/*test.cc",
            "tao_bridge/gpu/bace_gemm_thunk.cc",
        ],
    ) + [
        ":tao_compiler_input_genproto",
        ":tao_compilation_result_genproto",
        ":xla_activity_genproto",
    ],
    hdrs = glob(
        include = [
            "tao_bridge/**/*.h"
        ] + [
            "third_party/json/single_include/nlohmann/json.hpp",
        ],
        exclude = [
            "tao_bridge/gpu/bace_gemm_thunk.h",
        ],
    ),
    deps = [
        ":version_header",
        "@local_config_tf//:tf_header_lib",
        "@local_config_tf//:libtensorflow_framework",
        "@org_tao_compiler//tensorflow/compiler/mlir/xla/ral:ral_bridge",
    ] + if_platform_alibaba([
        "@blade_service_common//blade_service_common:blade_service_common_deps",
    ]),
    copts = if_cuda([
        "-DGOOGLE_CUDA=1"
    ]) + if_dcu([
        "-DTENSORFLOW_USE_DCU=1"
    ]) + if_rocm([
        "-DTENSORFLOW_USE_ROCM=1",
        "-x rocm",
    ])+ tf_copts(),
    linkopts = [
        "-lm",
    ],
    alwayslink = 1,  # targets depending on it should carry all symbols in its children.
    linkstatic = 1,
)

tf_cc_shared_object(
    name = "libtao_ops.so",
    linkopts = select({
        "//conditions:default": [
            "-z defs",
            "-Wl,--version-script",  #  This line must be directly followed by the version_script.lds file
            "$(location //:bridge_version_scripts.lds)",
        ],
    }),
    deps = [
        ":tao_bridge",
        "//:bridge_version_scripts.lds",
    ] + if_rocm([
        "@local_config_rocm//rocm:rocblas",
    ]),
)

tao_bridge_cc_test(
    name = "tao_bridge_tests",
    srcs = [
        "tao_bridge/common_test.cc",
        "tao_bridge/cuda_utils_test.cc",
        "tao_bridge/dumper_common_test.cc",
        "tao_bridge/errors_test.cc",
        "tao_bridge/tao_util_test.cc",
    ],
    tags = ["gpu"],
)

tao_bridge_cc_test(
    name = "tao_port_tests",
    srcs = [
        "tao_bridge/tf/const_analysis_test.cc",
        "tao_bridge/tf/deadness_analysis_test.cc",
        "tao_bridge/tf/graphcycles_test.cc",
        "tao_bridge/tf/resource_operation_safety_analysis_test.cc",
        "tao_bridge/tf/resource_operation_table_test.cc",
        "tao_bridge/tf/util_test.cc",
        "tao_bridge/tf/xla_cluster_util_test.cc",
        "tao_bridge/tf/xla_op_registry_test.cc",
    ],
)

tao_bridge_cc_test(
    name = "tao_passes_tests",
    srcs = [
        "tao_bridge/passes/tao_bace_reformat_pass_test.cc",
        "tao_bridge/passes/tao_encapsulate_subgraphs_pass_test.cc",
        # "tao_bridge/passes/tao_partially_decluster_pass_test.cc",
    ],
)

tao_bridge_cc_test(
    name = "tao_cpu_passes_tests",
    srcs = [
        "tao_bridge/passes/tao_clone_constants_for_better_clustering_test.cc",
        "tao_bridge/passes/tao_defuse_pass_test.cc",
        "tao_bridge/passes/tao_remove_small_cluster_pass_test.cc",
    ],
    tags = ["cpu"],
)
