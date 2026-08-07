package(default_visibility = ["//visibility:public"])

RUNTIME_ASSETS = [
    "traffic.mp4",
    "traffic2.mp4",
    "traffic3.mp4",
    "traffic4.mp4",
    "rt-detrv2-s.onnx",
    "yolov8m-oiv7.onnx",
    "yolov8n-oiv7.onnx",
    "yolov8m.onnx",
    "yolov8n.onnx",
]

exports_files(RUNTIME_ASSETS)

filegroup(
    name = "runtime_assets",
    srcs = RUNTIME_ASSETS,
)
