# Fallout 2 → FOnline Migration Tool

Инструмент поэтапной миграции локаций Fallout 2 в формат FOnline (TLA).

Текущий этап — **MVP-1**: чтение `.map` из `master.dat`, построение нормализованной
модели локации и визуальный просмотр карты «как в игре». Экспорт в FOnline ещё не
реализован.

## Возможности

- Провижининг: распаковка нужных файлов из `.dat` внешним `dat2.exe` в локальный `projects/<Id>.migration/source/raw`.
- Чтение `*.map` Fallout 2 → нормализованная модель (`Location`, `Entity`, `SourceRef`).
- Разрешение прототипов через `*.lst` и `*.pro` (`ProtoResolver`).
- Тексты прототипов из `*.msg` (CP1251 → UTF-8), переключение RU/EN в интерфейсе.
- Отрисовка карты: пол, объекты, крыши, сетки выходов (FRM + `color.pal`), масштаб и панорама.
- Панели: список карт и объектов, инспектор, найденные проблемы разбора.
- Headless-режим: разбор и сводка без окна (для проверок и тестов).

## Требования

- Windows, Visual Studio 2022 (toolset C++20), CMake ≥ 3.24.
- Внешний `dat2.exe` (Fallout DAT packer/unpacker) и файлы игры (`master.dat`, `critter.dat`).
- SDL2 уже вендорен в `third_party/SDL2` (x64), отдельно ставить не нужно.

## Сборка

```bat
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config RelWithDebInfo
```

Быстрый вариант — `start.bat` (сборка и запуск одной командой).

## Запуск

```bat
build\RelWithDebInfo\f2mt_viewer.exe              REM GUI
build\RelWithDebInfo\f2mt_viewer.exe --headless   REM разбор + отчёт без окна
```

Параметры командной строки:

| Параметр | Назначение |
| --- | --- |
| `--headless` | Разбор и сводка без окна |
| `--dat2 <exe>` | Путь к `dat2.exe` |
| `--dat <file>` | Путь к `master.dat` |
| `--project <dir>` | Каталог проекта миграции |
| `--map <entry>` | Путь к карте внутри `.dat` |
| `--id <name>` | Идентификатор локации |
| `--ru-text <dir>` | Каталог русских `pro_*.msg` |
| `--ru-dat <file>` | Русский `master.dat` |
| `--switch-test <map>` | Диагностика: вторая карта для переключения |

Пути по умолчанию (`AppConfig` в `src/ui/App.h`) указывают на локальные каталоги
`E:\Games\...` — при необходимости измените их или задайте через параметры.

## Структура

```
src/
  core/       нормализованная модель (Model) и чтение .fomap (FomapReader)
  source/     чтение исходных данных: .map, .pro, .lst, .msg, .dat (DatProvisioner)
  render/     палитра, FRM-спрайты, SDL-текстуры (Graphics)
  ui/         Dear ImGui интерфейс и отрисовка карты (App, MapRender)
  project/    сессионное состояние и каталог проекта миграции (MigrationProject)
third_party/SDL2   вендоренный SDL2 (x64)
projects/          рабочие данные (создаются во время работы, не в репозитории)
```

Каталог `projects/` содержит распакованные из `.dat` игровые данные и результаты
разбора. Он создаётся во время работы и исключён из репозитория (см. `.gitignore`).

## Принцип работы

Исследование → архитектура → MVP → реализация → тест → следующий этап.
Подробности процесса — в `PROJECT_RULES.md`.
