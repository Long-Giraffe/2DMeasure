#pragma once

#include "core/CaliperTypes.h"

#include <opencv2/core.hpp>

namespace measure {

class CaliperDetector {
public:
    static constexpr double kProfileSampleSpacing = 0.25;

    CaliperResult detect(const cv::Mat& gray, const CaliperTool& tool) const;
    CaliperResult detectLine(const cv::Mat& gray, const CaliperTool& tool) const;
    CaliperResult detectTemplate(const cv::Mat& gray, const CaliperTool& tool) const;
    CaliperResult detectCircle(const cv::Mat& gray, const CaliperTool& tool) const;

private:
    std::vector<float> extractProfileAlongLine(
        const cv::Mat& gray,
        const cv::Point2d& p1,
        const cv::Point2d& p2,
        int width,
        double smoothSigma) const;

    std::vector<float> computeProfileGradient(
        const std::vector<float>& profile,
        double sigma,
        double sampleStep) const;

    std::vector<EdgePoint> findEdges(
        const std::vector<float>& gradient,
        const CaliperTool& tool,
        double sampleStep) const;

    int selectEdge(const std::vector<EdgePoint>& edges, EdgePickMode mode) const;
};

} // namespace measure
