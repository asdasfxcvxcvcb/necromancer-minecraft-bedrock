#include "pch.h"
#include "WorldToScreen.h"
#include "mc/common/client/game/ClientInstance.h"
#include "mc/common/client/gui/GuiData.h"
#include <glm/glm.hpp>

namespace WorldToScreen {
    std::optional<ProjectionContext> createContext() {
        SDK::ClientInstance* clientInstance = SDK::ClientInstance::get();
        if (!clientInstance || !clientInstance->levelRenderer || !clientInstance->minecraftGame) return std::nullopt;

        SDK::GuiData* guiData = clientInstance->getGuiData();
        if (!guiData) return std::nullopt;

        SDK::LevelRendererPlayer* levelRendererPlayer = clientInstance->levelRenderer->getLevelRendererPlayer();
        if (!levelRendererPlayer) return std::nullopt;

        SDK::GameRenderer* gameRenderer = clientInstance->minecraftGame->gameRenderer;
        if (!gameRenderer) return std::nullopt;

        return ProjectionContext {
            levelRendererPlayer->getOrigin(),
            gameRenderer->lastProjectionMatrix._m * gameRenderer->lastViewMatrix._m,
            guiData->screenSize,
        };
    }

    std::optional<Vec2> convert(const Vec3& worldPos, const ProjectionContext& context) {
        Vec3 pos = worldPos - context.origin;
        glm::vec4 clipCoords = context.mvp * glm::vec4(pos.x, pos.y, pos.z, 1.0f);
        if (clipCoords.w < 0.1f) return std::nullopt;

        float invW = 1.0f / clipCoords.w;
        float ndcX = clipCoords.x * invW;
        float ndcY = clipCoords.y * invW;
        return Vec2((ndcX + 1.0f) * 0.5f * context.screenSize.x,
                    (1.0f - ndcY) * 0.5f * context.screenSize.y);
    }

    std::optional<Vec2> convert(const Vec3& worldPos) {
        auto context = createContext();
        if (!context) return std::nullopt;
        return convert(worldPos, *context);
    }
}
