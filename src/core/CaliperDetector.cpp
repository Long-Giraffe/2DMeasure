#include "core/CaliperDetector.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace measure {
namespace {

float sampleBilinear(const cv::Mat& img, double x, double y) {
    x = std::clamp(x, 0.0, static_cast<double>(img.cols - 1));
    y = std::clamp(y, 0.0, static_cast<double>(img.rows - 1));

    const int x0 = static_cast<int>(std::floor(x));
    const int y0 = static_cast<int>(std::floor(y));
    const int x1 = std::min(x0 + 1, img.cols - 1);
    const int y1 = std::min(y0 + 1, img.rows - 1);
    const double dx = x - x0;
    const double dy = y - y0;

    const float i00 = img.at<uchar>(y0, x0);
    const float i10 = img.at<uchar>(y0, x1);
    const float i01 = img.at<uchar>(y1, x0);
    const float i11 = img.at<uchar>(y1, x1);

    const double value =
        (1.0 - dx) * (1.0 - dy) * i00 +
        dx * (1.0 - dy) * i10 +
        (1.0 - dx) * dy * i01 +
        dx * dy * i11;
    return static_cast<float>(value);
}

std::vector<float> makeGaussianKernel(double sigma) {
    if (sigma <= 0.0) {
        return {1.0f};
    }

    const int radius = std::max(1, static_cast<int>(std::ceil(3.0 * sigma)));
    std::vector<float> kernel(static_cast<size_t>(2 * radius + 1), 0.0f);
    double sum = 0.0;

    for (int i = -radius; i <= radius; ++i) {
        const double value = std::exp(-0.5 * (static_cast<double>(i) * i) / (sigma * sigma));
        kernel[static_cast<size_t>(i + radius)] = static_cast<float>(value);
        sum += value;
    }

    for (float& value : kernel) {
        value = static_cast<float>(value / sum);
    }
    return kernel;
}

std::vector<float> makeGaussianDerivativeKernel(double sigma) {
    sigma = std::max(sigma, 1e-6);
    const int radius = std::max(1, static_cast<int>(std::ceil(3.0 * sigma)));
    std::vector<float> kernel(static_cast<size_t>(2 * radius + 1), 0.0f);
    const double sigma2 = sigma * sigma;
    double norm = 0.0;

    for (int i = -radius; i <= radius; ++i) {
        const double value = -(static_cast<double>(i) / sigma2) *
                             std::exp(-0.5 * (static_cast<double>(i) * i) / sigma2);
        kernel[static_cast<size_t>(i + radius)] = static_cast<float>(value);
        norm += std::abs(value);
    }

    if (norm > 0.0) {
        for (float& value : kernel) {
            value = static_cast<float>(value / norm);
        }
    }
    return kernel;
}

std::vector<float> convolve1DReplicate(
    const std::vector<float>& signal,
    const std::vector<float>& kernel) {
    if (signal.empty() || kernel.empty()) {
        return {};
    }

    const int half = static_cast<int>(kernel.size() / 2);
    std::vector<float> result(signal.size(), 0.0f);

    for (size_t i = 0; i < signal.size(); ++i) {
        double sum = 0.0;
        for (int k = -half; k <= half; ++k) {
            int idx = static_cast<int>(i) + k;
            idx = std::clamp(idx, 0, static_cast<int>(signal.size()) - 1);
            sum += signal[static_cast<size_t>(idx)] * kernel[static_cast<size_t>(half - k)];
        }
        result[i] = static_cast<float>(sum);
    }

    return result;
}

double fitQuadraticSubpixel(const std::vector<float>& x, const std::vector<float>& y) {
    const int n = static_cast<int>(x.size());
    if (n < 3) {
        return n == 0 ? 0.0 : x[static_cast<size_t>(n / 2)];
    }

    const double xMean = std::accumulate(x.begin(), x.end(), 0.0) / n;
    double sumX = 0.0;
    double sumX2 = 0.0;
    double sumX3 = 0.0;
    double sumX4 = 0.0;
    double sumY = 0.0;
    double sumXY = 0.0;
    double sumX2Y = 0.0;

    for (int i = 0; i < n; ++i) {
        const double xi = x[static_cast<size_t>(i)] - xMean;
        const double yi = y[static_cast<size_t>(i)];
        const double xi2 = xi * xi;
        sumX += xi;
        sumX2 += xi2;
        sumX3 += xi2 * xi;
        sumX4 += xi2 * xi2;
        sumY += yi;
        sumXY += xi * yi;
        sumX2Y += xi2 * yi;
    }

    cv::Mat a = (cv::Mat_<double>(3, 3) <<
        sumX4, sumX3, sumX2,
        sumX3, sumX2, sumX,
        sumX2, sumX, static_cast<double>(n));
    cv::Mat b = (cv::Mat_<double>(3, 1) << sumX2Y, sumXY, sumY);
    cv::Mat coeff;

    if (!cv::solve(a, b, coeff, cv::DECOMP_SVD)) {
        return x[static_cast<size_t>(n / 2)];
    }

    const double qa = coeff.at<double>(0);
    const double qb = coeff.at<double>(1);
    if (std::abs(qa) < 1e-8) {
        return x[static_cast<size_t>(n / 2)];
    }

    double peak = -qb / (2.0 * qa) + xMean;
    return std::clamp(peak, static_cast<double>(x.front()), static_cast<double>(x.back()));
}

bool passesThreshold(double value, const CaliperTool& tool) {
    switch (tool.polarity) {
    case EdgePolarity::DarkToBright:
        return value >= tool.positiveThreshold;
    case EdgePolarity::BrightToDark:
        return value <= tool.negativeThreshold;
    case EdgePolarity::Any:
    default:
        return value >= tool.positiveThreshold || value <= tool.negativeThreshold;
    }
}

} // namespace

std::string toString(EdgePolarity polarity) {
    switch (polarity) {
    case EdgePolarity::DarkToBright:
        return "dark_to_bright";
    case EdgePolarity::BrightToDark:
        return "bright_to_dark";
    case EdgePolarity::Any:
    default:
        return "any";
    }
}

std::string toString(EdgePickMode mode) {
    switch (mode) {
    case EdgePickMode::First:
        return "first";
    case EdgePickMode::Last:
        return "last";
    case EdgePickMode::Strongest:
    default:
        return "strongest";
    }
}

EdgePolarity edgePolarityFromString(const std::string& value) {
    if (value == "dark_to_bright") {
        return EdgePolarity::DarkToBright;
    }
    if (value == "bright_to_dark") {
        return EdgePolarity::BrightToDark;
    }
    return EdgePolarity::Any;
}

EdgePickMode edgePickModeFromString(const std::string& value) {
    if (value == "first") {
        return EdgePickMode::First;
    }
    if (value == "last") {
        return EdgePickMode::Last;
    }
    return EdgePickMode::Strongest;
}

CaliperResult CaliperDetector::detect(const cv::Mat& gray, const CaliperTool& tool) const {
    CaliperResult result;
    if (!tool.enabled) {
        result.message = "工具未启用";
        return result;
    }
    if (gray.empty() || gray.type() != CV_8U) {
        result.message = "图像为空或不是 8 位灰度图";
        return result;
    }

    const cv::Point2d axis = tool.p2 - tool.p1;
    const double lineLength = std::hypot(axis.x, axis.y);
    if (lineLength < 1e-6) {
        result.message = "卡尺长度过短";
        return result;
    }

    const cv::Point2d u = axis * (1.0 / lineLength);
    const double padding = std::max(0.0, 3.0 * tool.profileSmoothSigma +
                                            3.0 * tool.derivativeSigma +
                                            2.0 * kProfileSampleSpacing);
    const cv::Point2d extP1 = tool.p1 - u * padding;
    const cv::Point2d extP2 = tool.p2 + u * padding;
    const double extLength = lineLength + 2.0 * padding;
    const int lineSamples = std::max(2, static_cast<int>(std::round(extLength / kProfileSampleSpacing)) + 1);
    const double sampleStep = extLength / static_cast<double>(lineSamples - 1);

    result.profileStartPosition = -padding;
    result.profileSampleStep = sampleStep;
    result.profile = extractProfileAlongLine(gray, extP1, extP2, tool.width, tool.profileSmoothSigma);
    result.gradient = computeProfileGradient(result.profile, tool.derivativeSigma, sampleStep);
    auto rawEdges = findEdges(result.gradient, tool, sampleStep);

    for (auto edge : rawEdges) {
        edge.position -= padding;
        if (edge.position < 0.0 || edge.position > lineLength) {
            continue;
        }
        edge.point = tool.p1 + u * edge.position;
        result.edges.push_back(edge);
    }

    result.selectedIndex = selectEdge(result.edges, tool.edgePickMode);
    result.ok = result.selectedIndex >= 0;
    result.message = result.ok ? "OK" : "未找到符合条件的边缘";
    return result;
}

std::vector<float> CaliperDetector::extractProfileAlongLine(
    const cv::Mat& gray,
    const cv::Point2d& p1,
    const cv::Point2d& p2,
    int width,
    double smoothSigma) const {
    const cv::Point2d axis = p2 - p1;
    const double lineLength = std::hypot(axis.x, axis.y);
    if (lineLength < 1e-6) {
        return {};
    }

    const cv::Point2d u = axis * (1.0 / lineLength);
    const cv::Point2d perp(-u.y, u.x);
    const int acrossSamples = std::max(1, width);
    const int lineSamples = std::max(2, static_cast<int>(std::round(lineLength / kProfileSampleSpacing)) + 1);
    const double lineStep = lineLength / static_cast<double>(lineSamples - 1);

    std::vector<float> profile;
    profile.reserve(static_cast<size_t>(lineSamples));

    for (int i = 0; i < lineSamples; ++i) {
        const double t = static_cast<double>(i) * lineStep;
        const cv::Point2d center = p1 + u * t;
        double sum = 0.0;
        for (int j = 0; j < acrossSamples; ++j) {
            const double offset = static_cast<double>(j) - 0.5 * static_cast<double>(acrossSamples - 1);
            const cv::Point2d samplePt = center + perp * offset;
            sum += sampleBilinear(gray, samplePt.x, samplePt.y);
        }
        profile.push_back(static_cast<float>(sum / acrossSamples));
    }

    if (smoothSigma > 0.0) {
        profile = convolve1DReplicate(profile, makeGaussianKernel(smoothSigma / lineStep));
    }
    return profile;
}

std::vector<float> CaliperDetector::computeProfileGradient(
    const std::vector<float>& profile,
    double sigma,
    double sampleStep) const {
    if (profile.empty()) {
        return {};
    }

    sampleStep = std::max(sampleStep, 1e-6);
    const double sigmaSamples = sigma / sampleStep;
    auto gradient = convolve1DReplicate(profile, makeGaussianDerivativeKernel(sigmaSamples));
    for (float& value : gradient) {
        value = static_cast<float>(value * sigmaSamples * std::sqrt(2.0 * CV_PI) / sampleStep);
    }
    return gradient;
}

std::vector<EdgePoint> CaliperDetector::findEdges(
    const std::vector<float>& gradient,
    const CaliperTool& tool,
    double sampleStep) const {
    std::vector<EdgePoint> edges;
    const int n = static_cast<int>(gradient.size());
    int start = -1;

    auto flushSegment = [&](int end) {
        if (start < 0 || end < start) {
            return;
        }

        double peakValue = 0.0;
        int peakIndex = start;
        for (int i = start; i <= end; ++i) {
            const double value = gradient[static_cast<size_t>(i)];
            if (std::abs(value) > std::abs(peakValue)) {
                peakValue = value;
                peakIndex = i;
            }
        }

        std::vector<float> x;
        std::vector<float> y;
        for (int i = peakIndex - 2; i <= peakIndex + 2; ++i) {
            if (i >= 0 && i < n) {
                x.push_back(static_cast<float>(i));
                y.push_back(gradient[static_cast<size_t>(i)]);
            }
        }

        EdgePoint edge;
        edge.position = fitQuadraticSubpixel(x, y) * sampleStep;
        edge.polarity = peakValue > 0.0 ? +1 : -1;
        edge.gradient = peakValue;
        edges.push_back(edge);
    };

    for (int i = 0; i < n; ++i) {
        const double value = gradient[static_cast<size_t>(i)];
        if (passesThreshold(value, tool)) {
            if (start < 0) {
                start = i;
            }
        } else if (start >= 0) {
            flushSegment(i - 1);
            start = -1;
        }
    }

    if (start >= 0) {
        flushSegment(n - 1);
    }
    return edges;
}

int CaliperDetector::selectEdge(const std::vector<EdgePoint>& edges, EdgePickMode mode) const {
    if (edges.empty()) {
        return -1;
    }

    int selected = 0;
    switch (mode) {
    case EdgePickMode::First:
        for (int i = 1; i < static_cast<int>(edges.size()); ++i) {
            if (edges[static_cast<size_t>(i)].position < edges[static_cast<size_t>(selected)].position) {
                selected = i;
            }
        }
        break;
    case EdgePickMode::Last:
        for (int i = 1; i < static_cast<int>(edges.size()); ++i) {
            if (edges[static_cast<size_t>(i)].position > edges[static_cast<size_t>(selected)].position) {
                selected = i;
            }
        }
        break;
    case EdgePickMode::Strongest:
    default:
        for (int i = 1; i < static_cast<int>(edges.size()); ++i) {
            if (std::abs(edges[static_cast<size_t>(i)].gradient) >
                std::abs(edges[static_cast<size_t>(selected)].gradient)) {
                selected = i;
            }
        }
        break;
    }
    return selected;
}

} // namespace measure
