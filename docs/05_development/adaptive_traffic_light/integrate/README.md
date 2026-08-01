# Adaptive Traffic Light — chạy tích hợp End-to-End

Deployment chạy ba process thật dưới Eclipse S-CORE Lifecycle:

```text
traffic_perception
    -> /traffic_snapshot_v1
traffic_timing_decision
    -> /traffic_timing_plan_v1
traffic_signal_controller
```

## 1. Chuẩn bị dependency và dữ liệu

Từ thư mục `integrate`, chạy script setup trước tiên:

```bash
cd /root/work/adaptive_traffic_light/docs/05_development/adaptive_traffic_light/integrate
bash traffic_perception/scripts/setup_deps.sh
```

Script thực hiện hai việc và có thể chạy lại an toàn:

- Tải ONNX Runtime `1.27.1` phù hợp với `x86_64` hoặc `aarch64` vào
  `traffic_perception/lib/onnxruntime` nếu chưa có.
- Tải các file còn thiếu từ [Google Drive](https://drive.google.com/drive/folders/1nXzpFTzbHLYzKeyYwoBCRkDmDqH5TnIM)
  vào `data`: `traffic.mp4` đến `traffic4.mp4`, `yolov8m-oiv7.onnx`,
  `yolov8m.onnx` và `yolov8n.onnx`.

Máy cần có `python3`, `python3-pip` và `curl` hoặc `wget`. Nếu chưa có
`gdown`, script cài tạm `gdown 5.2.0`; không cài package Python toàn hệ thống.
Thư mục Google Drive phải cho phép người có link tải file.

Kiểm tra nhanh sau setup:

```bash
test -e traffic_perception/lib/onnxruntime/lib/libonnxruntime.so.1
ls -lh data/*.mp4 data/*.onnx
```

## 2. Cấp giới hạn real-time cho terminal hiện tại

```bash
sudo prlimit --pid $$ --rtprio=99:99 --memlock=unlimited:unlimited
```

Nếu Bazel server đã chạy trước khi cấp quyền, dừng nó để server mới kế thừa
giới hạn real-time từ terminal hiện tại:

```bash
bazel shutdown
```

## 3. Build và chạy End-to-End

```bash
bazel run --config=x86_64-linux //deployment:traffic_light_system
```

Deployment tự động:

1. Build ba application và Launch Manager.
2. Stage binary, config, ONNX Runtime, model và video vào
   `/tmp/linux_rt_application`.
3. Xóa IPC cũ khi toàn hệ thống đang dừng.
4. Khởi động theo thứ tự Perception -> Timing Decision -> Signal Controller.

Không cần tự export `LD_LIBRARY_PATH`; deployment đặt đường dẫn
`/tmp/linux_rt_application/lib` cho các process con.

Output hợp lệ sẽ có các pha đèn như:

```text
Phase: NS_GREEN, Remaining: 29 s
...
Phase: YELLOW, Remaining: 1 s
Phase: ALL_RED, Remaining: 1 s
Phase: EW_GREEN, Remaining: ...
```

Các file log chính:

```text
/tmp/linux_rt_application/logs/traffic_perception.dlt
/tmp/linux_rt_application/logs/timing_decision.dlt
/tmp/linux_rt_application/logs/signal_control.dlt
/tmp/linux_rt_application/logs/CTRL.dlt.txt
```

Dừng hệ thống bằng `Ctrl-C`. Lifecycle sẽ dừng các process theo dependency;
`shutdown_timeout` hiện là 10 giây.

## Lệnh đầy đủ

```bash
cd /root/work/adaptive_traffic_light/docs/05_development/adaptive_traffic_light/integrate
bash traffic_perception/scripts/setup_deps.sh
sudo prlimit --pid $$ --rtprio=99:99 --memlock=unlimited:unlimited
bazel shutdown
bazel run --config=x86_64-linux //deployment:traffic_light_system
```
