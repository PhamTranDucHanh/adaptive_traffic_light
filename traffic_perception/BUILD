
cc_library(
    name = "traffic_perception_lib",
    srcs = glob(["src/**/*.cpp"]),
    hdrs = glob(["include/**/*.h"]),
    includes = ["include"],
    copts = ["-std=c++17", "-Wall", "-Wextra", "-Wpedantic", "-I/usr/include/opencv4"],
    linkopts = ["-lrt", "-lpthread", "-lopencv_core", "-lopencv_video", "-lopencv_imgproc"],
    visibility = ["//visibility:public"],
)

cc_binary(
    name = "module_demo",
    srcs = ["src/module_demo.cpp"],
    copts = ["-I/usr/include/opencv4"],
    linkopts = ["-lopencv_videoio", "-lopencv_highgui", "-lopencv_video", "-lopencv_imgproc", "-lopencv_core"],
    deps = [":traffic_perception_lib"],
)
