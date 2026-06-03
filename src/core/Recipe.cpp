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

QJsonObject toolToJson(const CaliperTool& tool) {
    QJsonObject obj;
    obj["id"] = qstr(tool.id);
    obj["name"] = qstr(tool.name);
    obj["type"] = "caliper";
    obj["enabled"] = tool.enabled;
    obj["p1"] = pointToJson(tool.p1);
    obj["p2"] = pointToJson(tool.p2);
    obj["width"] = tool.width;
    obj["profileSmoothSigma"] = tool.profileSmoothSigma;
    obj["derivativeSigma"] = tool.derivativeSigma;
    obj["positiveThreshold"] = tool.positiveThreshold;
    obj["negativeThreshold"] = tool.negativeThreshold;
    obj["polarity"] = qstr(toString(tool.polarity));
    obj["edgePickMode"] = qstr(toString(tool.edgePickMode));
    return obj;
}

CaliperTool toolFromJson(const QJsonObject& obj) {
    CaliperTool tool;
    tool.id = str(obj.value("id").toString());
    tool.name = str(obj.value("name").toString("卡尺"));
    tool.enabled = obj.value("enabled").toBool(true);
    tool.p1 = pointFromJson(obj.value("p1").toObject());
    tool.p2 = pointFromJson(obj.value("p2").toObject());
    tool.width = std::max(1, obj.value("width").toInt(31));
    tool.profileSmoothSigma = std::max(0.0, obj.value("profileSmoothSigma").toDouble(0.4));
    tool.derivativeSigma = std::max(0.1, obj.value("derivativeSigma").toDouble(0.4));
    tool.positiveThreshold = obj.value("positiveThreshold").toDouble(8.0);
    tool.negativeThreshold = obj.value("negativeThreshold").toDouble(-8.0);
    tool.polarity = edgePolarityFromString(str(obj.value("polarity").toString("any")));
    tool.edgePickMode = edgePickModeFromString(str(obj.value("edgePickMode").toString("strongest")));
    return tool;
}

QJsonObject measurementToJson(const EdgePairMeasurement& measurement) {
    QJsonObject obj;
    obj["id"] = qstr(measurement.id);
    obj["name"] = qstr(measurement.name);
    obj["type"] = "edge_pair";
    obj["enabled"] = measurement.enabled;
    obj["caliperToolId"] = qstr(measurement.caliperToolId);
    obj["edgeAPosition"] = measurement.edgeAPosition;
    obj["edgeBPosition"] = measurement.edgeBPosition;
    return obj;
}

EdgePairMeasurement measurementFromJson(const QJsonObject& obj) {
    EdgePairMeasurement measurement;
    measurement.id = str(obj.value("id").toString());
    measurement.name = str(obj.value("name").toString("边距"));
    measurement.enabled = obj.value("enabled").toBool(true);
    measurement.caliperToolId = str(obj.value("caliperToolId").toString());
    measurement.edgeAPosition = obj.value("edgeAPosition").toDouble(0.0);
    measurement.edgeBPosition = obj.value("edgeBPosition").toDouble(0.0);
    return measurement;
}

} // namespace

bool RecipeCodec::saveToFile(const Recipe& recipe, const QString& path, QString* errorMessage) {
    QJsonObject root;
    root["version"] = recipe.version;
    root["imagePath"] = qstr(recipe.imagePath);

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
            *errorMessage = "无法写入配方文件：" + file.errorString();
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
            *errorMessage = "无法读取配方文件：" + file.errorString();
        }
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        if (errorMessage) {
            *errorMessage = "配方 JSON 格式错误：" + parseError.errorString();
        }
        return false;
    }

    const QJsonObject root = doc.object();
    Recipe loaded;
    loaded.version = root.value("version").toInt(1);
    loaded.imagePath = str(root.value("imagePath").toString());

    const QJsonObject calibration = root.value("calibration").toObject();
    loaded.calibration.enabled = calibration.value("enabled").toBool(false);
    loaded.calibration.mmPerPixel = calibration.value("mmPerPixel").toDouble(0.01);

    const QJsonArray tools = root.value("tools").toArray();
    for (const QJsonValue& value : tools) {
        const QJsonObject obj = value.toObject();
        if (obj.value("type").toString("caliper") != "caliper") {
            continue;
        }
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
