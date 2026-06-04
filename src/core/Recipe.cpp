#include "core/Recipe.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>

namespace measure {
namespace {

QString qstr(const std::string& value) {
    return QString::fromStdString(value);
}

std::string str(const QString& value) {
    return value.toStdString();
}

QJsonObject pointToJson(const cv::Point2d& point) {
    QJsonObject obj;
    obj["x"] = point.x;
    obj["y"] = point.y;
    return obj;
}

cv::Point2d pointFromJson(const QJsonObject& obj) {
    return {obj.value("x").toDouble(), obj.value("y").toDouble()};
}

QJsonObject rectToJson(const cv::Rect2d& rect) {
    QJsonObject obj;
    obj["x"] = rect.x;
    obj["y"] = rect.y;
    obj["width"] = rect.width;
    obj["height"] = rect.height;
    return obj;
}

cv::Rect2d rectFromJson(const QJsonObject& obj) {
    return {
        obj.value("x").toDouble(),
        obj.value("y").toDouble(),
        obj.value("width").toDouble(),
        obj.value("height").toDouble()};
}

QJsonObject toolToJson(const MeasureTool& tool) {
    QJsonObject obj;
    obj["id"] = qstr(tool.id);
    obj["name"] = qstr(tool.name);
    obj["type"] = qstr(toString(tool.type));
    obj["enabled"] = tool.enabled;
    obj["channel"] = qstr(toString(tool.channel));

    obj["p1"] = pointToJson(tool.p1);
    obj["p2"] = pointToJson(tool.p2);
    obj["width"] = tool.width;
    obj["profileSmoothSigma"] = tool.profileSmoothSigma;
    obj["derivativeSigma"] = tool.derivativeSigma;
    obj["positiveThreshold"] = tool.positiveThreshold;
    obj["negativeThreshold"] = tool.negativeThreshold;
    obj["polarity"] = qstr(toString(tool.polarity));
    obj["edgePickMode"] = qstr(toString(tool.edgePickMode));

    obj["center"] = pointToJson(tool.center);
    obj["innerRadius"] = tool.innerRadius;
    obj["outerRadius"] = tool.outerRadius;
    obj["sampleCount"] = tool.sampleCount;
    obj["circleFitMethod"] = qstr(toString(tool.circleFitMethod));
    obj["ransacThreshold"] = tool.ransacThreshold;
    obj["ransacIterations"] = tool.ransacIterations;

    obj["templateRoi"] = rectToJson(tool.templateRoi);
    obj["searchRoi"] = rectToJson(tool.searchRoi);
    obj["templateReferencePoint"] = pointToJson(tool.templateReferencePoint);
    obj["templateScoreThreshold"] = tool.templateScoreThreshold;
    obj["templateImageBase64Png"] = qstr(tool.templateImageBase64Png);
    return obj;
}

MeasureTool toolFromJson(const QJsonObject& obj) {
    MeasureTool tool;
    tool.id = str(obj.value("id").toString());
    tool.name = str(obj.value("name").toString("Caliper"));
    tool.enabled = obj.value("enabled").toBool(true);

    const std::string type = str(obj.value("type").toString("line_caliper"));
    tool.type = type == "caliper" ? ToolType::LineCaliper : toolTypeFromString(type);
    tool.channel = imageChannelFromString(str(obj.value("channel").toString("gray")));

    tool.p1 = pointFromJson(obj.value("p1").toObject());
    tool.p2 = pointFromJson(obj.value("p2").toObject());
    tool.width = std::max(1, obj.value("width").toInt(31));
    tool.profileSmoothSigma = std::max(0.0, obj.value("profileSmoothSigma").toDouble(0.4));
    tool.derivativeSigma = std::max(0.1, obj.value("derivativeSigma").toDouble(0.4));
    tool.positiveThreshold = obj.value("positiveThreshold").toDouble(8.0);
    tool.negativeThreshold = obj.value("negativeThreshold").toDouble(-8.0);
    tool.polarity = edgePolarityFromString(str(obj.value("polarity").toString("any")));
    tool.edgePickMode = edgePickModeFromString(str(obj.value("edgePickMode").toString("strongest")));

    tool.center = pointFromJson(obj.value("center").toObject());
    tool.innerRadius = std::max(0.0, obj.value("innerRadius").toDouble(40.0));
    tool.outerRadius = std::max(tool.innerRadius + 1.0, obj.value("outerRadius").toDouble(80.0));
    tool.sampleCount = std::max(8, obj.value("sampleCount").toInt(72));
    tool.circleFitMethod = circleFitMethodFromString(str(obj.value("circleFitMethod").toString("least_squares")));
    tool.ransacThreshold = std::max(0.01, obj.value("ransacThreshold").toDouble(2.0));
    tool.ransacIterations = std::max(1, obj.value("ransacIterations").toInt(100));

    tool.templateRoi = rectFromJson(obj.value("templateRoi").toObject());
    tool.searchRoi = rectFromJson(obj.value("searchRoi").toObject());
    if (tool.searchRoi.width <= 0.0 || tool.searchRoi.height <= 0.0) {
        tool.searchRoi = tool.templateRoi;
    }
    tool.templateReferencePoint = pointFromJson(obj.value("templateReferencePoint").toObject());
    tool.templateScoreThreshold = obj.value("templateScoreThreshold").toDouble(0.75);
    tool.templateImageBase64Png = str(obj.value("templateImageBase64Png").toString());
    return tool;
}

QJsonObject measurementToJson(const EdgePairMeasurement& measurement) {
    QJsonObject obj;
    obj["id"] = qstr(measurement.id);
    obj["name"] = qstr(measurement.name);
    obj["type"] = "edge_pair";
    obj["enabled"] = measurement.enabled;
    obj["toolAId"] = qstr(measurement.toolAId);
    obj["toolBId"] = qstr(measurement.toolBId);
    obj["edgeAPosition"] = measurement.edgeAPosition;
    obj["edgeBPosition"] = measurement.edgeBPosition;
    return obj;
}

EdgePairMeasurement measurementFromJson(const QJsonObject& obj) {
    EdgePairMeasurement measurement;
    measurement.id = str(obj.value("id").toString());
    measurement.name = str(obj.value("name").toString("Edge distance"));
    measurement.enabled = obj.value("enabled").toBool(true);
    const std::string legacyToolId = str(obj.value("caliperToolId").toString());
    measurement.toolAId = str(obj.value("toolAId").toString(qstr(legacyToolId)));
    measurement.toolBId = str(obj.value("toolBId").toString(qstr(legacyToolId)));
    measurement.edgeAPosition = obj.value("edgeAPosition").toDouble(0.0);
    measurement.edgeBPosition = obj.value("edgeBPosition").toDouble(0.0);
    return measurement;
}

} // namespace

bool RecipeCodec::saveToFile(const Recipe& recipe, const QString& path, QString* errorMessage) {
    QJsonObject root;
    root["version"] = recipe.version;
    root["imagePath"] = qstr(recipe.imagePath);
    root["defaultChannel"] = qstr(toString(recipe.defaultChannel));

    QJsonObject calibration;
    calibration["enabled"] = recipe.calibration.enabled;
    calibration["mmPerPixel"] = recipe.calibration.mmPerPixel;
    root["calibration"] = calibration;

    QJsonArray tools;
    for (const auto& tool : recipe.tools) {
        tools.append(toolToJson(tool));
    }
    root["tools"] = tools;

    QJsonArray measurements;
    for (const auto& measurement : recipe.measurements) {
        measurements.append(measurementToJson(measurement));
    }
    root["measurements"] = measurements;

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Cannot write recipe file: ") + file.errorString();
        }
        return false;
    }

    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return true;
}

bool RecipeCodec::loadFromFile(const QString& path, Recipe* recipe, QString* errorMessage) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Cannot read recipe file: ") + file.errorString();
        }
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Recipe JSON parse error: ") + parseError.errorString();
        }
        return false;
    }

    const QJsonObject root = doc.object();
    Recipe loaded;
    loaded.version = 3;
    loaded.imagePath = str(root.value("imagePath").toString());
    loaded.defaultChannel = imageChannelFromString(str(root.value("defaultChannel").toString("gray")));

    const QJsonObject calibration = root.value("calibration").toObject();
    loaded.calibration.enabled = calibration.value("enabled").toBool(false);
    loaded.calibration.mmPerPixel = calibration.value("mmPerPixel").toDouble(0.01);

    const QJsonArray tools = root.value("tools").toArray();
    for (const QJsonValue& value : tools) {
        const QJsonObject obj = value.toObject();
        loaded.tools.push_back(toolFromJson(obj));
    }

    const QJsonArray measurements = root.value("measurements").toArray();
    for (const QJsonValue& value : measurements) {
        const QJsonObject obj = value.toObject();
        if (obj.value("type").toString("edge_pair") != "edge_pair") {
            continue;
        }
        loaded.measurements.push_back(measurementFromJson(obj));
    }

    *recipe = loaded;
    return true;
}

} // namespace measure
