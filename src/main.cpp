#include <SDL.h>  // подменяет main -> SDL_main (нужно для SDL2main/WinMain)

#include <string>

#include "ui/App.h"

int main(int argc, char** argv) {
    f2mt::AppConfig cfg;
    bool headless = false;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--headless") {
            headless = true;
        } else if (a == "--dat2" && i + 1 < argc) {
            cfg.dat2Exe = argv[++i];
        } else if (a == "--dat" && i + 1 < argc) {
            cfg.gameDat = argv[++i];
        } else if (a == "--project" && i + 1 < argc) {
            cfg.projectDir = argv[++i];
        } else if (a == "--map" && i + 1 < argc) {
            cfg.mapEntry = argv[++i];
        } else if (a == "--id" && i + 1 < argc) {
            cfg.locationId = argv[++i];
        } else if (a == "--switch-test" && i + 1 < argc) {
            cfg.testSwitchMap = argv[++i];
        } else if (a == "--ru-text" && i + 1 < argc) {
            cfg.russianTextDir = argv[++i];
        } else if (a == "--ru-dat" && i + 1 < argc) {
            cfg.russianDat = argv[++i];
        }
    }
    f2mt::App app;
    return headless ? app.runHeadless(cfg) : app.run(cfg);
}
