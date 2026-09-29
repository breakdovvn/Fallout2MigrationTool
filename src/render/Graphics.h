#pragma once

#include <array>
#include <cstdint>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include "core/Model.h"

struct SDL_Texture;
struct SDL_Renderer;

namespace f2mt {

// Палитра Fallout: первые 768 байт color.pal (256 RGB).
class Palette {
public:
    bool load(const std::string& path);
    bool loaded() const { return _loaded; }
    void setBrightness(float b) { _brightness = b; }
    float brightness() const { return _brightness; }
    // RGBA (ARGB8888 как uint32). Индекс 0 -> прозрачный.
    uint32_t rgba(uint8_t index) const;

private:
    std::array<uint8_t, 768> _rgb{};
    bool _loaded = false;
    float _brightness = 1.0f;  // 1.0 = день; <1 = темнее (ночь)
};

// Декодированный кадр FRM (индексы -> RGBA).
struct DecodedFrame {
    int w = 0;
    int h = 0;
    int x = 0;  // смещение кадра (не используется для карты)
    int y = 0;
    std::vector<uint32_t> rgba;
};

// FRM-спрайт (несжатые кадры Fallout 2).
class FrmImage {
public:
    bool load(const std::string& path, const Palette& pal);
    bool valid() const { return _valid; }
    int framesPerDir() const { return _framesPerDir; }
    int dirCount() const { return _dirCount; }
    int fps() const { return _fps; }
    int16_t xOffset(int dir) const;
    int16_t yOffset(int dir) const;
    const DecodedFrame* frame(int frameIndex, int dir) const;

private:
    bool _valid = false;
    int _framesPerDir = 1;
    int _fps = 0;
    int _dirCount = 6;
    std::array<int16_t, 6> _xOff{};
    std::array<int16_t, 6> _yOff{};
    std::vector<DecodedFrame> _frames;  // dir * framesPerDir + frame
};

class DatProvisioner;

// Резолв FID -> FRM, декодирование и SDL-текстуры с кэшем.
// Вся работа с артом идёт по правилу: сначала локальный raw/, при отсутствии — извлечь из .dat.
class SpriteManager {
public:
    void init(SDL_Renderer* renderer, std::string rawDir, std::string dat2Exe, std::string masterDat,
              std::string critterDat, Palette palette);
    // Яркость палитры (день/ночь): пересобирает кэш спрайтов.
    void setBrightness(float b);
    bool artListsReady() const { return _artReady; }

    // Спрайт по FID (type=0 items, 1 critters, 2 scenery, 3 walls, 4 tiles, ...).
    const FrmImage* frm(uint8_t type, uint16_t num);
    // Тайл по TileId/RoofId (art/tiles).
    const FrmImage* tileFrm(uint16_t tileId);

    SDL_Texture* texture(const std::string& frmPath, int frameIndex, int dir);

    int artListCount(uint8_t type) const;
    std::string artName(uint8_t type, uint16_t num) const;
    int framesPerDir(uint8_t type, uint16_t num);  // >1 => анимируемый спрайт
    // Батч-предзагрузка ВСЕХ артов карты (объекты + пол + крыши) одним/двумя вызовами dat2.
    void ensureMapArts(const Location& loc);

    // Готовая к отрисовке ссылка на спрайт (текстура + размеры + смещение кадра).
    struct SpriteRef {
        SDL_Texture* tex = nullptr;
        int w = 0;
        int h = 0;
        int16_t fx = 0;
        int16_t fy = 0;
        std::string path;        // для анимированных (выбор кадра по времени)
        int framesPerDir = 1;
        int fps = 0;
    };
    SpriteRef sprite(uint8_t type, uint16_t num, int frameIndex, int dir);
    SpriteRef tileSprite(uint16_t tileId, int frameIndex);

private:
    std::string resolveArtLocal(uint8_t type, uint16_t num);  // локальный путь к .frm или ""
    std::string artFrmName(uint8_t type, uint16_t num) const;
    bool extractEntry(const std::string& datPath, const std::string& entry, std::string& outLocal);
    const FrmImage* loadFrm(const std::string& path);

    SDL_Renderer* _renderer = nullptr;
    std::string _rawDir;
    std::string _dat2Exe;
    std::string _masterDat;
    std::string _critterDat;
    Palette _pal;
    bool _artReady = false;

    std::array<std::vector<std::string>, 8> _artLists;
    std::vector<std::string> _critterNames;  // имена FRM из critter.dat (lowercase, sorted)
    std::map<uint32_t, std::string> _pathCache;  // (type<<16)|num -> путь (пусто = нет)
    std::map<std::string, FrmImage> _frmCache;
    std::map<std::string, SDL_Texture*> _texCache;
};

}  // namespace f2mt
