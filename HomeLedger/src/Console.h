// Console.h — цвета, ввод и форматирование вывода в консоль (Windows UTF-8 и POSIX)
#pragma once
#include <string>

namespace console {

enum class Color { Default, Red, Green, Yellow, Blue, Cyan, Gray, Bold };

void init(bool useColor, bool forceColor = false);
std::string paint(const std::string& text, Color c);
bool readLine(const std::string& prompt, std::string& out);
std::string bar(double fraction, int width = 20);  // полоса диаграммы из символов '█' и '░'
std::string line(int width = 76, const char* ch = "─");
void header(const std::string& title);

}  // namespace console
