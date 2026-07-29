#include "traffic_perception/core/runtime_paths.h"
#include <cstdlib>
#include <iostream>

namespace traffic_perception {

std::filesystem::path RuntimePaths::GetRoot() {
  const char* tper_root = std::getenv("TPER_ROOT");
  if (tper_root) {
    return std::filesystem::path(tper_root);
  }
  return "/tmp/traffic_perception";
}

std::filesystem::path RuntimePaths::Root() {
  return GetRoot();
}

std::filesystem::path RuntimePaths::Etc() {
  return GetRoot() / "etc";
}

std::filesystem::path RuntimePaths::Models() {
  return GetRoot() / "models";
}

std::filesystem::path RuntimePaths::Logs() {
  return GetRoot() / "logs";
}

std::filesystem::path RuntimePaths::Lib() {
  return GetRoot() / "lib";
}

}  // namespace traffic_perception
