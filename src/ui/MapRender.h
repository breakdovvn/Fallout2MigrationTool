#pragma once

#include "core/Model.h"
#include "project/MigrationProject.h"

struct ImDrawList;
struct ImVec2;

namespace f2mt {

struct Camera {
    float zoom = 0.35f;
    float panX = 0.0f;
    float panY = 0.0f;

    ImVec2 worldToScreen(float wx, float wy) const;
    void screenToWorld(float sx, float sy, float& wx, float& wy) const;
};

// Размер карты в «мировых» единицах.
void mapWorldSize(const Location& loc, float& w, float& h);

// Подобрать зум/пан так, чтобы карта вписалась в область.
void fitCameraForMap(const Location& loc, float canvasW, float canvasH, Camera& cam);

// Схематичный рендер карты из NormalizedModel.
void drawMap(ImDrawList* dl, const ImVec2& origin, float canvasW, float canvasH, const Location& loc,
             const ProjectState& st, const Camera& cam);

}  // namespace f2mt
