#pragma once

#include <string>
#include <utility>
#include <vector>

namespace f2mt {

// Объект целевой карты FOnline (.fomap).
struct TargetObject {
    std::string proto;
    int x = 0;
    int y = 0;
    int dir = 0;
    bool critter = false;
    // Гексы мультигексной сетки: клиент рисует спрайт в каждом из них (MapView.cpp:536).
    std::vector<std::pair<int, int>> multihex;
};

// Прочитанная целевая карта (MVP-2). Не смешивается с нормализованной моделью источника.
struct TargetMap {
    bool loaded = false;
    std::string name;
    int sizeX = 0;
    int sizeY = 0;
    int workX = 0;
    int workY = 0;
    std::vector<TargetObject> objects;
};

// Читает текстовый .fomap (секции [ProtoMap], [$Name/Item], [$Name/Critter]).
bool loadFomap(const std::string& path, TargetMap& out);

}  // namespace f2mt
