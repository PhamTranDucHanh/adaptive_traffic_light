load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library")
load("@hedron_compile_commands//:refresh_compile_commands.bzl", "refresh_compile_commands")

package(default_visibility = ["//visibility:public"])

refresh_compile_commands(
    name = "refresh_compile_commands",
    targets = {
        "//:traffic_perception": "",
        "//:perception_smoke": "",
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
]

LINUX_LINKOPTS = OPENCV_LINKOPTS + [
    "-lpthread",
    "-lrt",
]

cc_library(
    name = "traffic_perception_core",
    srcs = [
        "src/core/config_manager.cpp",
        "src/inference/inference_engine.cpp",
        "src/inference/model_backend.cpp",
        "src/ingestion/safe_frame_queue.cpp",
        "src/ingestion/stream_watchdog.cpp",
        "src/ingestion/stream_worker.cpp",
        "src/io/multi_lane_viewer.cpp",
        "src/io/snapshot_sender.cpp",
        "src/io/telemetry_manager.cpp",
        "src/perception_module.cpp",
        "src/pipeline/snapshot_publisher.cpp",
        "src/pipeline/traffic_analyzer.cpp",
    ],
    hdrs = [
        "include/traffic_perception/core/config_manager.h",
        "include/traffic_perception/core/types.h",
        "include/traffic_perception/inference/inference_engine.h",
        "include/traffic_perception/inference/model_backend.h",
        "include/traffic_perception/ingestion/safe_frame_queue.h",
        "include/traffic_perception/ingestion/stream_watchdog.h",
        "include/traffic_perception/ingestion/stream_worker.h",
        "include/traffic_perception/io/multi_lane_viewer.h",
        "include/traffic_perception/io/snapshot_sender.h",
        "include/traffic_perception/io/telemetry_manager.h",
        "include/traffic_perception/perception_module.h",
        "include/traffic_perception/pipeline/pipeline_node.h",
        "include/traffic_perception/pipeline/snapshot_publisher.h",
        "include/traffic_perception/pipeline/traffic_analyzer.h",
    ],
    copts = COMMON_COPTS,
    includes = ["include"],
)

cc_binary(
    name = "perception_smoke",
    srcs = ["src/perception_smoke.cpp"],
    copts = COMMON_COPTS,
    linkopts = select({
        "@platforms//os:linux": LINUX_LINKOPTS,
        "//conditions:default": [],
    }),
    deps = [":traffic_perception_core"],
)

cc_library(
    name = "traffic_perception_lifecycle",
    srcs = [
        "src/lifecycle_health_reporter.cpp",
        "src/perception_periodic_service.cpp",
        "src/traffic_perception_application.cpp",
    ],
    hdrs = [
        "include/traffic_perception/lifecycle_health_reporter.h",
        "include/traffic_perception/perception_periodic_service.h",
        "include/traffic_perception/traffic_perception_application.h",
    ],
    copts = COMMON_COPTS,
    includes = ["include"],
    deps = [
        ":traffic_perception_core",
        "@score_baselibs//score/concurrency:interruptible_wait",
        "@score_lifecycle//score/health_monitor:health_monitoring_cc",
        "@score_lifecycle//score/launch_manager:lifecycle_cc",
    ],
)

cc_binary(
    name = "traffic_perception",
    srcs = ["src/main.cpp"],
    copts = COMMON_COPTS,
    linkopts = select({
        "@platforms//os:linux": LINUX_LINKOPTS,
        "//conditions:default": [],
    }),
    deps = [
        ":traffic_perception_lifecycle",
        "@score_baselibs//score/mw/log",
        "@score_lifecycle//score/launch_manager:lifecycle_cc",
    ],
)
