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

enum class ToolType {
    LineCaliper,
    CircleCaliper,
    TemplateLocator,
};

enum class ImageChannel {
    Gray,
    Red,
    Green,
    Blue,
};

enum class CircleFitMethod {
    LeastSquares,
    Ransac,
};

struct EdgePoint {
    double position = 0.0;
    int polarity = 0;
    double gradient = 0.0;
    cv::Point2d point;
};

struct MeasureTool {
    std::string id;
    std::string name = "Caliper";
    bool enabled = true;
    ToolType type = ToolType::LineCaliper;
    ImageChannel channel = ImageChannel::Gray;

    // Line caliper geometry and shared edge parameters.
    cv::Point2d p1;
    cv::Point2d p2;
    int width = 31;
    double profileSmoothSigma = 0.4;
    double derivativeSigma = 0.4;
    double positiveThreshold = 8.0;
    double negativeThreshold = -8.0;
    EdgePolarity polarity = EdgePolarity::Any;
    EdgePickMode edgePickMode = EdgePickMode::Strongest;

    // Circle caliper geometry and fit parameters.
    cv::Point2d center;
    double innerRadius = 40.0;
    double outerRadius = 80.0;
    int sampleCount = 72;
    CircleFitMethod circleFitMethod = CircleFitMethod::LeastSquares;
    double ransacThreshold = 2.0;
    int ransacIterations = 100;

    // Template locator geometry and embedded template image.
    cv::Rect2d templateRoi;
    cv::Rect2d searchRoi;
    cv::Point2d templateReferencePoint;
    double templateScoreThreshold = 0.75;
    std::string templateImageBase64Png;
};

using CaliperTool = MeasureTool;

struct Calibration {
    bool enabled = false;
    double mmPerPixel = 0.01;
};

struct EdgePairMeasurement {
    std::string id;
    std::string name = "Edge distance";
    bool enabled = true;
    std::string toolAId;
    std::string toolBId;
    double edgeAPosition = 0.0;
    double edgeBPosition = 0.0;
};

struct Recipe {
    int version = 3;
    std::string imagePath;
    Calibration calibration;
    ImageChannel defaultChannel = ImageChannel::Gray;
    std::vector<MeasureTool> tools;
    std::vector<EdgePairMeasurement> measurements;
};

struct CaliperResult {
    bool ok = false;
    std::string message;
    ToolType type = ToolType::LineCaliper;
    std::vector<float> profile;
    std::vector<float> gradient;
    double profileStartPosition = 0.0;
    double profileSampleStep = 1.0;
    std::vector<EdgePoint> edges;
    int selectedIndex = -1;

    cv::Rect2d matchedRoi;
    cv::Point2d offset;
    double matchScore = 0.0;

    std::vector<EdgePoint> circleEdges;
    cv::Point2d fittedCenter;
    double fittedRadius = 0.0;
    double fittedDiameter = 0.0;
    double fitRmsError = 0.0;
    int fitPointCount = 0;
    int ransacInlierCount = 0;

    const EdgePoint* selectedEdge() const {
        if (selectedIndex < 0 || selectedIndex >= static_cast<int>(edges.size())) {
            return nullptr;
        }
        return &edges[static_cast<size_t>(selectedIndex)];
    }
};

std::string toString(ToolType type);
std::string toString(EdgePolarity polarity);
std::string toString(EdgePickMode mode);
std::string toString(ImageChannel channel);
std::string toString(CircleFitMethod method);
ToolType toolTypeFromString(const std::string& value);
EdgePolarity edgePolarityFromString(const std::string& value);
EdgePickMode edgePickModeFromString(const std::string& value);
ImageChannel imageChannelFromString(const std::string& value);
CircleFitMethod circleFitMethodFromString(const std::string& value);

} // namespace measure
