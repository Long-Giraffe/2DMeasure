#include "core/RecipeRunner.h"

#include <algorithm>
#include <cmath>

#include <opencv2/imgproc.hpp>

namespace measure {
namespace {

int findToolIndex(const std::vector<MeasureTool>& tools, const std::string& id) {
    for (int i = 0; i < static_cast<int>(tools.size()); ++i) {
        if (tools[static_cast<size_t>(i)].id == id) {
            return i;
        }
    }
    return -1;
}

int nearestEdgeIndex(const CaliperResult& result, double position, double maxDistance = 3.0) {
    int best = -1;
    double bestDistance = maxDistance;
    for (int i = 0; i < static_cast<int>(result.edges.size()); ++i) {
        const double distance = std::abs(result.edges[static_cast<size_t>(i)].position - position);
        if (distance <= bestDistance) {
            best = i;
            bestDistance = distance;
        }
    }
    return best;
}

} // namespace

cv::Mat RecipeRunner::selectChannel(const cv::Mat& colorBgr, ImageChannel channel) {
    if (colorBgr.empty()) {
        return {};
    }
    if (colorBgr.channels() == 1) {
        return colorBgr.clone();
    }

    if (channel == ImageChannel::Gray) {
        cv::Mat gray;
        cv::cvtColor(colorBgr, gray, cv::COLOR_BGR2GRAY);
        return gray;
    }

    std::vector<cv::Mat> channels;
    cv::split(colorBgr, channels);
    const int index = channel == ImageChannel::Blue ? 0 :
                      channel == ImageChannel::Green ? 1 : 2;
    return channels[static_cast<size_t>(index)].clone();
}

RecipeRunResult RecipeRunner::run(const Recipe& recipe, const cv::Mat& colorBgr) const {
    RecipeRunResult run;
    run.measurementImage = selectChannel(colorBgr, recipe.defaultChannel);
    if (run.measurementImage.empty()) {
        run.message = "Image is empty";
        return run;
    }

    run.toolResults.resize(recipe.tools.size());
    cv::Point2d locatorOffset(0.0, 0.0);
    bool hasLocator = false;
    bool locatorOk = false;

    for (int i = 0; i < static_cast<int>(recipe.tools.size()); ++i) {
        const auto& tool = recipe.tools[static_cast<size_t>(i)];
        if (tool.type != ToolType::TemplateLocator) {
            continue;
        }
        auto result = detector_.detect(run.measurementImage, tool);
        run.toolResults[static_cast<size_t>(i)] = result;
        if (!hasLocator) {
            hasLocator = true;
            locatorOk = result.ok;
            if (result.ok) {
                locatorOffset = result.offset;
            }
        }
    }

    for (int i = 0; i < static_cast<int>(recipe.tools.size()); ++i) {
        const auto& tool = recipe.tools[static_cast<size_t>(i)];
        if (tool.type == ToolType::TemplateLocator) {
            continue;
        }
        auto shifted = tool;
        if (hasLocator && locatorOk) {
            shifted.p1 += locatorOffset;
            shifted.p2 += locatorOffset;
            shifted.center += locatorOffset;
        }
        auto result = detector_.detect(run.measurementImage, shifted);
        if (hasLocator && !locatorOk) {
            result.message = std::string("Unlocated; ") + result.message;
        }
        run.toolResults[static_cast<size_t>(i)] = result;
    }

    run.measurementResults.reserve(recipe.measurements.size());
    for (const auto& measurement : recipe.measurements) {
        MeasurementResult result;
        const int toolAIndex = findToolIndex(recipe.tools, measurement.toolAId);
        const int toolBIndex = findToolIndex(recipe.tools, measurement.toolBId);
        if (!measurement.enabled) {
            result.message = "Disabled";
        } else if (toolAIndex < 0 || toolBIndex < 0) {
            result.message = "Caliper missing";
        } else if (recipe.tools[static_cast<size_t>(toolAIndex)].type != ToolType::LineCaliper ||
                   recipe.tools[static_cast<size_t>(toolBIndex)].type != ToolType::LineCaliper) {
            result.message = "Only line caliper edges are supported";
        } else {
            const auto& toolAResult = run.toolResults[static_cast<size_t>(toolAIndex)];
            const auto& toolBResult = run.toolResults[static_cast<size_t>(toolBIndex)];
            const int edgeAIndex = nearestEdgeIndex(toolAResult, measurement.edgeAPosition);
            const int edgeBIndex = nearestEdgeIndex(toolBResult, measurement.edgeBPosition);
            if (edgeAIndex < 0 || edgeBIndex < 0) {
                result.message = "Edge missing";
            } else {
                result.pointA = toolAResult.edges[static_cast<size_t>(edgeAIndex)].point;
                result.pointB = toolBResult.edges[static_cast<size_t>(edgeBIndex)].point;
                result.distancePx = std::hypot(result.pointB.x - result.pointA.x, result.pointB.y - result.pointA.y);
                result.ok = true;
                result.message = "OK";
            }
        }
        run.measurementResults.push_back(result);
    }

    run.ok = true;
    for (const auto& result : run.toolResults) {
        run.ok = run.ok && result.ok;
    }
    for (const auto& result : run.measurementResults) {
        run.ok = run.ok && result.ok;
    }
    run.message = run.ok ? "OK" : "One or more results are NG";
    return run;
}

} // namespace measure
