#include "core/CaliperDetector.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <numeric>
#include <random>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

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

bool fitCircleLeastSquares(const std::vector<cv::Point2d>& points, cv::Point2d* center, double* radius) {
    if (points.size() < 3) {
        return false;
    }

    cv::Mat a(static_cast<int>(points.size()), 3, CV_64F);
    cv::Mat b(static_cast<int>(points.size()), 1, CV_64F);
    for (int i = 0; i < static_cast<int>(points.size()); ++i) {
        const auto& p = points[static_cast<size_t>(i)];
        a.at<double>(i, 0) = p.x;
        a.at<double>(i, 1) = p.y;
        a.at<double>(i, 2) = 1.0;
        b.at<double>(i, 0) = -(p.x * p.x + p.y * p.y);
    }

    cv::Mat coeff;
    if (!cv::solve(a, b, coeff, cv::DECOMP_SVD)) {
        return false;
    }

    const double d = coeff.at<double>(0);
    const double e = coeff.at<double>(1);
    const double f = coeff.at<double>(2);
    const cv::Point2d c(-0.5 * d, -0.5 * e);
    const double r2 = c.x * c.x + c.y * c.y - f;
    if (r2 <= 0.0 || !std::isfinite(r2)) {
        return false;
    }

    *center = c;
    *radius = std::sqrt(r2);
    return true;
}

bool fitCircleFromThreePoints(
    const cv::Point2d& a,
    const cv::Point2d& b,
    const cv::Point2d& c,
    cv::Point2d* center,
    double* radius) {
    const double d = 2.0 * (a.x * (b.y - c.y) + b.x * (c.y - a.y) + c.x * (a.y - b.y));
    if (std::abs(d) < 1e-9) {
        return false;
    }

    const double a2 = a.x * a.x + a.y * a.y;
    const double b2 = b.x * b.x + b.y * b.y;
    const double c2 = c.x * c.x + c.y * c.y;
    center->x = (a2 * (b.y - c.y) + b2 * (c.y - a.y) + c2 * (a.y - b.y)) / d;
    center->y = (a2 * (c.x - b.x) + b2 * (a.x - c.x) + c2 * (b.x - a.x)) / d;
    *radius = std::hypot(a.x - center->x, a.y - center->y);
    return std::isfinite(*radius) && *radius > 0.0;
}

double circleRmsError(const std::vector<cv::Point2d>& points, const cv::Point2d& center, double radius) {
    if (points.empty()) {
        return 0.0;
    }
    double sum = 0.0;
    for (const auto& p : points) {
        const double residual = std::hypot(p.x - center.x, p.y - center.y) - radius;
        sum += residual * residual;
    }
    return std::sqrt(sum / static_cast<double>(points.size()));
}

std::vector<cv::Point2d> ransacCircleInliers(
    const std::vector<cv::Point2d>& points,
    double threshold,
    int iterations,
    cv::Point2d* bestCenter,
    double* bestRadius) {
    std::vector<cv::Point2d> bestInliers;
    if (points.size() < 3) {
        return bestInliers;
    }

    std::mt19937 rng(12345);
    std::uniform_int_distribution<int> dist(0, static_cast<int>(points.size()) - 1);
    iterations = std::max(1, iterations);
    threshold = std::max(0.01, threshold);

    for (int iter = 0; iter < iterations; ++iter) {
        int i0 = dist(rng);
        int i1 = dist(rng);
        int i2 = dist(rng);
        if (i0 == i1 || i0 == i2 || i1 == i2) {
            continue;
        }

        cv::Point2d center;
        double radius = 0.0;
        if (!fitCircleFromThreePoints(
                points[static_cast<size_t>(i0)],
                points[static_cast<size_t>(i1)],
                points[static_cast<size_t>(i2)],
                &center,
                &radius)) {
            continue;
        }

        std::vector<cv::Point2d> inliers;
        for (const auto& p : points) {
            const double residual = std::abs(std::hypot(p.x - center.x, p.y - center.y) - radius);
            if (residual <= threshold) {
                inliers.push_back(p);
            }
        }

        if (inliers.size() > bestInliers.size()) {
            bestInliers = inliers;
            *bestCenter = center;
            *bestRadius = radius;
        }
    }
    return bestInliers;
}

} // namespace

std::string toString(ToolType type) {
    switch (type) {
    case ToolType::CircleCaliper:
        return "circle_caliper";
    case ToolType::TemplateLocator:
        return "template_locator";
    case ToolType::LineCaliper:
    default:
        return "line_caliper";
    }
}

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

std::string toString(ImageChannel channel) {
    switch (channel) {
    case ImageChannel::Red:
        return "red";
    case ImageChannel::Green:
        return "green";
    case ImageChannel::Blue:
        return "blue";
    case ImageChannel::Gray:
    default:
        return "gray";
    }
}

std::string toString(CircleFitMethod method) {
    switch (method) {
    case CircleFitMethod::Ransac:
        return "ransac";
    case CircleFitMethod::LeastSquares:
    default:
        return "least_squares";
    }
}

ToolType toolTypeFromString(const std::string& value) {
    if (value == "circle_caliper") {
        return ToolType::CircleCaliper;
    }
    if (value == "template_locator") {
        return ToolType::TemplateLocator;
    }
    return ToolType::LineCaliper;
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

ImageChannel imageChannelFromString(const std::string& value) {
    if (value == "red") {
        return ImageChannel::Red;
    }
    if (value == "green") {
        return ImageChannel::Green;
    }
    if (value == "blue") {
        return ImageChannel::Blue;
    }
    return ImageChannel::Gray;
}

CircleFitMethod circleFitMethodFromString(const std::string& value) {
    if (value == "ransac") {
        return CircleFitMethod::Ransac;
    }
    return CircleFitMethod::LeastSquares;
}

CaliperResult CaliperDetector::detect(const cv::Mat& gray, const CaliperTool& tool) const {
    switch (tool.type) {
    case ToolType::TemplateLocator:
        return detectTemplate(gray, tool);
    case ToolType::CircleCaliper:
        return detectCircle(gray, tool);
    case ToolType::LineCaliper:
    default:
        return detectLine(gray, tool);
    }
}

CaliperResult CaliperDetector::detectLine(const cv::Mat& gray, const CaliperTool& tool) const {
    CaliperResult result;
    result.type = ToolType::LineCaliper;
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

CaliperResult CaliperDetector::detectTemplate(const cv::Mat& gray, const CaliperTool& tool) const {
    CaliperResult result;
    result.type = ToolType::TemplateLocator;
    if (!tool.enabled) {
        result.message = "Disabled";
        return result;
    }
    if (gray.empty() || gray.type() != CV_8U) {
        result.message = "Image is empty or not 8-bit grayscale";
        return result;
    }
    if (tool.templateImageBase64Png.empty()) {
        result.message = "Template image is empty";
        return result;
    }

    const std::string table =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::vector<uchar> pngBytes;
    int val = 0;
    int valb = -8;
    for (unsigned char c : tool.templateImageBase64Png) {
        if (std::isspace(c) || c == '=') {
            continue;
        }
        const size_t idx = table.find(static_cast<char>(c));
        if (idx == std::string::npos) {
            result.message = "Template base64 is invalid";
            return result;
        }
        val = (val << 6) + static_cast<int>(idx);
        valb += 6;
        if (valb >= 0) {
            pngBytes.push_back(static_cast<uchar>((val >> valb) & 0xFF));
            valb -= 8;
        }
    }

    cv::Mat templ = cv::imdecode(pngBytes, cv::IMREAD_GRAYSCALE);
    if (templ.empty()) {
        result.message = "Template decode failed";
        return result;
    }
    if (templ.cols > gray.cols || templ.rows > gray.rows) {
        result.message = "Template is larger than image";
        return result;
    }

    cv::Rect searchRect(
        static_cast<int>(std::floor(tool.searchRoi.x)),
        static_cast<int>(std::floor(tool.searchRoi.y)),
        static_cast<int>(std::ceil(tool.searchRoi.width)),
        static_cast<int>(std::ceil(tool.searchRoi.height)));
    searchRect &= cv::Rect(0, 0, gray.cols, gray.rows);
    if (searchRect.width < templ.cols || searchRect.height < templ.rows) {
        searchRect = cv::Rect(0, 0, gray.cols, gray.rows);
    }

    cv::Mat response;
    cv::matchTemplate(gray(searchRect), templ, response, cv::TM_CCOEFF_NORMED);
    double minValue = 0.0;
    double maxValue = 0.0;
    cv::Point minLoc;
    cv::Point maxLoc;
    cv::minMaxLoc(response, &minValue, &maxValue, &minLoc, &maxLoc);

    result.matchScore = maxValue;
    result.ok = maxValue >= tool.templateScoreThreshold;
    const cv::Point matchedTopLeft(maxLoc.x + searchRect.x, maxLoc.y + searchRect.y);
    result.matchedRoi = cv::Rect2d(matchedTopLeft.x, matchedTopLeft.y, templ.cols, templ.rows);
    result.offset = cv::Point2d(matchedTopLeft.x - tool.templateRoi.x, matchedTopLeft.y - tool.templateRoi.y);
    result.message = result.ok ? "OK" : "Template score below threshold";
    return result;
}

CaliperResult CaliperDetector::detectCircle(const cv::Mat& gray, const CaliperTool& tool) const {
    CaliperResult result;
    result.type = ToolType::CircleCaliper;
    if (!tool.enabled) {
        result.message = "Disabled";
        return result;
    }
    if (gray.empty() || gray.type() != CV_8U) {
        result.message = "Image is empty or not 8-bit grayscale";
        return result;
    }
    if (tool.outerRadius <= tool.innerRadius || tool.innerRadius < 0.0) {
        result.message = "Circle radii are invalid";
        return result;
    }

    const int samples = std::max(8, tool.sampleCount);
    std::vector<cv::Point2d> fitPoints;
    fitPoints.reserve(static_cast<size_t>(samples));
    for (int i = 0; i < samples; ++i) {
        const double angle = 2.0 * CV_PI * static_cast<double>(i) / static_cast<double>(samples);
        const cv::Point2d dir(std::cos(angle), std::sin(angle));

        CaliperTool radial = tool;
        radial.type = ToolType::LineCaliper;
        radial.p1 = tool.center + dir * tool.innerRadius;
        radial.p2 = tool.center + dir * tool.outerRadius;
        const CaliperResult line = detectLine(gray, radial);
        if (const EdgePoint* edge = line.selectedEdge()) {
            result.circleEdges.push_back(*edge);
            fitPoints.push_back(edge->point);
        }
    }

    result.fitPointCount = static_cast<int>(fitPoints.size());
    if (fitPoints.size() < 3) {
        result.message = "Not enough circle edge points";
        return result;
    }

    std::vector<cv::Point2d> pointsForFit = fitPoints;
    if (tool.circleFitMethod == CircleFitMethod::Ransac) {
        cv::Point2d ransacCenter;
        double ransacRadius = 0.0;
        auto inliers = ransacCircleInliers(
            fitPoints,
            tool.ransacThreshold,
            tool.ransacIterations,
            &ransacCenter,
            &ransacRadius);
        result.ransacInlierCount = static_cast<int>(inliers.size());
        if (inliers.size() >= 3) {
            pointsForFit = inliers;
        }
    }

    cv::Point2d center;
    double radius = 0.0;
    if (!fitCircleLeastSquares(pointsForFit, &center, &radius)) {
        result.message = "Circle fit failed";
        return result;
    }

    result.ok = true;
    result.fittedCenter = center;
    result.fittedRadius = radius;
    result.fittedDiameter = radius * 2.0;
    result.fitRmsError = circleRmsError(pointsForFit, center, radius);
    result.message = "OK";
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
