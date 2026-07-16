cc_library(
    name = "traffic_signal_control_lib",

    srcs = [
        "src/analytics_service/analytics.cpp",
        "src/analytics_service/output_simulator.cpp",

        "src/common/health_reporter.cpp",

        "src/traffic_signal_controller/plan_receiver.cpp",
        "src/traffic_signal_controller/signal_fsm_engine.cpp",
    ],

    hdrs = [
        "include/analytics_service/analytics.h",
        "include/analytics_service/output_simulator.h",

        "include/common/config.h",
        "include/common/health_reporter.h",

        "include/traffic_signal_controller/plan_receiver.h",
        "include/traffic_signal_controller/signal_fsm_engine.h",
    ],

    includes = [
        "include",
    ],

    visibility = ["//visibility:public"],

    deps = [
        "@score_baselibs//score/mw/log:frontend",
        "@score_logging//score/mw/log/backend:file",
        "@score_baselibs//score/mw/log:console",
    ],
)

cc_binary(
    name = "traffic_signal_controller_demo",

    srcs = [
        "src/traffic_signal_controller_demo.cpp",
    ],

    deps = [
        ":traffic_signal_control_lib",
    ],

    data = ["etc/logging.json"],
)
