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
