#include "analytics_service/output_simulator.h"

namespace {
score::mw::log::Logger& logger =
    score::mw::log::CreateLogger(ctrl::logging::kCtxOut, "Output simulator");
}

void OutputSimulator::receiveSignalDisplay(const SignalDisplay& display) {
    logger.LogInfo() << "Received signal display";
    // TODO: lưu display vào state nội bộ
}

void OutputSimulator::formatConsole() {
    logger.LogDebug() << "Formatted console output";
}

void OutputSimulator::sendToParticipants() {
    // TODO: hàm hiện trả về void, không có tín hiệu success/fail rõ ràng.
    // Nếu có cách biết publish thất bại (exception, return code khác, errno...),
    // thêm nhánh: logger.LogError() << "Failed to publish GPIO";
    logger.LogInfo() << "Published signal output";
}