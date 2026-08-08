#pragma once
#include <optional>
#include <glm/glm.hpp>
#include "LMath.h"

namespace WorldToScreen {
    struct ProjectionContext {
        Vec3 origin;
        glm::mat4x4 mvp;
        Vec2 screenSize;
    };

    std::optional<ProjectionContext> createContext();
    std::optional<Vec2> convert(const Vec3& worldPos, const ProjectionContext& context);
    std::optional<Vec2> convert(const Vec3& worldPos);
};
