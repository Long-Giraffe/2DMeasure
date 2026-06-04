#pragma once

#include "core/CaliperDetector.h"
#include "core/CaliperTypes.h"

#include <opencv2/core.hpp>

#include <string>
#include <vector>

namespace measure {

struct MeasurementResult {
    bool ok = false;
    std::string message;
    cv::Point2d pointA;
    cv::Point2d pointB;
    double distancePx = 0.0;
};

struct RecipeRunResult {
    bool ok = false;
    std::string message;
    cv::Mat measurementImage;
    std::vector<MeasureTool> runtimeTools;
    std::vector<CaliperResult> toolResults;
    std::vector<MeasurementResult> measurementResults;
    bool hasLocator = false;
    bool locatorOk = false;
    cv::Point2d locatorOffset;
};

class RecipeRunner {
public:
    RecipeRunResult run(const Recipe& recipe, const cv::Mat& colorBgr) const;
    static cv::Mat selectChannel(const cv::Mat& colorBgr, ImageChannel channel);

private:
    CaliperDetector detector_;
};

} // namespace measure
