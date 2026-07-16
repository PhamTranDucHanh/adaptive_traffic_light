#include "common/config.h"

#include "score/mw/log/logger.h"
#include "score/mw/log/logging.h"

int main()
{
    auto& logger =
        score::mw::log::CreateLogger(
            "APP1",
            "My application"
        );

    logger.LogInfo() << "Application started";
    logger.LogWarn() << "Something to watch: value=" << 42;

    return 0;
}