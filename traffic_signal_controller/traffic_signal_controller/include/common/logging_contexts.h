#ifndef TRAFFIC_SIGNAL_CONTROLLER_COMMON_LOGGING_CONTEXTS_H_
#define TRAFFIC_SIGNAL_CONTROLLER_COMMON_LOGGING_CONTEXTS_H_

namespace ctrl::logging {
inline constexpr char kCtxApp[] = "APP";
inline constexpr char kCtxHealth[] = "HLTH";
inline constexpr char kCtxPlan[] = "PLAN";
inline constexpr char kCtxSync[] = "SYNC";
inline constexpr char kCtxFsm[] = "FSM";
inline constexpr char kCtxOut[] = "OUT";
inline constexpr char kCtxDemo[] = "DEMO";
inline constexpr char kCtxAnalytics[] = "ANLY";
}  // namespace ctrl::logging

#endif  // TRAFFIC_SIGNAL_CONTROLLER_COMMON_LOGGING_CONTEXTS_H_
