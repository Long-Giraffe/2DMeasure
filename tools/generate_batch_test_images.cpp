#include "core/RecipeRunner.h"

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

constexpr int kImageWidth = 640;
constexpr int kImageHeight = 480;
constexpr int kMarkerX = 80;
constexpr int kMarkerY = 70;
constexpr int kMarkerSize = 72;
constexpr int kBarX = 280;
constexpr int kBarY = 320;
constexpr int kBarWidth = 120;
constexpr int kBarHeight = 56;
constexpr int kCircleX = 430;
constexpr int kCircleY = 190;
constexpr int kCircleRadius = 62;

struct Variant {
    std::string fileName;
    int dx = 0;
    int dy = 0;
    int circleRadius = kCircleRadius;
    int barWidth = kBarWidth;
    int brightness = 0;
    double noiseSigma = 0.0;
    double blurSigma = 0.0;
    bool drawMarker = true;
    bool corruptMarker = false;
    bool drawCircle = true;
    bool drawBar = true;
    bool lowContrast = false;
    std::string note;
};

std::string jsonEscape(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size() + 16);
    for (const char c : value) {
        switch (c) {
        case '\\': escaped += "\\\\"; break;
        case '"': escaped += "\\\""; break;
        case '\n': escaped += "\\n"; break;
        case '\r': escaped += "\\r"; break;
        case '\t': escaped += "\\t"; break;
        default: escaped += c; break;
        }
    }
    return escaped;
}

std::string csvEscape(const std::string& value) {
    std::string escaped = value;
    size_t pos = 0;
    while ((pos = escaped.find('"', pos)) != std::string::npos) {
        escaped.insert(pos, 1, '"');
        pos += 2;
    }
    return '"' + escaped + '"';
}

std::string base64Encode(const std::vector<uchar>& bytes) {
    static const char table[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string encoded;
    encoded.reserve((bytes.size() + 2) / 3 * 4);
    for (size_t i = 0; i < bytes.size(); i += 3) {
        const unsigned int b0 = bytes[i];
        const unsigned int b1 = i + 1 < bytes.size() ? bytes[i + 1] : 0;
        const unsigned int b2 = i + 2 < bytes.size() ? bytes[i + 2] : 0;
        const unsigned int packed = (b0 << 16) | (b1 << 8) | b2;
        encoded.push_back(table[(packed >> 18) & 0x3F]);
        encoded.push_back(table[(packed >> 12) & 0x3F]);
        encoded.push_back(i + 1 < bytes.size() ? table[(packed >> 6) & 0x3F] : '=');
        encoded.push_back(i + 2 < bytes.size() ? table[packed & 0x3F] : '=');
    }
    return encoded;
}

std::string pointJson(const cv::Point2d& point) {
    std::ostringstream out;
    out << "{\"x\":" << point.x << ",\"y\":" << point.y << '}';
    return out.str();
}

std::string rectJson(const cv::Rect2d& rect) {
    std::ostringstream out;
    out << "{\"x\":" << rect.x << ",\"y\":" << rect.y
        << ",\"width\":" << rect.width << ",\"height\":" << rect.height << '}';
    return out.str();
}

cv::Scalar blendColor(const cv::Scalar& highContrast, const cv::Scalar& lowContrastColor, bool useLowContrast) {
    return useLowContrast ? lowContrastColor : highContrast;
}

void drawMarker(cv::Mat& image, int dx, int dy, bool lowContrast) {
    const cv::Point origin(kMarkerX + dx, kMarkerY + dy);
    const cv::Scalar dark = blendColor(cv::Scalar(25, 35, 45), cv::Scalar(95, 105, 115), lowContrast);
    const cv::Scalar mid = blendColor(cv::Scalar(80, 130, 185), cv::Scalar(120, 145, 165), lowContrast);
    const cv::Scalar light = blendColor(cv::Scalar(245, 245, 245), cv::Scalar(185, 185, 185), lowContrast);

    cv::rectangle(image, cv::Rect(origin.x, origin.y, kMarkerSize, kMarkerSize), dark, cv::FILLED);
    cv::rectangle(image, cv::Rect(origin.x + 5, origin.y + 5, kMarkerSize - 10, kMarkerSize - 10), light, cv::FILLED);
    cv::line(image, origin + cv::Point(10, 12), origin + cv::Point(58, 55), dark, 5, cv::LINE_AA);
    cv::line(image, origin + cv::Point(12, 56), origin + cv::Point(38, 18), mid, 6, cv::LINE_AA);
    cv::circle(image, origin + cv::Point(51, 18), 8, mid, cv::FILLED, cv::LINE_AA);
    cv::circle(image, origin + cv::Point(20, 45), 6, dark, cv::FILLED, cv::LINE_AA);
    cv::rectangle(image, cv::Rect(origin.x + 43, origin.y + 39, 16, 17), dark, cv::FILLED);
}

cv::Mat makeImage(const Variant& variant) {
    const cv::Scalar background = variant.lowContrast
        ? cv::Scalar(190, 190, 190)
        : cv::Scalar(225, 225, 225);
    const cv::Scalar objectColor = variant.lowContrast
        ? cv::Scalar(105, 105, 105)
        : cv::Scalar(45, 45, 45);

    cv::Mat image(kImageHeight, kImageWidth, CV_8UC3, background);
    if (variant.drawMarker) {
        drawMarker(image, variant.dx, variant.dy, variant.lowContrast);
        if (variant.corruptMarker) {
            const cv::Point p(kMarkerX + variant.dx, kMarkerY + variant.dy);
            cv::rectangle(image, cv::Rect(p.x + 8, p.y + 8, 52, 52), background, cv::FILLED);
            cv::line(image, p + cv::Point(8, 60), p + cv::Point(60, 8), objectColor, 6, cv::LINE_AA);
        }
    }

    if (variant.drawCircle) {
        cv::circle(
            image,
            cv::Point(kCircleX + variant.dx, kCircleY + variant.dy),
            variant.circleRadius,
            objectColor,
            cv::FILLED,
            cv::LINE_AA);
    }

    if (variant.drawBar) {
        cv::rectangle(
            image,
            cv::Rect(kBarX + variant.dx, kBarY + variant.dy, variant.barWidth, kBarHeight),
            objectColor,
            cv::FILLED);
    }

    if (variant.blurSigma > 0.0) {
        const int radius = std::max(1, static_cast<int>(std::ceil(3.0 * variant.blurSigma)));
        const int size = 2 * radius + 1;
        cv::GaussianBlur(image, image, cv::Size(size, size), variant.blurSigma);
    }

    if (variant.brightness != 0) {
        image.convertTo(image, -1, 1.0, variant.brightness);
    }

    if (variant.noiseSigma > 0.0) {
        cv::Mat noise(image.size(), CV_16SC3);
        cv::RNG rng(1000 + variant.dx * 17 + variant.dy * 31);
        rng.fill(noise, cv::RNG::NORMAL, 0.0, variant.noiseSigma);
        cv::Mat signedImage;
        image.convertTo(signedImage, CV_16SC3);
        signedImage += noise;
        signedImage.convertTo(image, CV_8UC3);
    }
    return image;
}

measure::MeasureTool makeLineTool(
    const std::string& id,
    const std::string& name,
    const cv::Point2d& p1,
    const cv::Point2d& p2,
    measure::EdgePolarity polarity) {
    measure::MeasureTool tool;
    tool.id = id;
    tool.name = name;
    tool.type = measure::ToolType::LineCaliper;
    tool.channel = measure::ImageChannel::Gray;
    tool.p1 = p1;
    tool.p2 = p2;
    tool.width = 31;
    tool.profileSmoothSigma = 0.4;
    tool.derivativeSigma = 0.4;
    tool.positiveThreshold = 8.0;
    tool.negativeThreshold = -8.0;
    tool.polarity = polarity;
    tool.edgePickMode = measure::EdgePickMode::Strongest;
    return tool;
}

measure::Recipe makeRecipe(const cv::Mat& reference, const fs::path& referencePath) {
    measure::Recipe recipe;
    recipe.version = 3;
    recipe.imagePath = fs::absolute(referencePath).generic_string();
    recipe.defaultChannel = measure::ImageChannel::Gray;
    recipe.calibration.enabled = true;
    recipe.calibration.mmPerPixel = 0.05;

    measure::MeasureTool locator;
    locator.id = "template_locator_1";
    locator.name = "Template Locator";
    locator.type = measure::ToolType::TemplateLocator;
    locator.channel = measure::ImageChannel::Gray;
    locator.templateRoi = cv::Rect2d(kMarkerX, kMarkerY, kMarkerSize, kMarkerSize);
    locator.searchRoi = cv::Rect2d(20, 20, 260, 220);
    locator.templateReferencePoint = cv::Point2d(kMarkerX, kMarkerY);
    locator.templateScoreThreshold = 0.75;

    cv::Mat gray;
    cv::cvtColor(reference, gray, cv::COLOR_BGR2GRAY);
    std::vector<uchar> pngBytes;
    cv::imencode(
        ".png",
        gray(cv::Rect(kMarkerX, kMarkerY, kMarkerSize, kMarkerSize)),
        pngBytes);
    locator.templateImageBase64Png = base64Encode(pngBytes);
    recipe.tools.push_back(locator);

    recipe.tools.push_back(makeLineTool(
        "line_left",
        "Bar Left Edge",
        cv::Point2d(240, 348),
        cv::Point2d(330, 348),
        measure::EdgePolarity::BrightToDark));
    recipe.tools.push_back(makeLineTool(
        "line_right",
        "Bar Right Edge",
        cv::Point2d(350, 348),
        cv::Point2d(440, 348),
        measure::EdgePolarity::DarkToBright));

    measure::MeasureTool circle;
    circle.id = "circle_1";
    circle.name = "Circle Diameter";
    circle.type = measure::ToolType::CircleCaliper;
    circle.channel = measure::ImageChannel::Gray;
    circle.center = cv::Point2d(kCircleX, kCircleY);
    circle.innerRadius = 42.0;
    circle.outerRadius = 82.0;
    circle.sampleCount = 96;
    circle.profileSmoothSigma = 0.4;
    circle.derivativeSigma = 0.4;
    circle.positiveThreshold = 8.0;
    circle.negativeThreshold = -8.0;
    circle.polarity = measure::EdgePolarity::DarkToBright;
    circle.edgePickMode = measure::EdgePickMode::Strongest;
    circle.circleFitMethod = measure::CircleFitMethod::Ransac;
    circle.ransacThreshold = 1.5;
    circle.ransacIterations = 200;
    recipe.tools.push_back(circle);

    measure::RecipeRunner runner;
    const auto referenceRun = runner.run(recipe, reference);
    if (referenceRun.toolResults.size() >= 3) {
        const auto* left = referenceRun.toolResults[1].selectedEdge();
        const auto* right = referenceRun.toolResults[2].selectedEdge();
        if (left && right) {
            measure::EdgePairMeasurement distance;
            distance.id = "bar_width";
            distance.name = "Bar Width";
            distance.toolAId = "line_left";
            distance.toolBId = "line_right";
            distance.edgeAPosition = left->position;
            distance.edgeBPosition = right->position;
            recipe.measurements.push_back(distance);
        }
    }
    return recipe;
}

void writeToolJson(std::ostream& out, const measure::MeasureTool& tool, bool last) {
    out << "    {\n"
        << "      \"id\": \"" << jsonEscape(tool.id) << "\",\n"
        << "      \"name\": \"" << jsonEscape(tool.name) << "\",\n"
        << "      \"type\": \"" << measure::toString(tool.type) << "\",\n"
        << "      \"enabled\": " << (tool.enabled ? "true" : "false") << ",\n"
        << "      \"channel\": \"" << measure::toString(tool.channel) << "\",\n"
        << "      \"p1\": " << pointJson(tool.p1) << ",\n"
        << "      \"p2\": " << pointJson(tool.p2) << ",\n"
        << "      \"width\": " << tool.width << ",\n"
        << "      \"profileSmoothSigma\": " << tool.profileSmoothSigma << ",\n"
        << "      \"derivativeSigma\": " << tool.derivativeSigma << ",\n"
        << "      \"positiveThreshold\": " << tool.positiveThreshold << ",\n"
        << "      \"negativeThreshold\": " << tool.negativeThreshold << ",\n"
        << "      \"polarity\": \"" << measure::toString(tool.polarity) << "\",\n"
        << "      \"edgePickMode\": \"" << measure::toString(tool.edgePickMode) << "\",\n"
        << "      \"center\": " << pointJson(tool.center) << ",\n"
        << "      \"innerRadius\": " << tool.innerRadius << ",\n"
        << "      \"outerRadius\": " << tool.outerRadius << ",\n"
        << "      \"sampleCount\": " << tool.sampleCount << ",\n"
        << "      \"circleFitMethod\": \"" << measure::toString(tool.circleFitMethod) << "\",\n"
        << "      \"ransacThreshold\": " << tool.ransacThreshold << ",\n"
        << "      \"ransacIterations\": " << tool.ransacIterations << ",\n"
        << "      \"templateRoi\": " << rectJson(tool.templateRoi) << ",\n"
        << "      \"searchRoi\": " << rectJson(tool.searchRoi) << ",\n"
        << "      \"templateReferencePoint\": " << pointJson(tool.templateReferencePoint) << ",\n"
        << "      \"templateScoreThreshold\": " << tool.templateScoreThreshold << ",\n"
        << "      \"templateImageBase64Png\": \"" << tool.templateImageBase64Png << "\"\n"
        << "    }" << (last ? "\n" : ",\n");
}

bool writeRecipe(const measure::Recipe& recipe, const fs::path& path) {
    std::ofstream out(path);
    if (!out) {
        return false;
    }
    out << std::setprecision(12);
    out << "{\n"
        << "  \"version\": " << recipe.version << ",\n"
        << "  \"imagePath\": \"" << jsonEscape(recipe.imagePath) << "\",\n"
        << "  \"defaultChannel\": \"" << measure::toString(recipe.defaultChannel) << "\",\n"
        << "  \"calibration\": {\"enabled\": "
        << (recipe.calibration.enabled ? "true" : "false")
        << ", \"mmPerPixel\": " << recipe.calibration.mmPerPixel << "},\n"
        << "  \"tools\": [\n";
    for (size_t i = 0; i < recipe.tools.size(); ++i) {
        writeToolJson(out, recipe.tools[i], i + 1 == recipe.tools.size());
    }
    out << "  ],\n  \"measurements\": [\n";
    for (size_t i = 0; i < recipe.measurements.size(); ++i) {
        const auto& measurement = recipe.measurements[i];
        out << "    {\"id\":\"" << jsonEscape(measurement.id)
            << "\",\"name\":\"" << jsonEscape(measurement.name)
            << "\",\"type\":\"edge_pair\",\"enabled\":"
            << (measurement.enabled ? "true" : "false")
            << ",\"toolAId\":\"" << jsonEscape(measurement.toolAId)
            << "\",\"toolBId\":\"" << jsonEscape(measurement.toolBId)
            << "\",\"edgeAPosition\":" << measurement.edgeAPosition
            << ",\"edgeBPosition\":" << measurement.edgeBPosition << "}"
            << (i + 1 == recipe.measurements.size() ? "\n" : ",\n");
    }
    out << "  ]\n}\n";
    return true;
}

std::string status(bool ok) {
    return ok ? "OK" : "NG";
}

void writeExpectedHeader(std::ostream& out) {
    out << "image,dx,dy,note,overall_status,locator_status,locator_score,"
           "locator_dx,locator_dy,left_edge_status,left_edge_x,right_edge_status,"
           "right_edge_x,circle_status,circle_diameter_px,bar_width_status,bar_width_px,message\n";
}

void writeExpectedRow(
    std::ostream& out,
    const Variant& variant,
    const measure::RecipeRunResult& run) {
    const auto& locator = run.toolResults[0];
    const auto& left = run.toolResults[1];
    const auto& right = run.toolResults[2];
    const auto& circle = run.toolResults[3];
    const auto* leftEdge = left.selectedEdge();
    const auto* rightEdge = right.selectedEdge();
    const bool hasDistance = !run.measurementResults.empty();
    const auto* distance = hasDistance ? &run.measurementResults[0] : nullptr;

    out << csvEscape(variant.fileName) << ','
        << variant.dx << ',' << variant.dy << ','
        << csvEscape(variant.note) << ','
        << status(run.ok) << ','
        << status(locator.ok) << ','
        << std::fixed << std::setprecision(6) << locator.matchScore << ','
        << locator.offset.x << ',' << locator.offset.y << ','
        << status(left.ok) << ','
        << (leftEdge ? std::to_string(leftEdge->point.x) : std::string()) << ','
        << status(right.ok) << ','
        << (rightEdge ? std::to_string(rightEdge->point.x) : std::string()) << ','
        << status(circle.ok) << ','
        << (circle.ok ? std::to_string(circle.fittedDiameter) : std::string()) << ','
        << (distance ? status(distance->ok) : "NG") << ','
        << (distance && distance->ok ? std::to_string(distance->distancePx) : std::string()) << ','
        << csvEscape(run.message) << '\n';
}

bool writeReadme(const fs::path& path) {
    std::ofstream out(path);
    if (!out) {
        return false;
    }
    out <<
        "# Batch measurement and locator test data\n\n"
        "Generated by `tools/generate_batch_test_images.cpp`.\n\n"
        "## How to use\n\n"
        "1. Start `measure_app.exe`.\n"
        "2. Load `recipe.json`. It opens `reference.png` and contains one template locator, "
        "two line calipers, one circle caliper, and one cross-caliper bar-width measurement.\n"
        "3. Open the Batch Test tab, select the `images` folder, and start the batch run.\n"
        "4. Compare the exported `batch_results.csv` with `expected_results.csv`.\n\n"
        "## Dataset intent\n\n"
        "- Normal images move the full part together and should be recovered by template localization.\n"
        "- Noise, blur, brightness, and low-contrast images exercise robustness.\n"
        "- Circle-large changes the measured diameter.\n"
        "- Bar-wide changes the right edge and intentionally invalidates the stored bar-width edge pair.\n"
        "- Missing or corrupted objects exercise NG handling.\n"
        "- The recipe uses Gray channel and a calibration of 0.05 mm/px.\n";
    return true;
}

} // namespace

int main(int argc, char* argv[]) {
    const fs::path outputRoot = argc > 1 ? fs::path(argv[1]) : fs::path("batch_test_data");
    const fs::path imageDir = outputRoot / "images";
    std::error_code error;
    fs::create_directories(imageDir, error);
    if (error) {
        std::cerr << "Cannot create output directory: " << error.message() << '\n';
        return 1;
    }

    const Variant referenceVariant{"reference.png", 0, 0, kCircleRadius, kBarWidth, 0, 0.0, 0.0, true, false, true, true, false, "Reference recipe image"};
    const cv::Mat reference = makeImage(referenceVariant);
    const fs::path referencePath = outputRoot / "reference.png";
    if (!cv::imwrite(referencePath.string(), reference)) {
        std::cerr << "Cannot write reference image\n";
        return 1;
    }

    measure::Recipe recipe = makeRecipe(reference, referencePath);
    if (recipe.measurements.empty()) {
        std::cerr << "Reference line edges were not detected; recipe cannot be generated\n";
        return 1;
    }
    if (!writeRecipe(recipe, outputRoot / "recipe.json")) {
        std::cerr << "Cannot write recipe.json\n";
        return 1;
    }

    const std::vector<Variant> variants = {
        {"000_reference.png", 0, 0, kCircleRadius, kBarWidth, 0, 0.0, 0.0, true, false, true, true, false, "Reference scene; all results should be OK"},
        {"001_shift_right_down.png", 24, 16, kCircleRadius, kBarWidth, 0, 0.0, 0.0, true, false, true, true, false, "Full scene translated right and down"},
        {"002_shift_left_up.png", -28, -20, kCircleRadius, kBarWidth, 0, 0.0, 0.0, true, false, true, true, false, "Full scene translated left and up"},
        {"003_shift_near_search_limit.png", 48, 32, kCircleRadius, kBarWidth, 0, 0.0, 0.0, true, false, true, true, false, "Translation near search ROI limit"},
        {"004_noise.png", 12, -10, kCircleRadius, kBarWidth, 0, 5.0, 0.0, true, false, true, true, false, "Translated scene with Gaussian noise"},
        {"005_blur.png", -14, 12, kCircleRadius, kBarWidth, 0, 0.0, 1.2, true, false, true, true, false, "Translated scene with blur"},
        {"006_brightness_low.png", 8, 18, kCircleRadius, kBarWidth, -45, 0.0, 0.0, true, false, true, true, false, "Translated darker scene"},
        {"007_low_contrast.png", -10, 20, kCircleRadius, kBarWidth, 0, 0.0, 0.0, true, false, true, true, true, "Translated low-contrast scene"},
        {"008_circle_large.png", 6, -6, 70, kBarWidth, 0, 0.0, 0.0, true, false, true, true, false, "Circle diameter intentionally increased"},
        {"009_bar_wide.png", -6, 4, kCircleRadius, 140, 0, 0.0, 0.0, true, false, true, true, false, "Bar width intentionally increased; stored edge pair should be NG"},
        {"010_locator_missing.png", 0, 0, kCircleRadius, kBarWidth, 0, 0.0, 0.0, false, false, true, true, false, "Template marker missing; locator should be NG"},
        {"011_locator_outside_search.png", 180, 0, kCircleRadius, kBarWidth, 0, 0.0, 0.0, true, false, true, true, false, "Template marker outside search ROI; locator should be NG"},
        {"012_circle_missing.png", 18, -4, kCircleRadius, kBarWidth, 0, 0.0, 0.0, true, false, false, true, false, "Circle missing; circle result should be NG"},
        {"013_bar_missing.png", -16, 8, kCircleRadius, kBarWidth, 0, 0.0, 0.0, true, false, true, false, false, "Bar missing; line and bar-width results should be NG"},
        {"014_locator_corrupted.png", 22, 14, kCircleRadius, kBarWidth, 0, 0.0, 0.0, true, true, true, true, false, "Template marker corrupted; locator should be NG"}
    };

    std::ofstream expected(outputRoot / "expected_results.csv");
    if (!expected) {
        std::cerr << "Cannot write expected_results.csv\n";
        return 1;
    }
    writeExpectedHeader(expected);

    measure::RecipeRunner runner;
    int okCount = 0;
    int ngCount = 0;
    for (const auto& variant : variants) {
        const cv::Mat image = makeImage(variant);
        const fs::path path = imageDir / variant.fileName;
        if (!cv::imwrite(path.string(), image)) {
            std::cerr << "Cannot write " << path.string() << '\n';
            return 1;
        }
        const auto run = runner.run(recipe, image);
        writeExpectedRow(expected, variant, run);
        run.ok ? ++okCount : ++ngCount;
        std::cout << variant.fileName << ": " << status(run.ok) << '\n';
    }

    if (!writeReadme(outputRoot / "README.md")) {
        std::cerr << "Cannot write README.md\n";
        return 1;
    }

    std::cout << "\nGenerated " << variants.size() << " batch images in "
              << fs::absolute(imageDir).string() << '\n'
              << "Expected overall OK: " << okCount << ", NG: " << ngCount << '\n'
              << "Load recipe: " << fs::absolute(outputRoot / "recipe.json").string() << '\n';
    return 0;
}
