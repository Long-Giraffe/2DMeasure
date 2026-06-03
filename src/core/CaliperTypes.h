#pragma once

#include <opencv2/core.hpp>

#include <string>
#include <vector>

namespace measure {

enum class EdgePolarity {
    Any,
    DarkToBright,
    BrightToDark,
};

enum class EdgePickMode {
    First,
    Last,
    Strongest,
};

struct EdgePoint {
    double position = 0.0;
    int polarity = 0;
    double gradient = 0.0;
    cv::Point2d point;
};

struct CaliperTool {
    std::string id;
    std::string name = "卡尺";
    bool enabled = true;
    cv::Point2d p1;
    cv::Point2d p2;
    int width = 31;
    double profileSmoothSigma = 0.4;
    double derivativeSigma = 0.4;
    double positiveThreshold = 8.0;
    double negativeThreshold = -8.0;
    EdgePolarity polarity = EdgePolarity::Any;
    EdgePickMode edgePickMode = EdgePickMode::Strongest;
};

struct Calibration {
    bool enabled = false;
    double mmPerPixel = 0.01;
};

struct EdgePairMeasurement {
    std::string id;
    std::string name = "边距";
    bool enabled = true;
    std::string caliperToolId;
    double edgeAPosition = 0.0;
    double edgeBPosition = 0.0;
};

struct Recipe {
    int version = 1;
    std::string imagePath;
    Calibration calibration;
    std::vector<CaliperTool> tools;
    std::vector<EdgePairMeasurement> measurements;
};

struct CaliperResult {
    bool ok = false;
    std::string message;
    std::vector<float> profile;
    std::vector<float> gradient;
    double profileStartPosition = 0.0;
    double profileSampleStep = 1.0;
    std::vector<EdgePoint> edges;
    int selectedIndex = -1;

    const EdgePoint* selectedEdge() const {
        if (selectedIndex < 0 || selectedIndex >= static_cast<int>(edges.size())) {
            return nullptr;
        }
        return &edges[static_cast<size_t>(selectedIndex)];
    }
};

std::string toString(EdgePolarity polarity);
std::string toString(EdgePickMode mode);
EdgePolarity edgePolarityFromString(const std::string& value);
EdgePickMode edgePickModeFromString(const std::string& value);

} // namespace measure
