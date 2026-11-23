// main.cpp — точка входа HomeLedger «Домашняя бухгалтерия» v1.0.0
//
// Использование:
//   HomeLedger [--data КАТАЛОГ] [--demo] [--no-color] [--force-color] [--help] [--version]
#include <cstdlib>
#include <cstring>
#include <iostream>

#include "App.h"
#include "Console.h"

namespace {
const char* kVersion = "1.0.0";

void printHelp() {
    std::cout << "HomeLedger " << kVersion << " — домашняя бухгалтерия\n\n"
              << "Параметры:\n"
              << "  --data КАТАЛОГ  каталог с файлами данных (по умолчанию ./data)\n"
              << "  --demo          заполнить пустой каталог демонстрационными данными за 6 месяцев\n"
              << "  --no-color      выводить без цвета\n"
              << "  --force-color   принудительно включить ANSI-цвета в выводе\n"
              << "  --help          эта справка\n"
              << "  --version       версия программы\n";
}
}  // namespace

int main(int argc, char* argv[]) {
    std::string dataDir = "data";
    bool demo = false, color = std::getenv("NO_COLOR") == nullptr, forceColor = false;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--data") && i + 1 < argc) dataDir = argv[++i];
        else if (!std::strcmp(argv[i], "--demo")) demo = true;
        else if (!std::strcmp(argv[i], "--no-color")) {
            color = false;
            forceColor = false;
        } else if (!std::strcmp(argv[i], "--force-color")) {
            color = true;
            forceColor = true;
        } else if (!std::strcmp(argv[i], "--version")) {
            std::cout << "HomeLedger " << kVersion << "\n";
            return 0;
        } else {
            console::init(false);
            printHelp();
            return std::strcmp(argv[i], "--help") ? 1 : 0;
        }
    }
    console::init(color, forceColor);
    App app{Storage(dataDir)};
    return app.run(demo);
}
