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
// Проекция гекса: решётка FOnline GeometryHelper::GetHexPos (MAP_HEX_WIDTH=32,
// MAP_HEX_LINE_HEIGHT=12). Сдвиг (4784, -1188) подобран так, чтобы при hx<200 результат
// в точности совпадал с Fallout tileToScreenXY (W=200) — это выравнивает объекты с
// квадратной сеткой пола в исходных .map. При этом формула корректна и для карт TLA
// шириной >200 гексов, где Fallout-формула ломается (v3 уходит в минус).
void gameObjProjection(int hx, int hy, int& sx, int& sy) {
    const int hx2 = (hx < 0 ? hx - 1 : hx) / 2;  // floor(hx/2)
    sx = 4784 + 16 * hy - 32 * hx + 16 * hx2;
    sy = -1188 + 12 * hy + 12 * hx2;
}

// Тайлы: squareTileToScreenXY + оффсеты квадратной сетки (_square_offx=_tile_offx-16, _square_offy=_tile_offy-2).
void gameTileProjection(int tx, int ty, int& sx, int& sy) {
    const int v5 = 99 - tx;
    const int v6 = ty;
    sx = -16 + 48 * v5 + 32 * v6;
    sy = -2 - 12 * v5 + 24 * v6;
}

uint32_t kindMarkerColor(ProtoType k) {
    switch (k) {
        case ProtoType::Critter: return IM_COL32(90, 210, 100, 255);
        case ProtoType::Item: return IM_COL32(240, 210, 70, 255);
        case ProtoType::Scenery: return IM_COL32(80, 180, 230, 255);
        case ProtoType::Wall: return IM_COL32(200, 200, 200, 255);
        case ProtoType::Tile: return IM_COL32(150, 110, 70, 255);
        default: return IM_COL32(220, 110, 210, 255);
    }
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
                    const GameSpriteItem& it, const Camera& cam, SpriteManager* sprites) {
    if (it.marker) {
        const ImVec2 a = cam.worldToScreen(it.wx, it.wy);
        const ImVec2 p(origin.x + a.x, origin.y + a.y);
        const float s = std::max(4.0f, it.w * cam.zoom);
        dl->AddRectFilled(ImVec2(p.x - s * 0.5f, p.y - s * 0.5f),
                          ImVec2(p.x + s * 0.5f, p.y + s * 0.5f), it.markerColor);
        return;
    }
    SDL_Texture* tex = static_cast<SDL_Texture*>(it.tex);
    if (it.animated && sprites != nullptr && !it.path.empty()) {
        const double t = ImGui::GetTime();
        int frame = static_cast<int>(t * it.fps) % it.framesPerDir;
        if (frame < 0) frame = 0;
        if (SDL_Texture* an = sprites->texture(it.path, frame, it.dir)) tex = an;
    }
    if (tex == nullptr || it.w <= 0 || it.h <= 0) return;
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
    dl->AddImage((ImTextureID)(intptr_t)tex, pmin, pmax);
}

void drawGameCached(ImDrawList* dl, const ImVec2& origin, const ImVec2& clipMin, const ImVec2& clipMax,
                    const GameRenderCache& cache, const Camera& cam, bool showRoofs, bool showExits,
                    SpriteManager* sprites) {
    for (const auto& it : cache.floors) drawSpriteItem(dl, origin, clipMin, clipMax, it, cam, sprites);

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

    for (const auto& it : cache.objects) drawSpriteItem(dl, origin, clipMin, clipMax, it, cam, sprites);
    if (showRoofs) {
        for (const auto& it : cache.roofs) drawSpriteItem(dl, origin, clipMin, clipMax, it, cam, sprites);
    }
}
}  // namespace

void buildGameCache(const Location& loc, const ProjectState& st, SpriteManager* sprites,
                    GameRenderCache& cache) {
    const int elevation = st.elevation;
    const int kindMask = st.kindMask;
    const bool showContents = st.showContents;
    auto animOn = [&](int localId) {
        if (st.animationsOn) return true;
        for (int id : st.animIds) {
            if (id == localId) return true;
        }
        return false;
    };
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
            gameObjProjection(e.x, e.y - 200 * e.elevation, ex, ey);
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
        if (e.fidType == 0 && e.fidNum == 0 && e.artPath.empty()) continue;
        if (e.targetHide) continue;  // FOnline AlwaysHideSprite — служебный блокер, не рисуется
        if (kindMask >= 0 && !(kindMask & (1 << static_cast<int>(e.kind)))) continue;

        int dir = e.dir;
        if (dir >= 6) dir = (((dir % 360) + 30) / 60) % 6;  // FOnline Dir в градусах -> 0..5
        if (dir < 0 || dir >= 6) dir = 0;
        const bool isTargetTile = e.kind == ProtoType::Tile && !e.artPath.empty();
        const auto s = !e.artPath.empty() ? sprites->spriteByPath(e.artPath, dir)
                                          : sprites->sprite(e.fidType, e.fidNum, 0, dir);
        // Мультигексные тайлы FOnline: спрайт рисуется в каждом гексе сетки
        // (клиент: MapView::AddItemToField -> DrawHexItem для каждой записи MultihexMesh).
        int hexCount = 1;
        const bool expandMesh = e.targetDrawMesh && !e.targetMesh.empty();
        if (expandMesh) hexCount += static_cast<int>(e.targetMesh.size());
        for (int h = 0; h < hexCount; ++h) {
            const int hx = h == 0 ? e.x : e.targetMesh[static_cast<size_t>(h - 1)].first;
            const int hy = h == 0 ? e.y : e.targetMesh[static_cast<size_t>(h - 1)].second;
            int sx, sy;
            gameObjProjection(hx, hy - 200 * e.elevation, sx, sy);
            const float ax = float(sx) + 16.0f + float(s.fx);
            const float ay = float(sy) + 8.0f + float(s.fy);
            if (s.tex == nullptr) {
                // Нет арта — рисуем маркер, чтобы объект не пропадал.
                GameSpriteItem gi{float(sx) + 16.0f, float(sy) + 8.0f, 12, 6, nullptr, true,
                                  e.localId};
                gi.marker = true;
                gi.markerColor = kindMarkerColor(e.kind);
                if (isTargetTile) {
                    (e.targetIsRoof ? cache.roofs : cache.floors).push_back(gi);
                    acc(gi.wx, gi.wy, float(gi.w), float(gi.h));
                } else {
                    tmp.push_back({&e, float(sy), gi});
                }
                continue;
            }
            GameSpriteItem gi{ax, ay, s.w, s.h, s.tex, true, e.localId};
            gi.path = s.path;
            gi.framesPerDir = s.framesPerDir;
            gi.fps = s.fps;
            gi.dir = dir;
            gi.animated = (s.framesPerDir > 1 && s.fps > 0) && animOn(e.localId);
            if (isTargetTile) {
                // Тайлы FOnline — это пол/крыша: отдельный слой, поверх не рисуем как объект.
                (e.targetIsRoof ? cache.roofs : cache.floors).push_back(gi);
                acc(ax - s.w * 0.5f, ay - s.h, float(s.w), float(s.h));
                continue;
            }
            tmp.push_back({&e, float(sy), gi});
            acc(ax - s.w * 0.5f, ay - s.h, float(s.w), float(s.h));
        }
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
            gameObjProjection(e.x, e.y - 200 * e.elevation, sx, sy);
            const float pax = float(sx) + 16.0f;
            const float pay = float(sy) + 8.0f;
            int idx = 0;
            for (const auto& inv : e.inventory) {
                const auto s = sprites->sprite(inv.fidType, inv.fidNum, 0, 0);
                if (s.tex != nullptr) {
                    const float dx = float(idx % 4) * 3.0f;
                    const float dy = -float(idx / 4) * 3.0f;
                    GameSpriteItem gi{pax + dx, pay + dy, s.w, s.h, s.tex, true, -1};
                    gi.path = s.path;
                    gi.framesPerDir = s.framesPerDir;
                    gi.fps = s.fps;
                    gi.dir = 0;
                    cache.objects.push_back(gi);
                }
                ++idx;
            }
        }
    }

    if (first) { cache.minx = cache.miny = 0; cache.maxx = cache.maxy = 100; }
}

bool gameFitBounds(const GameRenderCache& cache, float& minx, float& miny, float& maxx, float& maxy) {
    // Вписываем по содержимому карты (scroll-blocker'ы больше не влияют на камеру).
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
    gameObjProjection(e.x, e.y - 200 * e.elevation, sx, sy);
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
        buildGameCache(loc, st, sprites, cache);
    }

    float minx = 0, miny = 0, maxx = 100, maxy = 100;
    gameFitBounds(cache, minx, miny, maxx, maxy);
    const ImVec2 mapA = cam.worldToScreen(minx, miny);
    const ImVec2 mapB = cam.worldToScreen(maxx, maxy);
    dl->AddRectFilled(ImVec2(origin.x + mapA.x, origin.y + mapA.y),
                      ImVec2(origin.x + mapB.x, origin.y + mapB.y), IM_COL32(18, 18, 22, 255));

    if (cache.valid) {
        drawGameCached(dl, origin, clipMin, clipMax, cache, cam, st.showRoofs, st.showExits, sprites);
    }

    dl->PopClipRect();
}

}  // namespace f2mt
