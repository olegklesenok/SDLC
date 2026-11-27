// Date.h — календарная дата без времени (григорианский календарь)
#pragma once
#include <string>

struct Date {
    int y = 1970;
    int m = 1;
    int d = 1;

    static Date today();
    // Разбор даты: "ГГГГ-ММ-ДД", "ДД.ММ.ГГГГ", "ДД.ММ" (текущий год).
    // Пустая строка — сегодняшняя дата. Возвращает false при ошибке.
    static bool parse(const std::string& text, Date& out);
    static int daysInMonth(int y, int m);
    static bool isValid(int y, int m, int d);
    static Date fromDays(long days);
    static const char* weekdayName(int dow);
    static const char* weekdayShort(int dow);

    long toDays() const;            // номер дня от 1970-01-01
    int dayOfWeek() const;          // 0 = понедельник .. 6 = воскресенье
    Date addDays(long n) const { return fromDays(toDays() + n); }
    Date firstOfMonth() const { return Date{y, m, 1}; }
    Date lastOfMonth() const { return Date{y, m, daysInMonth(y, m)}; }
    Date addMonths(int n) const;    // первое число месяца через n месяцев
    std::string iso() const;        // 2026-10-30 — для файлов
    std::string yearMonth() const;  // 2026-10 — ключ месяца
    std::string str() const;        // 30.10.2026 — для экрана
    std::string monthName() const;  // «Октябрь 2026»

    bool operator<(const Date& o) const { return toDays() < o.toDays(); }
    bool operator<=(const Date& o) const { return toDays() <= o.toDays(); }
    bool operator>(const Date& o) const { return toDays() > o.toDays(); }
    bool operator==(const Date& o) const { return y == o.y && m == o.m && d == o.d; }
    bool sameMonth(const Date& o) const { return y == o.y && m == o.m; }
};
