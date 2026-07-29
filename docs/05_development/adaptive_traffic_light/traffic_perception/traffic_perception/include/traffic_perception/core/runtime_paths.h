#ifndef TRAFFIC_PERCEPTION_CORE_RUNTIME_PATHS_H_
#define TRAFFIC_PERCEPTION_CORE_RUNTIME_PATHS_H_

#include <filesystem>
#include <string>

namespace traffic_perception {

class RuntimePaths {
 public:
  static std::filesystem::path Root();
  static std::filesystem::path Etc();
  static std::filesystem::path Models();
  static std::filesystem::path Logs();
  static std::filesystem::path Lib();

 private:
  static std::filesystem::path GetRoot();
};

}  // namespace traffic_perception

#endif  // TRAFFIC_PERCEPTION_CORE_RUNTIME_PATHS_H_
