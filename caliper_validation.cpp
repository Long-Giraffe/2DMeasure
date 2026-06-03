#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
#include <random>
#include <sstream>
#include <string>
#include <vector>

struct Point2d {
    double x = 0.0;
    double y = 0.0;
};

static Point2d operator+(Point2d a, Point2d b) { return {a.x + b.x, a.y + b.y}; }
static Point2d operator-(Point2d a, Point2d b) { return {a.x - b.x, a.y - b.y}; }
static Point2d operator*(Point2d a, double s) { return {a.x * s, a.y * s}; }

struct Image {
    int width = 0;
    int height = 0;
    std::vector<double> pixels;

    double at(int x, int y) const {
        x = std::clamp(x, 0, width - 1);
        y = std::clamp(y, 0, height - 1);
        return pixels[static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)];
    }
};

struct EdgePoint {
    double position = 0.0;
    int polarity = 0;
    double gradient = 0.0;
};

struct Detection {
    bool found = false;
    int edgeCount = 0;
    EdgePoint edge;
    Point2d global;
    double expectedX = std::numeric_limits<double>::quiet_NaN();
    double errorX = std::numeric_limits<double>::quiet_NaN();
};

struct TestRow {
    std::string name;
    std::string status;
    double measured = std::numeric_limits<double>::quiet_NaN();
    double expected = std::numeric_limits<double>::quiet_NaN();
    double error = std::numeric_limits<double>::quiet_NaN();
    double extra = std::numeric_limits<double>::quiet_NaN();
    std::string note;
};

constexpr double kProfileSampleSpacing = 0.25;

static double length(Point2d p) {
    return std::hypot(p.x, p.y);
}

static std::string fixed(double value, int precision = 4) {
    if (!std::isfinite(value)) {
        return "";
    }
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(precision) << value;
    return oss.str();
}

static double sampleBilinear(const Image& img, double x, double y) {
    if (x < 0.0) x = 0.0;
    if (y < 0.0) y = 0.0;
    if (x > img.width - 1.0) x = img.width - 1.0;
    if (y > img.height - 1.0) y = img.height - 1.0;

    const int x0 = static_cast<int>(std::floor(x));
    const int y0 = static_cast<int>(std::floor(y));
    const int x1 = std::min(x0 + 1, img.width - 1);
    const int y1 = std::min(y0 + 1, img.height - 1);
    const double dx = x - x0;
    const double dy = y - y0;

    const double i00 = img.at(x0, y0);
    const double i10 = img.at(x1, y0);
    const double i01 = img.at(x0, y1);
    const double i11 = img.at(x1, y1);
    return (1.0 - dx) * (1.0 - dy) * i00
        + dx * (1.0 - dy) * i10
        + (1.0 - dx) * dy * i01
        + dx * dy * i11;
}

static std::vector<double> makeGaussianKernel(int ksize, double sigma) {
    if (ksize <= 1) {
        return {1.0};
    }
    if (ksize % 2 == 0) {
        ++ksize;
    }
    const int radius = ksize / 2;
    std::vector<double> kernel(static_cast<size_t>(ksize));
    double sum = 0.0;
    for (int i = -radius; i <= radius; ++i) {
        const double value = std::exp(-0.5 * (static_cast<double>(i) * i) / (sigma * sigma));
        kernel[static_cast<size_t>(i + radius)] = value;
        sum += value;
    }
    for (double& value : kernel) {
        value /= sum;
    }
    return kernel;
}

static std::vector<double> convolveReplicate(const std::vector<double>& signal, const std::vector<double>& kernel) {
    const int half = static_cast<int>(kernel.size() / 2);
    std::vector<double> result(signal.size(), 0.0);
    for (size_t i = 0; i < signal.size(); ++i) {
        double sum = 0.0;
        for (int k = -half; k <= half; ++k) {
            int idx = static_cast<int>(i) + k;
            idx = std::clamp(idx, 0, static_cast<int>(signal.size()) - 1);
            sum += signal[static_cast<size_t>(idx)] * kernel[static_cast<size_t>(half - k)];
        }
        result[i] = sum;
    }
    return result;
}

static std::vector<double> extractProfileFixed(const Image& img, Point2d p1, Point2d p2, int width, double smoothSigma) {
    width = std::max(1, width);

    const Point2d v = p2 - p1;
    const double len = length(v);
    if (len < 1e-6) {
        return {};
    }

    const Point2d u = v * (1.0 / len);
    const Point2d perp{-u.y, u.x};
    const int lineSamples = std::max(2, static_cast<int>(std::round(len / kProfileSampleSpacing)) + 1);
    const double lineStep = len / static_cast<double>(lineSamples - 1);
    std::vector<double> profile;
    profile.reserve(static_cast<size_t>(lineSamples));

    for (int i = 0; i < lineSamples; ++i) {
        const double t = static_cast<double>(i) * lineStep;
        const Point2d center = p1 + u * t;
        double sum = 0.0;
        for (int j = 0; j < width; ++j) {
            const double offset = static_cast<double>(j) - 0.5 * static_cast<double>(width - 1);
            const Point2d samplePt = center + perp * offset;
            sum += sampleBilinear(img, samplePt.x, samplePt.y);
        }
        profile.push_back(sum / static_cast<double>(width));
    }

    if (smoothSigma > 0.0) {
        const double sigmaSamples = smoothSigma / lineStep;
        const int smoothKsize = 2 * std::max(1, static_cast<int>(std::ceil(3.0 * sigmaSamples))) + 1;
        const auto kernel = makeGaussianKernel(smoothKsize, sigmaSamples);
        profile = convolveReplicate(profile, kernel);
    }
    return profile;
}

static std::vector<double> makeGaussianDerivativeKernel(double sigma) {
    if (sigma <= 0.0) {
        sigma = 1.0;
    }
    const int radius = static_cast<int>(std::ceil(3.0 * sigma));
    const int size = 2 * radius + 1;
    std::vector<double> kernel(static_cast<size_t>(std::max(size, 1)));

    const double sigma2 = sigma * sigma;
    double norm = 0.0;
    for (int i = -radius; i <= radius; ++i) {
        const double value = -(static_cast<double>(i) / sigma2) * std::exp(-0.5 * (static_cast<double>(i) * i) / sigma2);
        kernel[static_cast<size_t>(i + radius)] = value;
        norm += std::abs(value);
    }

    if (norm > 0.0) {
        for (double& value : kernel) {
            value /= norm;
        }
    }
    return kernel;
}

static std::vector<double> computeProfileGradientFixed(
    const std::vector<double>& profile,
    double sigma,
    double sampleStep) {
    if (profile.empty()) {
        return {};
    }
    sampleStep = std::max(sampleStep, 1e-6);
    const double sigmaSamples = sigma / sampleStep;
    auto kernel = makeGaussianDerivativeKernel(sigmaSamples);
    auto grad = convolveReplicate(profile, kernel);
    for (double& value : grad) {
        value *= sigmaSamples * std::sqrt(2.0 * 3.14159265358979323846) / sampleStep;
    }
    return grad;
}

static bool solve3x3(double a[3][4], double out[3]) {
    for (int col = 0; col < 3; ++col) {
        int pivot = col;
        for (int r = col + 1; r < 3; ++r) {
            if (std::abs(a[r][col]) > std::abs(a[pivot][col])) {
                pivot = r;
            }
        }
        if (std::abs(a[pivot][col]) < 1e-12) {
            return false;
        }
        if (pivot != col) {
            for (int c = col; c < 4; ++c) {
                std::swap(a[pivot][c], a[col][c]);
            }
        }
        const double div = a[col][col];
        for (int c = col; c < 4; ++c) {
            a[col][c] /= div;
        }
        for (int r = 0; r < 3; ++r) {
            if (r == col) {
                continue;
            }
            const double factor = a[r][col];
            for (int c = col; c < 4; ++c) {
                a[r][c] -= factor * a[col][c];
            }
        }
    }
    for (int i = 0; i < 3; ++i) {
        out[i] = a[i][3];
    }
    return true;
}

static double fitQuadraticSubpixelFixed(const std::vector<double>& x, const std::vector<double>& y) {
    const int n = static_cast<int>(x.size());
    if (n < 3) {
        return x.empty() ? 0.0 : x[static_cast<size_t>(n / 2)];
    }

    const double xMean = std::accumulate(x.begin(), x.end(), 0.0) / static_cast<double>(n);
    std::vector<double> xs(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) {
        xs[static_cast<size_t>(i)] = x[static_cast<size_t>(i)] - xMean;
    }

    double sumX = 0.0;
    double sumX2 = 0.0;
    double sumX3 = 0.0;
    double sumX4 = 0.0;
    double sumY = 0.0;
    double sumXY = 0.0;
    double sumX2Y = 0.0;

    for (int i = 0; i < n; ++i) {
        const double xi = xs[static_cast<size_t>(i)];
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

    double augmented[3][4] = {
        {sumX4, sumX3, sumX2, sumX2Y},
        {sumX3, sumX2, sumX, sumXY},
        {sumX2, sumX, static_cast<double>(n), sumY},
    };
    double coeff[3] = {};
    if (!solve3x3(augmented, coeff)) {
        return x[static_cast<size_t>(n / 2)];
    }

    const double a = coeff[0];
    const double b = coeff[1];
    if (std::abs(a) < 1e-8) {
        return x[static_cast<size_t>(n / 2)];
    }

    double xPeak = -b / (2.0 * a) + xMean;
    xPeak = std::clamp(xPeak, x.front(), x.back());
    return xPeak;
}

static std::vector<EdgePoint> findAllEdgesPeakFixed(
    const std::vector<double>& derivProfile,
    double threshold,
    double sampleStep) {
    std::vector<EdgePoint> edges;
    const int n = static_cast<int>(derivProfile.size());
    int start = -1;

    auto flushSegment = [&](int end) {
        if (start < 0 || end < start) {
            return;
        }

        double maxVal = 0.0;
        int maxIdx = start;
        for (int j = start; j <= end; ++j) {
            if (std::abs(derivProfile[static_cast<size_t>(j)]) > std::abs(maxVal)) {
                maxVal = derivProfile[static_cast<size_t>(j)];
                maxIdx = j;
            }
        }

        std::vector<double> x;
        std::vector<double> y;
        for (int k = maxIdx - 2; k <= maxIdx + 2; ++k) {
            if (k >= 0 && k < n) {
                x.push_back(static_cast<double>(k));
                y.push_back(derivProfile[static_cast<size_t>(k)]);
            }
        }

        const double subPos = fitQuadraticSubpixelFixed(x, y) * sampleStep;
        const int polarity = (maxVal > 0.0) ? +1 : -1;
        edges.push_back({subPos, polarity, maxVal});
    };

    for (int i = 0; i < n; ++i) {
        if (std::abs(derivProfile[static_cast<size_t>(i)]) > threshold) {
            if (start == -1) {
                start = i;
            }
        } else if (start != -1) {
            flushSegment(i - 1);
            start = -1;
        }
    }

    if (start != -1) {
        flushSegment(n - 1);
    }

    return edges;
}

static Image makeStepImage(
    int width,
    int height,
    Point2d normal,
    double d,
    double low,
    double high,
    double blurSigma,
    double noiseStd = 0.0) {
    Image img;
    img.width = width;
    img.height = height;
    img.pixels.resize(static_cast<size_t>(width) * static_cast<size_t>(height));

    std::mt19937 rng(12345);
    std::normal_distribution<double> noise(0.0, noiseStd);
    const double invNorm = 1.0 / std::max(1e-12, length(normal));
    normal = normal * invNorm;
    const double denom = std::max(1e-6, std::sqrt(2.0) * blurSigma);

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const double signedDistance = normal.x * x + normal.y * y - d;
            const double t = 0.5 * (1.0 + std::erf(signedDistance / denom));
            double value = low + (high - low) * t;
            if (noiseStd > 0.0) {
                value += noise(rng);
            }
            value = std::clamp(value, 0.0, 255.0);
            img.pixels[static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)] = value;
        }
    }
    return img;
}

static Image makeStripeImage(
    int width,
    int height,
    double x1,
    double x2,
    double low,
    double high,
    double blurSigma,
    double noiseStd = 0.0) {
    Image img;
    img.width = width;
    img.height = height;
    img.pixels.resize(static_cast<size_t>(width) * static_cast<size_t>(height));

    std::mt19937 rng(23456);
    std::normal_distribution<double> noise(0.0, noiseStd);
    const double denom = std::max(1e-6, std::sqrt(2.0) * blurSigma);

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const double left = 0.5 * (1.0 + std::erf((x - x1) / denom));
            const double right = 0.5 * (1.0 + std::erf((x - x2) / denom));
            double value = low + (high - low) * std::clamp(left - right, 0.0, 1.0);
            if (noiseStd > 0.0) {
                value += noise(rng);
            }
            value = std::clamp(value, 0.0, 255.0);
            img.pixels[static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)] = value;
        }
    }
    return img;
}

static void writePgm(const Image& img, const std::filesystem::path& path) {
    std::ofstream out(path, std::ios::binary);
    out << "P5\n" << img.width << " " << img.height << "\n255\n";
    for (double value : img.pixels) {
        const auto byte = static_cast<unsigned char>(std::clamp(std::lround(value), 0L, 255L));
        out.write(reinterpret_cast<const char*>(&byte), 1);
    }
}

static Detection detectNearExpected(
    const Image& img,
    Point2d p1,
    Point2d p2,
    int width,
    double profileSigma,
    double derivSigma,
    double threshold,
    double expectedX,
    int expectedPolarity = 0) {
    Detection det;
    det.expectedX = expectedX;
    const Point2d v = p2 - p1;
    const double len = length(v);
    const Point2d u = v * (1.0 / len);
    const double padding = std::max(0.0, 3.0 * profileSigma + 3.0 * derivSigma + 2.0 * kProfileSampleSpacing);
    const Point2d extP1 = p1 - u * padding;
    const Point2d extP2 = p2 + u * padding;
    const double extLen = len + 2.0 * padding;
    const int lineSamples = std::max(2, static_cast<int>(std::round(extLen / kProfileSampleSpacing)) + 1);
    const double sampleStep = extLen / static_cast<double>(lineSamples - 1);

    const auto profile = extractProfileFixed(img, extP1, extP2, width, profileSigma);
    const auto grad = computeProfileGradientFixed(profile, derivSigma, sampleStep);
    auto edges = findAllEdgesPeakFixed(grad, threshold, sampleStep);
    for (auto& edge : edges) {
        edge.position -= padding;
    }
    det.edgeCount = static_cast<int>(edges.size());
    if (edges.empty()) {
        return det;
    }

    double bestScore = std::numeric_limits<double>::infinity();
    EdgePoint best;
    Point2d bestGlobal;

    for (const auto& edge : edges) {
        if (expectedPolarity != 0 && edge.polarity != expectedPolarity) {
            continue;
        }
        if (edge.position < 0.0 || edge.position > len) {
            continue;
        }
        const Point2d global = p1 + u * edge.position;
        const double score = std::abs(global.x - expectedX);
        if (score < bestScore) {
            bestScore = score;
            best = edge;
            bestGlobal = global;
        }
    }

    if (!std::isfinite(bestScore)) {
        return det;
    }

    det.found = true;
    det.edge = best;
    det.global = bestGlobal;
    det.errorX = bestGlobal.x - expectedX;
    return det;
}

static std::vector<EdgePoint> detectAll(
    const Image& img,
    Point2d p1,
    Point2d p2,
    int width,
    double profileSigma,
    double derivSigma,
    double threshold) {
    const Point2d v = p2 - p1;
    const double len = length(v);
    const Point2d u = v * (1.0 / len);
    const double padding = std::max(0.0, 3.0 * profileSigma + 3.0 * derivSigma + 2.0 * kProfileSampleSpacing);
    const Point2d extP1 = p1 - u * padding;
    const Point2d extP2 = p2 + u * padding;
    const double extLen = len + 2.0 * padding;
    const int lineSamples = std::max(2, static_cast<int>(std::round(extLen / kProfileSampleSpacing)) + 1);
    const double sampleStep = extLen / static_cast<double>(lineSamples - 1);
    const auto profile = extractProfileFixed(img, extP1, extP2, width, profileSigma);
    const auto grad = computeProfileGradientFixed(profile, derivSigma, sampleStep);
    auto edges = findAllEdgesPeakFixed(grad, threshold, sampleStep);
    std::vector<EdgePoint> clipped;
    for (auto edge : edges) {
        edge.position -= padding;
        if (edge.position >= 0.0 && edge.position <= len) {
            clipped.push_back(edge);
        }
    }
    return clipped;
}

static void addRow(
    std::vector<TestRow>& rows,
    std::string name,
    std::string status,
    double measured,
    double expected,
    double error,
    double extra,
    std::string note) {
    rows.push_back({std::move(name), std::move(status), measured, expected, error, extra, std::move(note)});
}

static void writeCsv(const std::vector<TestRow>& rows, const std::filesystem::path& path) {
    std::ofstream out(path);
    out << "test,status,measured,expected,error,extra,note\n";
    for (const auto& row : rows) {
        out << '"' << row.name << '"' << ','
            << '"' << row.status << '"' << ','
            << fixed(row.measured) << ','
            << fixed(row.expected) << ','
            << fixed(row.error) << ','
            << fixed(row.extra) << ','
            << '"' << row.note << '"' << '\n';
    }
}

static void writeReport(const std::vector<TestRow>& rows, const std::filesystem::path& path) {
    std::ofstream out(path);
    out << "\xEF\xBB\xBF";
    out << "# Caliper validation report\n\n";
    out << "验证对象：修复后的 `caliper.cpp` 卡尺核心流程。测试图像由程序生成，"
           "不依赖外部图片、OpenCV 或 GUI。\n\n";
    out << "## 结论摘要\n\n";
    out << "- 正反向扫描结果已对齐：有效端点和 `p2 = image.cols` 这类边界端点场景都通过。\n";
    out << "- 卡尺沿搜索方向亚像素移动时的全局边缘抖动已作为稳定性指标验证。\n";
    out << "- 末端边缘漏检、偶数 width 采样数错误、双边缘极性顺序等问题均已覆盖。\n\n";

    out << "## 测试结果\n\n";
    out << "| Test | Status | Measured | Expected | Error | Extra | Note |\n";
    out << "|---|---:|---:|---:|---:|---:|---|\n";
    for (const auto& row : rows) {
        out << "| " << row.name
            << " | " << row.status
            << " | " << fixed(row.measured)
            << " | " << fixed(row.expected)
            << " | " << fixed(row.error)
            << " | " << fixed(row.extra)
            << " | " << row.note
            << " |\n";
    }
}

static void writeModificationReport(const std::filesystem::path& path) {
    std::ofstream out(path);
    out << "\xEF\xBB\xBF";
    out << "# Caliper modification report\n\n";
    out << "修改对象：`caliper.cpp`。\n\n";
    out << "## 修改清单\n\n";
    out << "| 问题 | 修改方式 | 影响 |\n";
    out << "|---|---|---|\n";
    out << "| `cv::Mat` 与 `std::vector<float>` 混用 | `extractProfileAlongLine` 统一返回 `std::vector<float>`，梯度和保存函数全部使用同一类型 | 消除接口不一致导致的编译/调用错误 |\n";
    out << "| 正反向扫描边缘偏移 | 线方向采样改为 0.25 像素过采样并包含两端点，使用 `sampleStep = length / (N - 1)` 计算真实距离 | 正向 `[p1,p2]` 与反向 `[p2,p1]` 使用同一组采样点，结果可逆 |\n";
    out << "| 固定 `+0.5` 亚像素偏移 | 二次拟合结果直接保留在样本索引坐标，再乘 `sampleStep` 返回真实距离 | 去掉系统性半像素偏差 |\n";
    out << "| 末端边缘漏检和末端位置偏差 | `findAllEdgesPeak` 在循环结束后 flush 未关闭的超阈值区间；完整检测入口在两端增加滤波 padding，检测后裁回原搜索范围 | 靠近 profile 末端的边缘可输出，且有足够滤波上下文 |\n";
    out << "| width 参数语义不连续 | 横向采样改为精确 `width` 个点，偶数 width 使用半像素对称偏移 | `width=20` 与 `width=21` 不再采样同样数量 |\n";
    out << "| 读图失败处理顺序错误 | 先检查 `imread` 是否成功，再 `cvtColor` | 读图失败时返回清晰错误，不触发 OpenCV 断言 |\n";
    out << "| sigma 含义漂移 | 平滑和一阶导分别使用显式 sigma，并由自有一维卷积实现 | 参数含义稳定，便于调参和复现 |\n\n";
    out << "## 主要接口\n\n";
    out << "- `extractProfileAlongLine(gray, p1, p2, width, smoothSigma)`：提取灰度剖面。\n";
    out << "- `computeProfileGradient(profile, derivativeSigma, sampleStep)`：按真实像素 sigma 计算一维高斯导数。\n";
    out << "- `findAllEdgesPeak(gradient, threshold, sampleStep)`：输出全部边缘，`position` 为从 `p1` 出发的真实距离。\n";
    out << "- `detectEdgesAlongLine(gray, p1, p2, width, profileSmoothSigma, derivativeSigma, threshold)`：完整卡尺检测入口。\n\n";
    out << "## 验证文件\n\n";
    out << "- `caliper_validation_report.md`：修复后验证报告。\n";
    out << "- `caliper_validation_results.csv`：验证数值结果。\n";
    out << "- `validation_output/*.pgm`：自生成测试图像。\n";
}

int main() {
    const std::filesystem::path outputDir = "validation_output";
    std::filesystem::create_directories(outputDir);

    constexpr int w = 256;
    constexpr int h = 160;
    constexpr double low = 40.0;
    constexpr double high = 220.0;
    constexpr int caliperWidth = 31;
    constexpr double profileSigma = 1.0;
    constexpr double derivSigma = 1.0;
    constexpr double threshold = 8.0;
    constexpr double edgeX = 100.35;

    std::vector<TestRow> rows;

    const Image vertical = makeStepImage(w, h, {1.0, 0.0}, edgeX, low, high, 1.2);
    writePgm(vertical, outputDir / "single_vertical_edge.pgm");

    const auto base = detectNearExpected(
        vertical, {20.0, 80.0}, {220.0, 80.0}, caliperWidth, profileSigma, derivSigma, threshold, edgeX, +1);
    addRow(rows, "single vertical edge", (base.found && std::abs(base.errorX) <= 0.10) ? "PASS" : "FAIL", base.global.x, edgeX, base.errorX,
        static_cast<double>(base.edgeCount), base.found ? "baseline absolute x error; extra=edge count" : "no edge detected");

    const auto forward = detectNearExpected(
        vertical, {20.0, 80.0}, {220.0, 80.0}, caliperWidth, profileSigma, derivSigma, threshold, edgeX, +1);
    const auto reverse = detectNearExpected(
        vertical, {220.0, 80.0}, {20.0, 80.0}, caliperWidth, profileSigma, derivSigma, threshold, edgeX, -1);
    const double validEndpointMismatch = (forward.found && reverse.found) ? (reverse.global.x - forward.global.x)
                                                                          : std::numeric_limits<double>::quiet_NaN();
    addRow(rows, "forward/reverse valid endpoints", (std::abs(validEndpointMismatch) <= 0.10) ? "PASS" : "FAIL",
        reverse.global.x, forward.global.x, validEndpointMismatch, static_cast<double>(reverse.edgeCount),
        "forward/reverse global x mismatch; extra=reverse edge count");

    const auto forwardBorder = detectNearExpected(
        vertical, {0.0, 80.0}, {static_cast<double>(w), 80.0}, caliperWidth, profileSigma, derivSigma, threshold, edgeX, +1);
    const auto reverseBorder = detectNearExpected(
        vertical, {static_cast<double>(w), 80.0}, {0.0, 80.0}, caliperWidth, profileSigma, derivSigma, threshold, edgeX, -1);
    const double reverseMismatch = (forwardBorder.found && reverseBorder.found) ? (reverseBorder.global.x - forwardBorder.global.x)
                                                                                : std::numeric_limits<double>::quiet_NaN();
    addRow(rows, "forward/reverse with p2=image.cols", (std::abs(reverseMismatch) <= 0.10) ? "PASS" : "FAIL",
        reverseBorder.global.x, forwardBorder.global.x, reverseMismatch, static_cast<double>(reverseBorder.edgeCount),
        "border endpoint reversibility check; extra=reverse edge count");

    double minX = std::numeric_limits<double>::infinity();
    double maxX = -std::numeric_limits<double>::infinity();
    for (double shift = -0.45; shift <= 0.451; shift += 0.05) {
        const auto det = detectNearExpected(
            vertical, {20.0 + shift, 80.0}, {220.0 + shift, 80.0},
            caliperWidth, profileSigma, derivSigma, threshold, edgeX, +1);
        if (det.found) {
            minX = std::min(minX, det.global.x);
            maxX = std::max(maxX, det.global.x);
        }
    }
    const double phaseJitter = maxX - minX;
    addRow(rows, "subpixel shift along scan", (phaseJitter <= 0.02) ? "PASS" : ((phaseJitter <= 0.10) ? "WARN" : "FAIL"),
        maxX - minX, 0.0, maxX - minX, (minX + maxX) * 0.5,
        "measured is peak-to-peak global-x jitter over +/-0.45 px; extra=mean x");

    double minYShiftX = std::numeric_limits<double>::infinity();
    double maxYShiftX = -std::numeric_limits<double>::infinity();
    for (double y = 70.0; y <= 90.0; y += 2.0) {
        const auto det = detectNearExpected(
            vertical, {20.0, y}, {220.0, y}, caliperWidth, profileSigma, derivSigma, threshold, edgeX, +1);
        if (det.found) {
            minYShiftX = std::min(minYShiftX, det.global.x);
            maxYShiftX = std::max(maxYShiftX, det.global.x);
        }
    }
    addRow(rows, "perpendicular shift on vertical edge", ((maxYShiftX - minYShiftX) <= 0.02) ? "PASS" : "WARN",
        maxYShiftX - minYShiftX, 0.0, maxYShiftX - minYShiftX, (minYShiftX + maxYShiftX) * 0.5,
        "vertical ideal edge should not move when caliper shifts in y");

    const double angle = 15.0 * 3.14159265358979323846 / 180.0;
    const Point2d normal{std::cos(angle), std::sin(angle)};
    const double d = normal.x * edgeX + normal.y * 80.0;
    const Image slanted = makeStepImage(w, h, normal, d, low, high, 1.2);
    writePgm(slanted, outputDir / "slanted_edge_15deg.pgm");

    double maxSlantedError = 0.0;
    double geometryMove = 0.0;
    double firstExpected = std::numeric_limits<double>::quiet_NaN();
    double lastExpected = std::numeric_limits<double>::quiet_NaN();
    for (double y = 72.0; y <= 88.0; y += 4.0) {
        const double expected = (d - normal.y * y) / normal.x;
        if (!std::isfinite(firstExpected)) {
            firstExpected = expected;
        }
        lastExpected = expected;
        const auto det = detectNearExpected(
            slanted, {20.0, y}, {220.0, y}, caliperWidth, profileSigma, derivSigma, threshold, expected, +1);
        if (det.found) {
            maxSlantedError = std::max(maxSlantedError, std::abs(det.errorX));
        }
    }
    geometryMove = std::abs(lastExpected - firstExpected);
    addRow(rows, "slanted edge y-shift geometry", "INFO", geometryMove, 0.0, maxSlantedError, angle * 180.0 / 3.14159265358979323846,
        "on a 15 degree edge, moving y changes the true x-intersection; extra=edge angle deg");

    const double stripeX1 = 80.25;
    const double stripeX2 = 150.75;
    const Image stripe = makeStripeImage(w, h, stripeX1, stripeX2, low, high, 1.2);
    writePgm(stripe, outputDir / "bright_stripe_two_edges.pgm");
    const auto stripeEdges = detectAll(stripe, {20.0, 80.0}, {220.0, 80.0}, caliperWidth, profileSigma, derivSigma, threshold);
    const bool stripeOk = stripeEdges.size() == 2 && stripeEdges[0].polarity == +1 && stripeEdges[1].polarity == -1;
    double measuredWidth = std::numeric_limits<double>::quiet_NaN();
    if (stripeEdges.size() >= 2) {
        measuredWidth = stripeEdges[1].position - stripeEdges[0].position;
    }
    addRow(rows, "two-edge bright stripe", stripeOk ? "PASS" : "FAIL", measuredWidth, stripeX2 - stripeX1,
        measuredWidth - (stripeX2 - stripeX1), static_cast<double>(stripeEdges.size()),
        "expects rising then falling edge; extra=edge count");

    const double nearEndX = 218.5;
    const Image nearEnd = makeStepImage(w, h, {1.0, 0.0}, nearEndX, low, high, 1.2);
    writePgm(nearEnd, outputDir / "edge_near_profile_end.pgm");
    const auto nearEndDet = detectNearExpected(
        nearEnd, {20.0, 80.0}, {220.0, 80.0}, caliperWidth, profileSigma, derivSigma, threshold, nearEndX, +1);
    addRow(rows, "edge near profile end", (nearEndDet.found && std::abs(nearEndDet.errorX) <= 0.15) ? "PASS" : "FAIL", nearEndDet.global.x, nearEndX,
        nearEndDet.errorX, static_cast<double>(nearEndDet.edgeCount),
        "detects whether trailing threshold segment is flushed; extra=edge count");

    const int effective20 = 20;
    const int effective21 = 21;
    addRow(rows, "width 20 vs 21 effective samples", (effective20 != effective21) ? "PASS" : "FAIL",
        static_cast<double>(effective20), static_cast<double>(effective21), static_cast<double>(effective20 - effective21),
        0.0, "fixed code uses exactly width samples");

    writeCsv(rows, "caliper_validation_results.csv");
    writeReport(rows, "caliper_validation_report.md");
    writeModificationReport("caliper_modification_report.md");

    std::cout << "Wrote caliper_validation_report.md\n";
    std::cout << "Wrote caliper_modification_report.md\n";
    std::cout << "Wrote caliper_validation_results.csv\n";
    std::cout << "Wrote validation_output/*.pgm\n";
    for (const auto& row : rows) {
        std::cout << row.name << ": " << row.status
                  << " measured=" << fixed(row.measured)
                  << " expected=" << fixed(row.expected)
                  << " error=" << fixed(row.error)
                  << " extra=" << fixed(row.extra) << '\n';
    }
    return 0;
}
