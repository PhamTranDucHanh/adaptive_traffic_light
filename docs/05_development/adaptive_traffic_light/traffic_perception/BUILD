load("@hedron_compile_commands//:refresh_compile_commands.bzl", "refresh_compile_commands")
load("@rules_cc//cc:defs.bzl", "cc_binary")

package(default_visibility = ["//visibility:public"])

alias(
    name = "traffic_perception",
    actual = "//traffic_perception:traffic_perception",
)

alias(
    name = "traffic_perception_core",
    actual = "//traffic_perception:traffic_perception_core",
)

alias(
    name = "perception_smoke",
    actual = "//traffic_perception:perception_smoke",
)

alias(
    name = "module_demo",
    actual = "//traffic_perception:module_demo",
)

alias(
    name = "traffic_perception_system",
    actual = "//deployment:traffic_perception_system",
)
alias(
    name = "analyze_log",
    actual = "//traffic_perception:analyze_log",
)

refresh_compile_commands(
    name = "refresh_compile_commands",
    targets = {
        "//:perception_smoke": "",
        "//:traffic_perception": "",
    },
)

COMMON_COPTS = [
    "-std=c++17",
    "-Wall",
    "-Wextra",
    "-Wpedantic",
    "-isystem/usr/include/opencv4",
]

OPENCV_LINKOPTS = [
    "-lopencv_dnn",
    "-lopencv_highgui",
    "-lopencv_videoio",
    "-lopencv_imgcodecs",
    "-lopencv_imgproc",
    "-lopencv_core",
    "-lpthread",
    "-lrt",
]

cc_binary(
    name = "stream_worker_test",
    srcs = ["test/stream_worker_test.cpp"],
    copts = COMMON_COPTS,
    data = ["//test/data:test_data"],
    linkopts = select({
        "@platforms//os:linux": OPENCV_LINKOPTS,
        "//conditions:default": [],
    }),
    deps = [
        ":traffic_perception_core",
        "@bazel_tools//tools/cpp/runfiles",
    ],
)


cc_binary(
    name = "pipeline_manager_test",
    srcs = ["test/pipeline_manager_test.cpp"],
    copts = COMMON_COPTS,
    data = ["//test/data:test_data", "//config:traffic_perception_config.json", "//config:logging.json", "//config:hm_logging.json", "//config:ecu_logging_config.json"],
    linkopts = select({
        "@platforms//os:linux": OPENCV_LINKOPTS,
        "//conditions:default": [],
    }),
  deps = [
      ":traffic_perception_core",
      "@bazel_tools//tools/cpp/runfiles",
      "@score_baselibs//src/log/stdout_logger_cpp_init:stdout_logger_cpp_init",
      "@score_logging//score/mw/log/backend:file",
      "@score_baselibs//score/mw/log:console",
  ]
)

genrule(
    name = "smoke_package",
    srcs = [
        "//:pipeline_manager_test",
        "//config:all_configs",
        "//test/data:test_data",
        "//lib/onnxruntime:onnxruntime_runtime",
    ],
    outs = ["smoke.tar"],
    cmd = """
        mkdir -p smoke/bin smoke/etc smoke/lib smoke/models
        cp $(location //:pipeline_manager_test) smoke/bin/pipeline_manager_test
        cp $(locations //config:all_configs) smoke/etc/
        cp $(locations //test/data:test_data) smoke/models/
        cp $(locations //lib/onnxruntime:onnxruntime_runtime) smoke/lib/
        tar -cf $@ smoke
    """,
)

sh_binary(
    name = "pipeline_manager_smoke",
    srcs = ["scripts/run_smoke_test.sh"],
    data = [":smoke_package"],
    args = ["$(location :smoke_package)"],
)


cc_binary(
    name = "mq_snapshot_receiver_test",
    srcs = ["test/mq_snapshot_receiver_test.cpp"],
    copts = COMMON_COPTS,
    linkopts = select({
        "@platforms//os:linux": OPENCV_LINKOPTS,
        "//conditions:default": [],
    }),
    deps = [
        ":traffic_perception_core",
    ],
)
