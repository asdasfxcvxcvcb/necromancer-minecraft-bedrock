#include "pch.h"
#include "DrawUtil3D.h"
#include "mc/common/client/renderer/MeshUtils.h"
#include "mc/common/client/renderer/game/LevelRendererPlayer.h"
#include <mc/common/client/renderer/Tessellator.h>

MCDrawUtil3D::MCDrawUtil3D(SDK::LevelRenderer* renderer, SDK::ScreenContext* ctx, SDK::MaterialPtr* material)
    : levelRenderer(renderer)
    , screenContext(ctx)
    , material(material) {
    if (!material) {
        this->material = SDK::MaterialPtr::getSelectionBoxMaterial();
    }

    if (levelRenderer) {
        if (auto* player = levelRenderer->getLevelRendererPlayer()) {
            origin = player->getOrigin();
            originValid = true;
        }
    }
}

void MCDrawUtil3D::openBatch(SDK::Primitive primitive, int vertexCount, d2d::Color const& color) {
    auto tess = screenContext->tess;
    *screenContext->shaderColor = { 1.f, 1.f, 1.f, 1.f };
    tess->begin(primitive, vertexCount);
    tess->color(color);
}

void MCDrawUtil3D::emitLineVertices(Vec3 const& a, Vec3 const& b) {
    auto tess = screenContext->tess;
    tess->vertex(a.x - origin.x, a.y - origin.y, a.z - origin.z);
    tess->vertex(b.x - origin.x, b.y - origin.y, b.z - origin.z);
}

void MCDrawUtil3D::emitQuadVertices(Vec3 const& a, Vec3 const& b, Vec3 const& c, Vec3 const& d) {
    auto tess = screenContext->tess;
    float ax = a.x - origin.x, ay = a.y - origin.y, az = a.z - origin.z;
    float bx = b.x - origin.x, by = b.y - origin.y, bz = b.z - origin.z;
    float cx = c.x - origin.x, cy = c.y - origin.y, cz = c.z - origin.z;
    float dx = d.x - origin.x, dy = d.y - origin.y, dz = d.z - origin.z;

    tess->vertex(ax, ay, az);
    tess->vertex(bx, by, bz);
    tess->vertex(cx, cy, cz);
    tess->vertex(dx, dy, dz);

    tess->vertex(dx, dy, dz);
    tess->vertex(cx, cy, cz);
    tess->vertex(bx, by, bz);
    tess->vertex(ax, ay, az);
}

void MCDrawUtil3D::drawLine(Vec3 const& p1, Vec3 const& p2, d2d::Color const& color, bool immediate) {
    openBatch(SDK::Primitive::LineList, 2, color);
    emitLineVertices(p1, p2);
    if (immediate) flush();
}

void MCDrawUtil3D::drawQuad(Vec3 a, Vec3 b, Vec3 c, Vec3 d, d2d::Color const& col) {
    openBatch(SDK::Primitive::LineList, 8, col);
    emitLineVertices(a, b);
    emitLineVertices(b, c);
    emitLineVertices(c, d);
    emitLineVertices(d, a);
}

void MCDrawUtil3D::fillQuad(Vec3 p1, Vec3 p2, Vec3 p3, Vec3 p4, d2d::Color const& color) {
    openBatch(SDK::Primitive::Quad, 8, color);
    emitQuadVertices(p1, p2, p3, p4);
}

void MCDrawUtil3D::emitThickLineVertices(Vec3 const& p1, Vec3 const& p2, float thickness) {
    Vec3 dir = p2 - p1;
    float lenSq = dir.magnitudeSq();
    if (lenSq <= 0.0001f * 0.0001f) return;

    Vec3 fwd = dir * (1.f / std::sqrt(lenSq));
    Vec3 right = std::abs(fwd.y) < 0.99f ? Vec3 { -fwd.z, 0.f, fwd.x }.normalized() : Vec3 { 1.f, 0.f, 0.f };
    Vec3 up { fwd.y * right.z - fwd.z * right.y, fwd.z * right.x - fwd.x * right.z,
              fwd.x * right.y - fwd.y * right.x };
    float half = thickness * 0.5f;
    right = right * half;
    up = up * half;

    Vec3 corners[4] = {
        Vec3 { -right.x - up.x, -right.y - up.y, -right.z - up.z },
        Vec3 { right.x - up.x, right.y - up.y, right.z - up.z },
        Vec3 { right.x + up.x, right.y + up.y, right.z + up.z },
        Vec3 { -right.x + up.x, -right.y + up.y, -right.z + up.z },
    };

    emitQuadVertices(p1 + corners[0], p1 + corners[1], p1 + corners[2], p1 + corners[3]);
    emitQuadVertices(p2 + corners[0], p2 + corners[1], p2 + corners[2], p2 + corners[3]);

    for (int i = 0; i < 4; i++) {
        int n = (i + 1) % 4;
        emitQuadVertices(p1 + corners[i], p2 + corners[i], p2 + corners[n], p1 + corners[n]);
    }
}

void MCDrawUtil3D::drawThickLine(Vec3 const& p1, Vec3 const& p2, float thickness, d2d::Color const& color) {
    Vec3 dir = p2 - p1;
    if (dir.magnitude() <= 0.0001f) return;

    openBatch(SDK::Primitive::Quad, thickLineVertexCount, color);
    emitThickLineVertices(p1, p2, thickness);
}

void MCDrawUtil3D::drawThickBox(AABB const& bb, float thickness, d2d::Color const& color) {
    Vec3 const& l = bb.lower;
    Vec3 const& h = bb.higher;

    Vec3 const edges[12][2] = {
        { { l.x, l.y, l.z }, { h.x, l.y, l.z } },
        { { h.x, l.y, l.z }, { h.x, l.y, h.z } },
        { { h.x, l.y, h.z }, { l.x, l.y, h.z } },
        { { l.x, l.y, h.z }, { l.x, l.y, l.z } },
        { { l.x, h.y, l.z }, { h.x, h.y, l.z } },
        { { h.x, h.y, l.z }, { h.x, h.y, h.z } },
        { { h.x, h.y, h.z }, { l.x, h.y, h.z } },
        { { l.x, h.y, h.z }, { l.x, h.y, l.z } },
        { { l.x, l.y, l.z }, { l.x, h.y, l.z } },
        { { h.x, l.y, l.z }, { h.x, h.y, l.z } },
        { { h.x, l.y, h.z }, { h.x, h.y, h.z } },
        { { l.x, l.y, h.z }, { l.x, h.y, h.z } },
    };

    constexpr float epsSq = 0.0001f * 0.0001f;
    int live = 0;
    for (auto const& edge : edges) {
        if ((edge[1] - edge[0]).magnitudeSq() > epsSq) ++live;
    }
    if (live == 0) return;

    openBatch(SDK::Primitive::Quad, live * thickLineVertexCount, color);
    for (auto const& edge : edges) {
        if ((edge[1] - edge[0]).magnitudeSq() > epsSq) emitThickLineVertices(edge[0], edge[1], thickness);
    }
}

void MCDrawUtil3D::drawBox(AABB const& bb, d2d::Color const& color) {
    Vec3 const& l = bb.lower;
    Vec3 const& h = bb.higher;

    Vec3 const verticals[4][2] = {
        { { l.x, l.y, l.z }, { l.x, h.y, l.z } },
        { { h.x, l.y, l.z }, { h.x, h.y, l.z } },
        { { l.x, l.y, h.z }, { l.x, h.y, h.z } },
        { { h.x, l.y, h.z }, { h.x, h.y, h.z } },
    };

    openBatch(SDK::Primitive::LineList, 24, color);
    for (auto const& edge : verticals) {
        emitLineVertices(edge[0], edge[1]);
    }

    emitLineVertices({ l.x, l.y, l.z }, { h.x, l.y, l.z });
    emitLineVertices({ h.x, l.y, l.z }, { h.x, l.y, h.z });
    emitLineVertices({ h.x, l.y, h.z }, { l.x, l.y, h.z });
    emitLineVertices({ l.x, l.y, h.z }, { l.x, l.y, l.z });

    emitLineVertices({ l.x, h.y, l.z }, { h.x, h.y, l.z });
    emitLineVertices({ h.x, h.y, l.z }, { h.x, h.y, h.z });
    emitLineVertices({ h.x, h.y, h.z }, { l.x, h.y, h.z });
    emitLineVertices({ l.x, h.y, h.z }, { l.x, h.y, l.z });
}

void MCDrawUtil3D::flush() {
    SDK::MeshHelpers::renderMeshImmediately(screenContext, screenContext->tess, material);
}
