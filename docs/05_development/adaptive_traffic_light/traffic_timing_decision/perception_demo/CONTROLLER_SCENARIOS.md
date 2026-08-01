# Các kịch bản Perception chuyên kiểm thử Controller

Macro `END_TO_END_TEST` dùng để chọn bộ kịch bản này trong file
`perception_application.cpp`. Nếu bỏ định nghĩa macro, chương trình sẽ quay lại
sử dụng 14 kịch bản kiểm thử Timing Decision ban đầu.

Chu kỳ hoạt động của các process trong bộ kiểm thử Controller:

- Perception gửi một snapshot sau mỗi 2 giây.
- Timing Decision xử lý dữ liệu sau mỗi 2,5 giây.
- FSM của Traffic Signal Controller chạy sau mỗi 1 giây.

Do các process có chu kỳ khác nhau, những mốc thời gian dưới đây là khoảng thời
gian dự kiến để kiểm thử, không phải deadline của IPC.

| Kịch bản | Input và mục đích | Kết quả mong đợi từ Controller |
|---|---|---|
| 1-2 | Giữ điểm nhu cầu NS bằng 20 và EW bằng 90. Timing Decision điều chỉnh rồi duy trì thời gian xanh `NS=20 giây, EW=50 giây`. | Controller có thể nhận nhiều plan với `result=accepted`, nhưng chỉ giữ normal plan mới nhất ở trạng thái pending. Tại `ALL_RED` đầu tiên, mong đợi xuất hiện `PLAN_CONSUMED`, `PLAN_APPLIED`, sau đó bắt đầu `EW_GREEN` với thời gian 50 giây. |
| 3 | Gửi emergency hướng East khi đang ở `EW_GREEN` và còn khoảng 6-9 giây. | Mong đợi lần lượt xuất hiện `EMERGENCY_QUEUED`, `EMERGENCY_CONSUMED`, `EMERGENCY_ACCEPTED` và `EMERGENCY_APPLIED`. Thời gian còn lại của `EW_GREEN` được đặt lại thành 20 giây. |
| 4 | Bật đồng thời cờ emergency hướng North và East trong 4 giây khi EW vẫn đang xanh. | Khoảng thời gian 4 giây bảo đảm ít nhất một chu kỳ Timing Decision đọc được input. `PlanReceiver` từ chối plan emergency vì cả hai hướng cùng được bật. MQ ghi `result=rejected` và phase xanh hiện tại không bị thay đổi. |
| 5-6 | Xóa toàn bộ cờ emergency và tiếp tục giữ nhu cầu `NS=20 giây, EW=50 giây` trong thời gian còn lại của EW, qua `YELLOW`, `ALL_RED` rồi chuyển sang NS. | Normal plan mới nhất chỉ được áp dụng tại `ALL_RED`. Sau đó, `NS_GREEN` bắt đầu với thời gian 20 giây. |
| 7 | Gửi emergency hướng North khi đang ở `NS_GREEN` và còn khoảng 6-9 giây. | Mong đợi xuất hiện `EMERGENCY_ACCEPTED` và `EMERGENCY_APPLIED`. Thời gian còn lại của `NS_GREEN` được đặt lại thành 20 giây. |
| 8 | Bật đồng thời cờ emergency của cả hai hướng trong 4 giây khi NS vẫn đang xanh. | Khoảng thời gian 4 giây bảo đảm ít nhất một chu kỳ Timing Decision đọc được input. Plan bị từ chối và phase `NS_GREEN` đang hoạt động không bị thay đổi. |
| 9 | Xóa toàn bộ cờ emergency và duy trì nhu cầu ổn định `NS=20 giây, EW=50 giây` cho đến khi hoàn thành chu kỳ hiện tại. | Tại `ALL_RED` tiếp theo, normal plan mới nhất được áp dụng và `EW_GREEN` bắt đầu với thời gian 50 giây. |
| 10 | Đặt NS ở mức trung bình: 40 xe, queue 20, occupancy 40%, tạo score 30. Đặt EW ở mức cao: 60 xe, queue 60, occupancy 60%, tạo score 60. | Timing Decision điều chỉnh từ `NS/EW=20/50 giây` về `30/40 giây`. Controller accept plan nhưng chỉ giữ pending; phase `EW_GREEN` hiện tại không bị ngắt. |
| 11 | Đổi sang NS rất cao: 80 xe, queue 100, occupancy 80%, tạo score 90. EW ở mức trung bình với score 30. | Timing Decision điều chỉnh từ `30/40 giây` về `50/30 giây`. Plan mới nhất ghi đè plan `30/40 giây` đang pending, nhưng chưa được áp dụng ngay. |
| 12 | Đổi lần nữa sang NS cao với score 60 và EW nhẹ với score 20; giữ input này đến `ALL_RED` tiếp theo. | Timing Decision đạt `NS/EW=40/20 giây`. Đây là pending plan cuối cùng nên controller consume và apply tại `ALL_RED`; `NS_GREEN` tiếp theo bắt đầu với 40 giây. |

## Những điểm cần phân biệt khi tích hợp

- `TIMING_PLAN_PROCESSED result=accepted` có nghĩa message nhận qua MQ đã vượt
  qua kiểm tra transport, kiểm tra nghiệp vụ và được lưu vào
  `PlanSyncChannel`.
- Normal plan chưa làm thay đổi ngay chu kỳ đèn đang chạy. Plan chỉ được áp dụng
  khi xuất hiện `PLAN_APPLIED` tại cuối phase `ALL_RED`.
- Emergency chỉ được áp dụng ngay khi hướng emergency trùng với hướng đèn đang
  xanh và thời gian xanh còn lại thỏa mãn:

```text
5 giây < thời gian còn lại < 10 giây
```

## Các mốc output mong đợi

Plan ID phụ thuộc vào thứ tự xử lý thực tế khi chạy hệ thống.

```text
Phase: NS_GREEN, Remaining: ...
Phase: YELLOW, Remaining: 3 s
Phase: ALL_RED, Remaining: 1 s
event=PLAN_APPLIED, plan_id=...
Phase: EW_GREEN, Remaining: 50 s
...
event=EMERGENCY_APPLIED, phase=EW_GREEN, old_remaining_ms=..., new_remaining_ms=20000
Phase: EW_GREEN, Remaining: 20 s
...
Phase: ALL_RED, Remaining: 1 s
Phase: NS_GREEN, Remaining: 20 s
...
event=EMERGENCY_APPLIED, phase=NS_GREEN, old_remaining_ms=..., new_remaining_ms=20000
```
