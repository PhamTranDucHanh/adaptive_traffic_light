# Báo cáo Trạng thái Bazel Build: `traffic_perception`

## 1. Tổng quan
Hiện tại, dự án `traffic_perception` đã được cấu hình Bazel và có thể build/run thành công. 

- **Bazel version:** 8.6.0
- **Cấu hình chính:** Sử dụng `WORKSPACE` (legacy), Bzlmod đã bị vô hiệu hóa (`.bazelrc`: `--noenable_bzlmod --enable_workspace`).
- **Dependencies:** Sử dụng thư viện OpenCV cài đặt tại hệ thống (`/usr`).

## 2. Ưu điểm
- **Build thành công:** Toàn bộ dự án build được hoàn chỉnh bằng `bazel build //...`.
- **Độ tin cậy:** Quy trình build nhất quán trên cùng một môi trường máy.
- **Tối ưu thời gian:** Sử dụng hệ thống thư viện OpenCV có sẵn trên máy (không cần build lại từ source, tiết kiệm tài nguyên và thời gian compile).
- **Rõ ràng:** Các phụ thuộc hệ thống đã được liệt kê cụ thể trong `BUILD` file.

## 3. Nhược điểm (Technical Debt)
- **Thiếu tính hermetic:** Dựa hoàn toàn vào các đường dẫn hardcoded trên hệ thống (`/usr/include/opencv4`), làm mất khả năng chạy build trên các môi trường khác (Linux distros khác, hoặc nơi OpenCV cài ở vị trí khác).
- **Sử dụng WORKSPACE (Deprecated):** Bazel 9 sẽ loại bỏ hỗ trợ cho `WORKSPACE`. Cấu hình hiện tại cần được migrate sang Bzlmod sớm để đảm bảo khả năng nâng cấp.
- **Propagation issues:** Phải cấu hình thủ công `copts` và `linkopts` trên binary (`module_demo`) thay vì để `traffic_perception_lib` tự động quản lý các flags này thông qua `cc_library`.
- **Cấu hình lỏng lẻo:** Việc truyền thủ công các thư viện link như `-lopencv_videoio` khiến `BUILD` file dễ bị lỗi khi cập nhật phiên bản OpenCV hoặc thay đổi cấu trúc thư mục của hệ thống.

## 4. Đề xuất cải tiến (Dành cho AI Pro Review)
1. **Migrate sang Bzlmod:** Chuyển đổi sang `MODULE.bazel`.
2. **Sử dụng `rules_pkg` hoặc tương đương:** Để xử lý các dependencies hệ thống một cách chuyên nghiệp hơn thay vì hardcoded paths.
3. **Cấu trúc lại `cc_library`:** Đóng gói OpenCV vào một `cc_library` nội bộ đúng cách để các binary consumer (`module_demo`) chỉ cần `deps = [":traffic_perception_lib"]` mà không cần biết về `copts`/`linkopts`.
