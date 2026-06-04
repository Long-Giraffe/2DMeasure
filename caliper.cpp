#include <opencv2/opencv.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <numeric>
#include <string>
#include <vector>

struct EdgePoint {
    double position = 0.0;   // Distance from p1 along the caliper axis.
    int polarity = 0;        // +1 dark-to-bright, -1 bright-to-dark along p1 -> p2.
    float gradient = 0.0f;   // Signed derivative peak value.
};

constexpr double kProfileSampleSpacing = 0.25;

static inline float sampleBilinear(const cv::Mat& img, double x, double y) {
    const int w = img.cols;
    const int h = img.rows;

    x = std::clamp(x, 0.0, static_cast<double>(w - 1));
    y = std::clamp(y, 0.0, static_cast<double>(h - 1));

    const int x0 = static_cast<int>(std::floor(x));
    const int y0 = static_cast<int>(std::floor(y));
    const int x1 = std::min(x0 + 1, w - 1);
    const int y1 = std::min(y0 + 1, h - 1);
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

static std::vector<float> makeGaussianKernel(double sigma) {
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

static std::vector<float> makeGaussianDerivativeKernel(double sigma) {
    if (sigma <= 0.0) {
        sigma = 1.0;
    }

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

static std::vector<float> convolve1DReplicate(
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

static double fitQuadraticSubpixel(const std::vector<float>& x, const std::vector<float>& y) {
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
    peak = std::clamp(peak, static_cast<double>(x.front()), static_cast<double>(x.back()));
    return peak;
}

std::vector<float> extractProfileAlongLine(
    const cv::Mat& gray,
    const cv::Point2f& p1,
    const cv::Point2f& p2,
    int width,
    double smoothSigma) {
    CV_Assert(!gray.empty() && gray.type() == CV_8U);

    const cv::Point2f axis = p2 - p1;
    const double lineLength = std::hypot(axis.x, axis.y);
    if (lineLength < 1e-6) {
        return {};
    }

    const cv::Point2f u = axis * static_cast<float>(1.0 / lineLength);
    const cv::Point2f perp(-u.y, u.x);

    const int acrossSamples = std::max(1, width);
    const int lineSamples = std::max(2, static_cast<int>(std::round(lineLength / kProfileSampleSpacing)) + 1);
    const double lineStep = lineLength / static_cast<double>(lineSamples - 1);

    std::vector<float> profile;
    profile.reserve(static_cast<size_t>(lineSamples));

    for (int i = 0; i < lineSamples; ++i) {
        const double t = static_cast<double>(i) * lineStep;
        const cv::Point2f center = p1 + u * static_cast<float>(t);
        double sum = 0.0;

        for (int j = 0; j < acrossSamples; ++j) {
            const double offset = static_cast<double>(j) - 0.5 * static_cast<double>(acrossSamples - 1);
            const cv::Point2f samplePt = center + perp * static_cast<float>(offset);
            sum += sampleBilinear(gray, samplePt.x, samplePt.y);
        }

        profile.push_back(static_cast<float>(sum / acrossSamples));
    }

    if (smoothSigma > 0.0) {
        profile = convolve1DReplicate(profile, makeGaussianKernel(smoothSigma / lineStep));
    }
    return profile;
}

std::vector<float> computeProfileGradient(
    const std::vector<float>& profile,
    double sigma = 1.0,
    double sampleStep = 1.0) {
    if (profile.empty()) {
        return {};
    }

    sampleStep = std::max(sampleStep, 1e-6);
    const double sigmaSamples = sigma / sampleStep;
    auto grad = convolve1DReplicate(profile, makeGaussianDerivativeKernel(sigmaSamples));
    for (float& value : grad) {
        value = static_cast<float>(value * sigmaSamples * std::sqrt(2.0 * CV_PI) / sampleStep);
    }
    return grad;
}

std::vector<EdgePoint> findAllEdgesPeak(
    const std::vector<float>& derivProfile,
    double threshold,
    double sampleStep = 1.0) {
    std::vector<EdgePoint> edges;
    const int n = static_cast<int>(derivProfile.size());
    int start = -1;

    auto flushSegment = [&](int end) {
        if (start < 0 || end < start) {
            return;
        }

        float peakValue = 0.0f;
        int peakIndex = start;
        for (int i = start; i <= end; ++i) {
            const float value = derivProfile[static_cast<size_t>(i)];
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
                y.push_back(derivProfile[static_cast<size_t>(i)]);
            }
        }

        const double subIndex = fitQuadraticSubpixel(x, y);
        const int polarity = peakValue > 0.0f ? +1 : -1;
        edges.push_back({subIndex * sampleStep, polarity, peakValue});
    };

    for (int i = 0; i < n; ++i) {
        if (std::abs(derivProfile[static_cast<size_t>(i)]) > threshold) {
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

std::vector<EdgePoint> detectEdgesAlongLine(
    const cv::Mat& gray,
    const cv::Point2f& p1,
    const cv::Point2f& p2,
    int width,
    double profileSmoothSigma,
    double derivativeSigma,
    double threshold) {
    const cv::Point2f axis = p2 - p1;
    const double lineLength = std::hypot(axis.x, axis.y);
    if (lineLength < 1e-6) {
        return {};
    }

    const cv::Point2f u = axis * static_cast<float>(1.0 / lineLength);
    const double padding = std::max(0.0, 3.0 * profileSmoothSigma + 3.0 * derivativeSigma + 2.0 * kProfileSampleSpacing);
    const cv::Point2f extP1 = p1 - u * static_cast<float>(padding);
    const cv::Point2f extP2 = p2 + u * static_cast<float>(padding);
    const double extLength = lineLength + 2.0 * padding;
    const int lineSamples = std::max(2, static_cast<int>(std::round(extLength / kProfileSampleSpacing)) + 1);
    const double sampleStep = extLength / static_cast<double>(lineSamples - 1);

    const auto profile = extractProfileAlongLine(gray, extP1, extP2, width, profileSmoothSigma);
    const auto gradient = computeProfileGradient(profile, derivativeSigma, sampleStep);
    auto edges = findAllEdgesPeak(gradient, threshold, sampleStep);

    std::vector<EdgePoint> clipped;
    clipped.reserve(edges.size());
    for (auto edge : edges) {
        edge.position -= padding;
        if (edge.position >= 0.0 && edge.position <= lineLength) {
            clipped.push_back(edge);
        }
    }
    return clipped;
}

static cv::Point2f pointAtDistance(const cv::Point2f& p1, const cv::Point2f& p2, double distance) {
    const cv::Point2f axis = p2 - p1;
    const double lineLength = std::hypot(axis.x, axis.y);
    if (lineLength < 1e-6) {
        return p1;
    }
    const cv::Point2f u = axis * static_cast<float>(1.0 / lineLength);
    return p1 + u * static_cast<float>(distance);
}

template<typename T>
void saveVector(const std::string& path, const std::vector<T>& values) {
    std::ofstream out(path);
    for (size_t i = 0; i < values.size(); ++i) {
        if (i > 0) {
            out << ',';
        }
        out << values[i];
    }
}

static cv::Mat makeSyntheticDemoImage(int width, int height) {
    cv::Mat img(height, width, CV_8U, cv::Scalar(40));
    cv::rectangle(img, cv::Rect(width / 3, height / 4, width / 3, height / 2), cv::Scalar(220), cv::FILLED);
    cv::GaussianBlur(img, img, cv::Size(0, 0), 1.2, 1.2, cv::BORDER_REPLICATE);
    return img;
}

int main(int argc, char** argv) {
    cv::Mat img;
    if (argc > 1) {
        img = cv::imread(argv[1], cv::IMREAD_GRAYSCALE);
        if (img.empty()) {
            std::cerr << "Cannot open image: " << argv[1] << '\n';
            return 1;
        }
    } else {
        img = makeSyntheticDemoImage(320, 180);
    }

    cv::Mat displayImg;
    cv::cvtColor(img, displayImg, cv::COLOR_GRAY2BGR);

    const cv::Point2f p1(0.0f, img.rows * 0.5f);
    const cv::Point2f p2(static_cast<float>(img.cols - 1), img.rows * 0.5f);
    const int width = std::max(1, img.rows / 4);
    const double profileSmoothSigma = 1.0;
    const double derivativeSigma = 1.0;
    const double derivativeThreshold = 8.0;

    const cv::Point2f axis = p2 - p1;
    const double lineLength = std::hypot(axis.x, axis.y);
    const cv::Point2f u = axis * static_cast<float>(1.0 / lineLength);
    const cv::Point2f perp(-u.y, u.x);
    const int lineSamples = std::max(2, static_cast<int>(std::round(lineLength / kProfileSampleSpacing)) + 1);
    const double sampleStep = lineLength / static_cast<double>(lineSamples - 1);

    const auto profile = extractProfileAlongLine(img, p1, p2, width, profileSmoothSigma);
    const auto gradient = computeProfileGradient(profile, derivativeSigma, sampleStep);
    const auto edges = detectEdgesAlongLine(
        img,
        p1,
        p2,
        width,
        profileSmoothSigma,
        derivativeSigma,
        derivativeThreshold);

    saveVector("grayProfile.txt", profile);
    saveVector("derivProfile.txt", gradient);

    cv::line(displayImg, p1, p2, cv::Scalar(255, 0, 0), 1, cv::LINE_AA);
    for (const auto& edge : edges) {
        const cv::Point2f linePoint = pointAtDistance(p1, p2, edge.position);
        const cv::Point2f start = linePoint + perp * static_cast<float>(0.5 * width);
        const cv::Point2f end = linePoint - perp * static_cast<float>(0.5 * width);
        cv::line(displayImg, start, end, cv::Scalar(0, 0, 255), 1, cv::LINE_AA);
        std::cout << "edge position=" << edge.position
                  << " polarity=" << edge.polarity
                  << " gradient=" << edge.gradient << '\n';
    }

    cv::imshow("Detected Edges", displayImg);
    cv::waitKey(0);
    return 0;
}
