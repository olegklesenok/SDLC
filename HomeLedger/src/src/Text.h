// Text.h — работа со строками UTF-8, форматом CSV и HTML-экранированием
#pragma once
#include <string>
#include <vector>

size_t utf8Length(const std::string& s);                       // число символов (а не байт)
std::string utf8Truncate(const std::string& s, size_t chars);  // обрезать до chars символов с «…»
std::string padRight(const std::string& s, size_t width);      // выравнивание по левому краю
std::string padLeft(const std::string& s, size_t width);       // выравнивание по правому краю
std::string trim(const std::string& s);
std::string toLowerAscii(const std::string& s);
std::string toLowerUtf8(const std::string& s);                 // нижний регистр для ASCII и русской/белорусской кириллицы
bool containsIgnoreCaseUtf8(const std::string& haystack, const std::string& needle);
bool startsWithIgnoreCaseUtf8(const std::string& s, const std::string& prefix);

// CSV с разделителем ';' (открывается в Excel с русской локалью без мастера импорта)
std::vector<std::string> csvSplit(const std::string& line, char sep = ';');
std::string csvField(const std::string& value, char sep = ';');

// Экранирование спецсимволов HTML для безопасной генерации автономного отчёта
std::string htmlEscape(const std::string& s);
