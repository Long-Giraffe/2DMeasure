#include "core/CaliperDetector.h"

#include <opencv2/imgcodecs.hpp>

#include <iomanip>
#include <iostream>

int main() {
    const std::string path = "E:/Code/2dMeasure/1.png";
    cv::Mat gray = cv::imread(path, cv::IMREAD_GRAYSCALE);
    if (gray.empty()) {
        std::cerr << "failed to read " << path << "\n";
        return 1;
    }

    int minX = gray.cols;
    int minY = gray.rows;
    int maxX = -1;
    int maxY = -1;
    for (int yy = 0; yy < gray.rows; ++yy) {
        for (int xx = 0; xx < gray.cols; ++xx) {
            if (gray.at<uchar>(yy, xx) < 128) {
                minX = std::min(minX, xx);
                minY = std::min(minY, yy);
                maxX = std::max(maxX, xx);
                maxY = std::max(maxY, yy);
            }
        }
    }

    std::cout << "image=" << gray.cols << "x" << gray.rows << "\n";
    std::cout << "black_bbox min=(" << minX << "," << minY << ") max=(" << maxX << "," << maxY << ")\n";
    std::cout << "bbox_boundaries left=" << (minX - 0.5)
              << " right=" << (maxX + 0.5)
              << " top=" << (minY - 0.5)
              << " bottom=" << (maxY + 0.5) << "\n";

    const int y = (minY + maxY) / 2;
    std::cout << "center_y=" << y << "\n";

    for (int x = 1; x < gray.cols; ++x) {
        const int prev = gray.at<uchar>(y, x - 1);
        const int cur = gray.at<uchar>(y, x);
        if (prev != cur) {
            std::cout << "pixel_step x=" << x
                      << " prev_center=" << (x - 1)
                      << " cur_center=" << x
                      << " boundary=" << (x - 0.5)
                      << " " << prev << "->" << cur << "\n";
        }
    }

    measure::CaliperTool tool;
    tool.name = "probe";
    tool.p1 = {0.0, static_cast<double>(y)};
    tool.p2 = {static_cast<double>(gray.cols - 1), static_cast<double>(y)};
    tool.width = 31;
    tool.profileSmoothSigma = 1.0;
    tool.derivativeSigma = 1.0;
    tool.positiveThreshold = 8.0;
    tool.negativeThreshold = -8.0;
    tool.polarity = measure::EdgePolarity::Any;
    tool.edgePickMode = measure::EdgePickMode::First;

    measure::CaliperDetector detector;
    const auto result = detector.detect(gray, tool);
    std::cout << std::fixed << std::setprecision(6);
    std::cout << "edges=" << result.edges.size() << "\n";
    for (const auto& edge : result.edges) {
        std::cout << "edge x=" << edge.point.x
                  << " y=" << edge.point.y
                  << " pos=" << edge.position
                  << " polarity=" << edge.polarity
                  << " gradient=" << edge.gradient << "\n";
    }

    return 0;
}
