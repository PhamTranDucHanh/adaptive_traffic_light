#include "traffic_perception/core/runtime_paths.h"
#include <cstdlib>

namespace traffic_perception {

std::filesystem::path RuntimePaths::GetRoot() {
  const char* tper_root = std::getenv("TPER_ROOT");
  if (tper_root) {
    return std::filesystem::path(tper_root);
  }
  return "/tmp/traffic_perception";
}

std::filesystem::path RuntimePaths::Etc() {
  return GetRoot() / "etc";
}

std::filesystem::path RuntimePaths::Models() {
  return GetRoot() / "models";
}

}  // namespace traffic_perception
