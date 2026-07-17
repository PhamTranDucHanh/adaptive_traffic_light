# *******************************************************************************
# BUILD - Traffic Signal Controller (corrected)
# *******************************************************************************

COMMON_COPTS = [
    "-Wall",
    "-Wextra",
]

cc_library(
    name = "traffic_signal_control_lib",

    srcs = [
        "src/analytics_service/analytics.cpp",
        "src/analytics_service/output_simulator.cpp",

        "src/common/plan_sync_channel.cpp",

        "src/traffic_signal_controller/plan_receiver.cpp",
        "src/traffic_signal_controller/signal_fsm_engine.cpp",
        "src/traffic_signal_controller/controller_module.cpp",
    ],

    hdrs = [
        "include/analytics_service/analytics.h",
        "include/analytics_service/output_simulator.h",

        "include/common/config.h",
        "include/common/plan_sync_channel.h",
        "include/common/logging_context.h",

        "include/traffic_signal_controller/plan_receiver.h",
        "include/traffic_signal_controller/signal_fsm_engine.h",
        "include/traffic_signal_controller/controller_module.h",
    ],

    includes = [
        "include",
    ],

    copts = COMMON_COPTS,

    visibility = ["//visibility:public"],

    deps = [
        "@score_baselibs//score/mw/log:frontend",
        "@score_baselibs//score/mw/log:console",
        "@score_logging//score/mw/log/backend:file",
    ],
)

cc_binary(
    name = "traffic_signal_controller_demo",

    srcs = [
        "src/traffic_signal_controller_demo.cpp",
    ],

    copts = COMMON_COPTS,

    linkopts = select({
        "@platforms//os:linux": ["-lpthread"],
        "//conditions:default": [],
    }),

    data = ["etc/logging.json"],

    deps = [
        ":traffic_signal_control_lib",
    ],
)

cc_binary(
    name = "controller_logic_smoke",
    srcs = ["src/controller_logic_smoke.cpp"],
    copts = COMMON_COPTS,
    linkopts = select({
        "@platforms//os:linux": ["-lpthread"],
        "//conditions:default": [],
    }),
    deps = [":traffic_signal_control_lib"],
)

cc_library(
    name = "traffic_signal_controller_application",
    srcs = [
        "src/controller_periodic_service.cpp",
        "src/lifecycle_health_reporter.cpp",
        "src/traffic_signal_controller_application.cpp",
    ],
    hdrs = [
        "include/traffic_signal_controller/controller_periodic_service.h",
        "include/traffic_signal_controller/lifecycle_health_reporter.h",
        "include/traffic_signal_controller/traffic_signal_controller_application.h",
        "include/traffic_signal_controller/controller_module.h",
    ],
    copts = COMMON_COPTS,
    includes = ["include"],
    deps = [
        ":traffic_signal_control_lib",
        "@score_baselibs//score/concurrency:interruptible_wait",
        "@score_lifecycle//score/health_monitor:health_monitoring_cc",
        "@score_lifecycle//score/launch_manager:lifecycle_cc",
    ],
)

cc_binary(
    name = "traffic_signal_controller",
    srcs = ["src/main.cpp"],
    copts = COMMON_COPTS,
    linkopts = select({
        "@platforms//os:linux": [
            "-lpthread",
            "-lrt",
        ],
        "//conditions:default": [],
    }),
    data = ["etc/logging.json"],
    deps = [
        ":traffic_signal_controller_application",
        "@score_baselibs//score/mw/log:frontend",
        "@score_baselibs//score/mw/log:console",
        "@score_logging//score/mw/log/backend:file",
        "@score_lifecycle//score/launch_manager:lifecycle_cc",
    ],
)