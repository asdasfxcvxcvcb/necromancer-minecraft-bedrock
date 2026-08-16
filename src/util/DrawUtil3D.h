#pragma once
#include "DrawContext.h"
#include "mc/common/client/renderer/Tessellator.h"
#include <span>

namespace SDK {
    class LevelRenderer;
    class ScreenContext;
    class MaterialPtr;
}

class MCDrawUtil3D {
private:
    SDK::LevelRenderer* levelRenderer;
    SDK::ScreenContext* screenContext;
    SDK::MaterialPtr* material;
    Vec3 origin { 0.f, 0.f, 0.f };
    bool originValid = false;

    void openBatch(SDK::Primitive primitive, int vertexCount, d2d::Color const& color);
    void emitQuadVertices(Vec3 const& a, Vec3 const& b, Vec3 const& c, Vec3 const& d);
    void emitLineVertices(Vec3 const& a, Vec3 const& b);
    void emitThickLineVertices(Vec3 const& p1, Vec3 const& p2, float thickness);
    void emitFilledBoxVertices(AABB const& box);
    void emitBoxVertices(AABB const& box);
    int thickBoxVertexCountFor(AABB const& box) const;
    void emitThickBoxVertices(AABB const& box, float thickness);

public:
    struct ColoredBox {
        AABB box;
        d2d::Color color;
    };

    struct ColoredThickBox {
        AABB box;
        float thickness;
        d2d::Color color;
    };

    struct ColoredThickLine {
        Vec3 start;
        Vec3 end;
        float thickness;
        d2d::Color color;
    };

    MCDrawUtil3D(SDK::LevelRenderer* renderer, SDK::ScreenContext* ctx, SDK::MaterialPtr* material = nullptr);

    void setMaterial(SDK::MaterialPtr* materialPtr) { this->material = materialPtr; }

    [[nodiscard]] bool isValid() const { return originValid; }
    [[nodiscard]] Vec3 const& getOrigin() const { return origin; }

    static constexpr int thickLineVertexCount = 48;
    static constexpr int thickBoxVertexCount = thickLineVertexCount * 12;

    void drawLine(Vec3 const& pos1, Vec3 const& pos2, d2d::Color const& color, bool immediate = false);
    void drawThickLine(Vec3 const& pos1, Vec3 const& pos2, float thickness, d2d::Color const& color);
    void drawThickBox(AABB const& box, float thickness, d2d::Color const& color);
    void drawQuad(Vec3 a, Vec3 b, Vec3 c, Vec3 d, d2d::Color const& col);
    void fillQuad(Vec3 a, Vec3 b, Vec3 c, Vec3 d, d2d::Color const& color);
    void fillBox(AABB const& box, d2d::Color const& color);
    void drawBox(AABB const& box, d2d::Color const& color);
    void fillBoxes(std::span<ColoredBox const> boxes);
    void drawBoxes(std::span<ColoredBox const> boxes);
    void drawThickBoxes(std::span<ColoredThickBox const> boxes);
    void drawThickLines(std::span<ColoredThickLine const> lines);
    void flush();
};
