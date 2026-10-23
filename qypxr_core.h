#pragma once
#include <string>
#include <cstddef>
namespace Qypxr {
    struct Result {
        bool success = false;
        std::string message, outputPath;
        size_t originalSize = 0, processedSize = 0;
        double timeSeconds = 0;
    };
    Result OptimizeVideo(const std::string&, int);
    Result OptimizeVideoSparse(const std::string&, int targetFps = 400);
}