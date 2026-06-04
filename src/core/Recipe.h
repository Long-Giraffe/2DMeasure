#pragma once

#include "core/CaliperTypes.h"

#include <QString>

namespace measure {

class RecipeCodec {
public:
    static bool saveToFile(const Recipe& recipe, const QString& path, QString* errorMessage);
    static bool loadFromFile(const QString& path, Recipe* recipe, QString* errorMessage);
};

} // namespace measure
