#include "ui/MapRender.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace f2mt {

namespace {
constexpr float kHexW = 24.0f;      // расстояние между центрами по X
constexpr float kHexRowH = 20.0f;   // расстояние между рядами по Y

// odd-r offset: нечётные ряды сдвинуты на полшага.
void hexToWorld(int x, int y, float& wx, float& wy) {
    wx = static_cast<float>(x) * kHexW + ((y & 1) ? kHexW * 0.5f : 0.0f);
    wy = static_cast<float>(y) * kHexRowH;
}

ImU32 kindColor(ProtoType k) {
    switch (k) {
        case ProtoType::Critter: return IM_COL32(90, 210, 100, 255);
        case ProtoType::Item: return IM_COL32(240, 210, 70, 255);
        case ProtoType::Scenery: return IM_COL32(80, 180, 230, 255);
        case ProtoType::Wall: return IM_COL32(190, 190, 190, 255);
        case ProtoType::Tile: return IM_COL32(150, 110, 70, 255);
        case ProtoType::Misc: return IM_COL32(220, 110, 210, 255);
    }
    return IM_COL32(255, 255, 255, 255);
}

uint16_t hashId(uint16_t v) {
    uint32_t h = v * 2654435761u;
    h ^= h >> 13;
    return static_cast<uint16_t>(h);
}

void drawHexOutline(ImDrawList* dl, const ImVec2& c, float r, ImU32 color) {
    ImVec2 pts[6];
    for (int i = 0; i < 6; ++i) {
        const float a = 3.14159265358979323846f / 3.0f * static_cast<float>(i) +
                        3.14159265358979323846f / 6.0f;
        pts[i] = ImVec2(c.x + r * std::cos(a), c.y + r * std::sin(a));
    }
    dl->AddPolyline(pts, 6, color, ImDrawFlags_Closed, 1.5f);
}

void drawMarker(ImDrawList* dl, const ImVec2& p, float r, ProtoType k, ImU32 col) {
    switch (k) {
        case ProtoType::Critter:  // треугольник
            dl->AddTriangleFilled(ImVec2(p.x, p.y - r), ImVec2(p.x - r, p.y + r * 0.8f),
                                  ImVec2(p.x + r, p.y + r * 0.8f), col);
            break;
        case ProtoType::Item:  // ромб
            dl->AddQuadFilled(ImVec2(p.x, p.y - r), ImVec2(p.x + r, p.y), ImVec2(p.x, p.y + r),
                              ImVec2(p.x - r, p.y), col);
            break;
        case ProtoType::Scenery:  // квадрат
            dl->AddRectFilled(ImVec2(p.x - r * 0.85f, p.y - r * 0.85f),
                              ImVec2(p.x + r * 0.85f, p.y + r * 0.85f), col);
            break;
        case ProtoType::Wall:  // маленький квадрат
            dl->AddRectFilled(ImVec2(p.x - r * 0.7f, p.y - r * 0.7f),
                              ImVec2(p.x + r * 0.7f, p.y + r * 0.7f), col);
            break;
        case ProtoType::Tile:
            break;
        default:  // круг
            dl->AddCircleFilled(p, r, col, 10);
            break;
    }
}
}  // namespace

ImVec2 Camera::worldToScreen(float wx, float wy) const {
    return ImVec2(panX + wx * zoom, panY + wy * zoom);
}

void Camera::screenToWorld(float sx, float sy, float& wx, float& wy) const {
    wx = (sx - panX) / zoom;
    wy = (sy - panY) / zoom;
}

void mapWorldSize(const Location& loc, float& w, float& h) {
    w = static_cast<float>(loc.grid.hexWidth) * kHexW;
    h = static_cast<float>(loc.grid.hexHeight) * kHexRowH;
}

void fitCameraForMap(const Location& loc, float canvasW, float canvasH, Camera& cam) {
    float w = 0, h = 0;
    mapWorldSize(loc, w, h);
    if (w <= 0.0f || h <= 0.0f) return;
    const float margin = 30.0f;
    const float zx = (canvasW - margin * 2.0f) / w;
    const float zy = (canvasH - margin * 2.0f) / h;
    cam.zoom = std::clamp(std::min(zx, zy), 0.02f, 4.0f);
    cam.panX = (canvasW - w * cam.zoom) * 0.5f;
    cam.panY = (canvasH - h * cam.zoom) * 0.5f;
}

void drawMap(ImDrawList* dl, const ImVec2& origin, float canvasW, float canvasH, const Location& loc,
             const ProjectState& st, const Camera& cam) {
    const ImVec2 clipMin = origin;
    const ImVec2 clipMax = ImVec2(origin.x + canvasW, origin.y + canvasH);
    dl->PushClipRect(clipMin, clipMax, true);

    // Фон области карты.
    float mw = 0, mh = 0;
    mapWorldSize(loc, mw, mh);
    const ImVec2 mapA = cam.worldToScreen(0, 0);
    const ImVec2 mapB = cam.worldToScreen(mw, mh);
    dl->AddRectFilled(ImVec2(origin.x + mapA.x, origin.y + mapA.y),
                      ImVec2(origin.x + mapB.x, origin.y + mapB.y), IM_COL32(30, 30, 36, 255));

    // Сетка каждые 20 гексов.
    if (cam.zoom > 0.1f) {
        const ImU32 grid = IM_COL32(60, 60, 70, 120);
        for (int x = 0; x <= loc.grid.hexWidth; x += 20) {
            const ImVec2 a = cam.worldToScreen(static_cast<float>(x) * kHexW, 0.0f);
            const ImVec2 b = cam.worldToScreen(static_cast<float>(x) * kHexW, mh);
            dl->AddLine(ImVec2(origin.x + a.x, origin.y + a.y), ImVec2(origin.x + b.x, origin.y + b.y),
                        grid);
        }
        for (int y = 0; y <= loc.grid.hexHeight; y += 20) {
            const ImVec2 a = cam.worldToScreen(0.0f, static_cast<float>(y) * kHexRowH);
            const ImVec2 b = cam.worldToScreen(mw, static_cast<float>(y) * kHexRowH);
            dl->AddLine(ImVec2(origin.x + a.x, origin.y + a.y), ImVec2(origin.x + b.x, origin.y + b.y),
                        grid);
        }
    }

    // Тайловый слой (квадратная сетка 100x100) — растянут на габарит карты.
    if (st.showTiles) {
        const float tcx = mw / static_cast<float>(loc.grid.tileWidth);
        const float tcy = mh / static_cast<float>(loc.grid.tileHeight);
        for (int ty = 0; ty < loc.grid.tileHeight; ++ty) {
            for (int tx = 0; tx < loc.grid.tileWidth; ++tx) {
                const TileCell* cell = loc.tileAt(st.elevation, tx, ty);
                if (cell == nullptr || (cell->tileId <= 1 && cell->roofId <= 1)) continue;
                const ImVec2 a = cam.worldToScreen(static_cast<float>(tx) * tcx,
                                                   static_cast<float>(ty) * tcy);
                const ImVec2 b = cam.worldToScreen(static_cast<float>(tx + 1) * tcx,
                                                   static_cast<float>(ty + 1) * tcy);
                if (a.x + origin.x > clipMax.x || a.y + origin.y > clipMax.y) continue;
                if (b.x + origin.x < clipMin.x || b.y + origin.y < clipMin.y) continue;
                const uint16_t hsh = hashId(cell->tileId);
                const ImU32 col = IM_COL32(50 + (hsh & 0x9F), 50 + ((hsh >> 5) & 0x9F),
                                           50 + ((hsh >> 10) & 0x9F), 160);
                dl->AddRectFilled(ImVec2(origin.x + a.x, origin.y + a.y),
                                  ImVec2(origin.x + b.x, origin.y + b.y), col);
            }
        }
    }

    // Объекты.
    const float r = std::max(2.0f, kHexW * 0.42f * cam.zoom);
    if (st.showEntities) {
        for (const auto& e : loc.entities) {
            if (e.elevation != st.elevation) continue;
            float wx = 0, wy = 0;
            hexToWorld(e.x, e.y, wx, wy);
            const ImVec2 c = cam.worldToScreen(wx, wy);
            const ImVec2 p = ImVec2(c.x + origin.x, c.y + origin.y);
            if (p.x < clipMin.x - 30 || p.y < clipMin.y - 30 || p.x > clipMax.x + 30 ||
                p.y > clipMax.y + 30) {
                continue;
            }
            const ImU32 col = kindColor(e.kind);
            drawMarker(dl, p, r, e.kind, col);
            if (e.isExit) {
                dl->AddCircle(p, r * 1.6f, IM_COL32(255, 130, 0, 255), 12, 2.0f);
            }
            if (e.localId == st.selectedEntity) {
                drawHexOutline(dl, p, r * 1.9f, IM_COL32(255, 255, 255, 255));
                char label[64];
                std::snprintf(label, sizeof(label), "#%d %s %u:%u", e.localId, toString(e.kind).c_str(),
                              e.proto.type, e.proto.num);
                dl->AddText(ImVec2(p.x + r * 2.0f, p.y - r * 2.0f), IM_COL32(255, 255, 255, 255),
                            label);
            }
        }
    }

    dl->PopClipRect();
}

}  // namespace f2mt
