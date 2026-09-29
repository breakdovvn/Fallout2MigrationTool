#include "ui/MapRender.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

#include "render/Graphics.h"

namespace f2mt {

namespace {
// Проекция «как в игре»: falloute2CE tile.cc::tileToScreenXY (origin 0).
void gameObjProjection(int hx, int hy, int& sx, int& sy) {
    const int W = 200;
    const int v3 = W - 1 - hx;
    const int v4 = hy;
    sx = 0;
    sy = 0;
    const int v5 = v3 / -2;
    sx += 48 * (v3 / 2);
    sy += 12 * v5;
    if (v3 & 1) {
        if (v3 <= 0) { sx -= 16; sy += 12; }
        else { sx += 32; }
    }
    sx += 16 * v4;
    sy += 12 * v4;
}

// Тайлы: squareTileToScreenXY + оффсеты квадратной сетки (_square_offx=_tile_offx-16, _square_offy=_tile_offy-2).
void gameTileProjection(int tx, int ty, int& sx, int& sy) {
    const int v5 = 99 - tx;
    const int v6 = ty;
    sx = -16 + 48 * v5 + 32 * v6;
    sy = -2 - 12 * v5 + 24 * v6;
}

int objTypePriority(ProtoType k) {
    switch (k) {
        case ProtoType::Wall: return 0;
        case ProtoType::Scenery: return 1;
        case ProtoType::Item: return 2;
        case ProtoType::Critter: return 3;
        default: return 4;
    }
}

// FRM закреплён низ-центром: left = ax - w/2, top = ay - h (object.cc:4916-4917).
void drawSpriteItem(ImDrawList* dl, const ImVec2& origin, const ImVec2& clipMin, const ImVec2& clipMax,
                    const GameSpriteItem& it, const Camera& cam) {
    if (it.tex == nullptr || it.w <= 0 || it.h <= 0) return;
    const ImVec2 a = cam.worldToScreen(it.wx, it.wy);
    const float w = it.w * cam.zoom;
    const float h = it.h * cam.zoom;
    ImVec2 pmin;
    if (it.bottomAnchor) {
        pmin = ImVec2(origin.x + a.x - w * 0.5f, origin.y + a.y - h);
    } else {
        pmin = ImVec2(origin.x + a.x, origin.y + a.y);
    }
    const ImVec2 pmax(pmin.x + w, pmin.y + h);
    if (pmax.x < clipMin.x || pmax.y < clipMin.y || pmin.x > clipMax.x || pmin.y > clipMax.y) return;
    dl->AddImage((ImTextureID)(intptr_t)it.tex, pmin, pmax);
}

void drawGameCached(ImDrawList* dl, const ImVec2& origin, const ImVec2& clipMin, const ImVec2& clipMax,
                    const GameRenderCache& cache, const Camera& cam, bool showRoofs, bool showExits) {
    for (const auto& it : cache.floors) drawSpriteItem(dl, origin, clipMin, clipMax, it, cam);

    // Сетки выходов (поверх пола, под объектами).
    for (const auto& xc : cache.exits) {
        if (!showExits) break;  // тумблер скрытия сеток
        const ImVec2 c = cam.worldToScreen(xc.wx, xc.wy);
        const ImVec2 p(origin.x + c.x, origin.y + c.y);
        const float s = std::max(3.0f, 22.0f * cam.zoom);
        if (p.x + s < clipMin.x || p.y + s < clipMin.y || p.x - s > clipMax.x || p.y - s > clipMax.y) {
            continue;
        }
        const ImU32 fill = xc.green ? IM_COL32(0, 200, 60, 110) : IM_COL32(210, 40, 40, 110);
        const ImU32 line = xc.green ? IM_COL32(0, 255, 80, 230) : IM_COL32(255, 60, 60, 230);
        const ImVec2 pts[4] = {ImVec2(p.x, p.y - s), ImVec2(p.x + s, p.y), ImVec2(p.x, p.y + s),
                               ImVec2(p.x - s, p.y)};
        dl->AddConvexPolyFilled(pts, 4, fill);
        dl->AddPolyline(pts, 4, line, ImDrawFlags_Closed, 1.5f);
    }

    for (const auto& it : cache.objects) drawSpriteItem(dl, origin, clipMin, clipMax, it, cam);
    if (showRoofs) {
        for (const auto& it : cache.roofs) drawSpriteItem(dl, origin, clipMin, clipMax, it, cam);
    }
}
}  // namespace

void buildGameCache(const Location& loc, int elevation, int kindMask, bool showContents,
                    SpriteManager* sprites, GameRenderCache& cache) {
    cache.floors.clear();
    cache.objects.clear();
    cache.roofs.clear();
    cache.exits.clear();
    cache.loc = &loc;
    cache.elevation = elevation;
    cache.kindMask = kindMask;
    cache.showContents = showContents;
    cache.hasScroll = false;
    cache.valid = true;
    if (sprites == nullptr) return;

    bool first = true;
    auto acc = [&](float x, float y, float w, float h) {
        if (first) { cache.minx = x; cache.miny = y; cache.maxx = x + w; cache.maxy = y + h; first = false; }
        cache.minx = std::min(cache.minx, x);
        cache.miny = std::min(cache.miny, y);
        cache.maxx = std::max(cache.maxx, x + w);
        cache.maxy = std::max(cache.maxy, y + h);
    };

    // Floor tiles.
    for (int ty = 0; ty < loc.grid.tileHeight; ++ty) {
        for (int tx = 0; tx < loc.grid.tileWidth; ++tx) {
            const TileCell* cell = loc.tileAt(elevation, tx, ty);
            if (cell == nullptr || cell->tileId <= 1) continue;
            const auto s = sprites->tileSprite(cell->tileId, 0);
            if (s.tex == nullptr) continue;
            int sx, sy;
            gameTileProjection(tx, ty, sx, sy);
            cache.floors.push_back({float(sx), float(sy), s.w, s.h, s.tex, false, -1});
            acc(float(sx), float(sy), float(s.w), float(s.h));
        }
    }
    // Roofs.
    for (int ty = 0; ty < loc.grid.tileHeight; ++ty) {
        for (int tx = 0; tx < loc.grid.tileWidth; ++tx) {
            const TileCell* cell = loc.tileAt(elevation, tx, ty);
            if (cell == nullptr || cell->roofId <= 1) continue;
            const auto s = sprites->tileSprite(cell->roofId, 0);
            if (s.tex == nullptr) continue;
            int sx, sy;
            gameTileProjection(tx, ty, sx, sy);
            cache.roofs.push_back({float(sx), float(sy - 96), s.w, s.h, s.tex, false, -1});
            acc(float(sx), float(sy - 96), float(s.w), float(s.h));
        }
    }

    struct SortKey { const Entity* e; float depth; GameSpriteItem item; };
    std::vector<SortKey> tmp;
    tmp.reserve(loc.entities.size());
    for (const auto& e : loc.entities) {
        if (e.elevation != elevation) continue;

        // Scroll blockers (служебные): задают игровую зону и НЕ рисуются.
        const bool isScrollBlocker = (e.proto.type == 2 && e.proto.num == 0x0158) ||
                                     (e.proto.type == 5 && e.proto.num == 0x000C) ||
                                     (e.fidType == 5 && e.fidNum == 1);
        if (isScrollBlocker) {
            if (!cache.hasScroll) {
                cache.hasScroll = true;
                cache.scrollMinX = cache.scrollMaxX = e.x;
                cache.scrollMinY = cache.scrollMaxY = e.y;
            } else {
                cache.scrollMinX = std::min(cache.scrollMinX, e.x);
                cache.scrollMaxX = std::max(cache.scrollMaxX, e.x);
                cache.scrollMinY = std::min(cache.scrollMinY, e.y);
                cache.scrollMaxY = std::max(cache.scrollMaxY, e.y);
            }
            continue;
        }

        // Misc 16..23 — сетки выходов: рисуем своей заливкой (зелёная/красная),
        // FID-арт неиспользуемый.
        if (e.proto.type == 5 && e.proto.num >= 16 && e.proto.num <= 23) {
            int ex, ey;
            gameObjProjection(e.x, e.y, ex, ey);
            GameRenderCache::ExitCell cell;
            cell.wx = float(ex) + 16.0f;
            cell.wy = float(ey) + 8.0f;
            const uint32_t d = e.exitDestMap;
            cell.green = (d != 0u && d != 0xFFFFFFFFu && d != 0xFFFFFFFEu && d < 100000u);
            cell.targetMap = cell.green ? static_cast<int>(d) : -1;
            cell.localId = e.localId;
            cache.exits.push_back(cell);
            continue;
        }
        if (e.fidType == 0 && e.fidNum == 0) continue;
        if (kindMask >= 0 && !(kindMask & (1 << static_cast<int>(e.kind)))) continue;

        const int dir = (e.dir >= 0 && e.dir < 6) ? e.dir : 0;
        const auto s = sprites->sprite(e.fidType, e.fidNum, 0, dir);
        if (s.tex == nullptr) continue;
        int sx, sy;
        gameObjProjection(e.x, e.y, sx, sy);
        const float ax = float(sx) + 16.0f + float(s.fx);
        const float ay = float(sy) + 8.0f + float(s.fy);
        tmp.push_back({&e, float(sy), {ax, ay, s.w, s.h, s.tex, true, e.localId}});
        acc(ax - s.w * 0.5f, ay - s.h, float(s.w), float(s.h));
    }
    std::sort(tmp.begin(), tmp.end(), [](const SortKey& a, const SortKey& b) {
        if (a.depth != b.depth) return a.depth < b.depth;
        if (a.item.wx != b.item.wx) return a.item.wx < b.item.wx;
        return objTypePriority(a.e->kind) < objTypePriority(b.e->kind);
    });
    cache.objects.reserve(tmp.size());
    for (auto& s : tmp) cache.objects.push_back(s.item);

    // Содержимое контейнеров (если включено) — рисуем поверх контейнера.
    if (showContents) {
        for (const auto& e : loc.entities) {
            if (e.elevation != elevation || e.inventory.empty()) continue;
            int sx, sy;
            gameObjProjection(e.x, e.y, sx, sy);
            const float pax = float(sx) + 16.0f;
            const float pay = float(sy) + 8.0f;
            int idx = 0;
            for (const auto& inv : e.inventory) {
                const auto s = sprites->sprite(inv.fidType, inv.fidNum, 0, 0);
                if (s.tex != nullptr) {
                    const float dx = float(idx % 4) * 3.0f;
                    const float dy = -float(idx / 4) * 3.0f;
                    cache.objects.push_back({pax + dx, pay + dy, s.w, s.h, s.tex, true, -1});
                }
                ++idx;
            }
        }
    }

    if (first) { cache.minx = cache.miny = 0; cache.maxx = cache.maxy = 100; }
}

bool gameFitBounds(const GameRenderCache& cache, float& minx, float& miny, float& maxx, float& maxy) {
    if (cache.hasScroll) {
        auto pr = [](int hx, int hy, float& x, float& y) {
            int sx, sy;
            gameObjProjection(hx, hy, sx, sy);
            x = float(sx) + 16.0f;
            y = float(sy) + 8.0f;
        };
        float x0, y0, x1, y1, x2, y2, x3, y3;
        pr(cache.scrollMinX, cache.scrollMinY, x0, y0);
        pr(cache.scrollMaxX, cache.scrollMinY, x1, y1);
        pr(cache.scrollMinX, cache.scrollMaxY, x2, y2);
        pr(cache.scrollMaxX, cache.scrollMaxY, x3, y3);
        minx = std::min(std::min(x0, x1), std::min(x2, x3));
        maxx = std::max(std::max(x0, x1), std::max(x2, x3));
        miny = std::min(std::min(y0, y1), std::min(y2, y3));
        maxy = std::max(std::max(y0, y1), std::max(y2, y3));
        minx -= 120.0f;
        maxx += 120.0f;
        miny -= 160.0f;
        maxy += 48.0f;
        return true;
    }
    if (!cache.valid) return false;
    minx = cache.minx;
    miny = cache.miny;
    maxx = cache.maxx;
    maxy = cache.maxy;
    return true;
}

int pickExitCell(const GameRenderCache& cache, const Camera& cam, float screenX, float screenY,
                 float originX, float originY) {
    if (!cache.valid || cam.zoom <= 0.0f) return -1;
    const float wx = (screenX - originX - cam.panX) / cam.zoom;
    const float wy = (screenY - originY - cam.panY) / cam.zoom;
    const float s = 22.0f;
    for (int i = 0; i < static_cast<int>(cache.exits.size()); ++i) {
        const auto& xc = cache.exits[i];
        if (std::abs(wx - xc.wx) <= s && std::abs(wy - xc.wy) <= s) return i;
    }
    return -1;
}

bool gameEntityWorld(const Entity& e, float& wx, float& wy) {
    int sx, sy;
    gameObjProjection(e.x, e.y, sx, sy);
    wx = float(sx) + 16.0f;
    wy = float(sy) + 8.0f;
    return true;
}

int pickGameObject(const GameRenderCache& cache, const Camera& cam, float screenX, float screenY,
                   float originX, float originY) {
    if (!cache.valid || cam.zoom <= 0.0f) return -1;
    const float wx = (screenX - originX - cam.panX) / cam.zoom;
    const float wy = (screenY - originY - cam.panY) / cam.zoom;
    for (auto it = cache.objects.rbegin(); it != cache.objects.rend(); ++it) {
        const auto& s = *it;
        if (s.localId < 0 || s.w <= 0 || s.h <= 0) continue;
        const float left = s.wx - s.w * 0.5f;
        const float top = s.wy - s.h;
        if (wx >= left && wx <= left + s.w && wy >= top && wy <= s.wy) return s.localId;
    }
    return -1;
}

ImVec2 Camera::worldToScreen(float wx, float wy) const {
    return ImVec2(panX + wx * zoom, panY + wy * zoom);
}
void Camera::screenToWorld(float sx, float sy, float& wx, float& wy) const {
    wx = (sx - panX) / zoom;
    wy = (sy - panY) / zoom;
}

void drawMap(ImDrawList* dl, const ImVec2& origin, float canvasW, float canvasH, const Location& loc,
             const ProjectState& st, const Camera& cam, SpriteManager* sprites,
             GameRenderCache& cache) {
    const ImVec2 clipMin = origin;
    const ImVec2 clipMax = ImVec2(origin.x + canvasW, origin.y + canvasH);
    dl->PushClipRect(clipMin, clipMax, true);

    if (!cache.valid || cache.loc != &loc || cache.elevation != st.elevation ||
        cache.kindMask != st.kindMask || cache.showContents != st.showContents) {
        buildGameCache(loc, st.elevation, st.kindMask, st.showContents, sprites, cache);
    }

    float minx = 0, miny = 0, maxx = 100, maxy = 100;
    gameFitBounds(cache, minx, miny, maxx, maxy);
    const ImVec2 mapA = cam.worldToScreen(minx, miny);
    const ImVec2 mapB = cam.worldToScreen(maxx, maxy);
    dl->AddRectFilled(ImVec2(origin.x + mapA.x, origin.y + mapA.y),
                      ImVec2(origin.x + mapB.x, origin.y + mapB.y), IM_COL32(18, 18, 22, 255));

    if (cache.valid) {
        drawGameCached(dl, origin, clipMin, clipMax, cache, cam, st.showRoofs, st.showExits);
    }

    dl->PopClipRect();
}

}  // namespace f2mt
