// App.cpp — экраны консольного приложения HomeLedger v1.0.0
#include "App.h"

#include <algorithm>
#include <cstdio>
#include <initializer_list>
#include <iostream>
#include <random>
#include <sstream>

#include "Analytics.h"
#include "Console.h"
#include "Text.h"

using console::Color;
using console::paint;
using std::cout;

namespace {

std::string signedMoney(Money m, bool income) { return (income ? "+" : "−") + formatMoney(m); }

std::string pct0(double v) {
    char buf[16];
    std::snprintf(buf, sizeof buf, "%.0f %%", v);
    return buf;
}

std::string fmt1Comma(double v) {
    char buf[24];
    std::snprintf(buf, sizeof buf, "%.1f", v);
    for (char* p = buf; *p; ++p)
        if (*p == '.') *p = ',';
    return buf;
}

Color budgetColor(BudgetState s) {
    return s == BudgetState::Over ? Color::Red : s == BudgetState::Near ? Color::Yellow : Color::Green;
}

bool isCmd(const std::string& in, std::initializer_list<const char*> variants) {
    std::string low = toLowerUtf8(in);
    for (const char* v : variants)
        if (low == v) return true;
    return false;
}

// Команда вида "и 12" / "у12" -> номер операции
bool cmdWithId(const std::string& in, std::initializer_list<const char*> prefixes, int& id) {
    for (const char* p : prefixes) {
        std::string pre = p;
        if (in.rfind(pre, 0) == 0) {
            std::string rest = trim(in.substr(pre.size()));
            if (rest.empty()) {
                id = -1;
                return true;
            }
            try {
                size_t pos = 0;
                id = std::stoi(rest, &pos);
                return pos == rest.size();
            } catch (...) {
                return false;
            }
        }
    }
    return false;
}

int parseIntOrDefault(const std::string& s, int def) {
    if (s.empty()) return def;
    try {
        size_t pos = 0;
        int v = std::stoi(s, &pos);
        return pos == s.size() ? v : -1;
    } catch (...) {
        return -1;
    }
}

}  // namespace

// ───────────────────────── общий цикл ─────────────────────────

int App::run(bool demo) {
    std::vector<std::string> warnings;
    storage_.load(ledger_, warnings);
    for (const auto& w : warnings) cout << paint("[!] " + w, Color::Yellow) << "\n";
    std::string err;
    if (!storage_.backup(err)) cout << paint("[!] " + err, Color::Yellow) << "\n";
    if (demo) {
        if (ledger_.operations().empty()) {
            generateDemo();
            cout << paint("Созданы демонстрационные данные за шесть месяцев (включая цели, подписки и долги).",
                          Color::Cyan)
                 << "\n";
        } else {
            cout << paint("Демонстрационные данные не созданы: в каталоге уже есть операции.", Color::Yellow) << "\n";
        }
    }
    if (!ledger_.wallets().empty()) lastWallet_ = ledger_.wallets().back().name;

    while (!eof_) {
        mainScreen();
        std::string choice;
        if (!ask("Команда (0–13, u — отмена, или быстрый ввод -сумма кат комм): ", choice)) break;
        if (!choice.empty() && (choice[0] == '-' || choice[0] == '+')) {
            handleQuickInput(choice);
        } else if (choice == "1") {
            addOperation(OpType::Expense);
        } else if (choice == "2") {
            addOperation(OpType::Income);
        } else if (choice == "3") {
            addTransfer();
        } else if (choice == "4") {
            journal();
        } else if (choice == "5") {
            searchScreen();
        } else if (choice == "6") {
            statistics();
        } else if (choice == "7") {
            budgets();
        } else if (choice == "8") {
            goalsScreen();
        } else if (choice == "9") {
            recurringScreen();
        } else if (choice == "10") {
            debtsScreen();
        } else if (choice == "11") {
            advice();
        } else if (choice == "12") {
            walletsAndCategories();
        } else if (choice == "13") {
            exportAndBackups();
        } else if (isCmd(choice, {"u", "г", "undo", "отмена"})) {
            undoLastAction();
        } else if (choice == "0") {
            break;
        } else {
            cout << paint("Нет такого пункта меню.", Color::Yellow) << "\n";
        }
    }
    cout << "\nДанные сохранены в каталоге «" << storage_.dir() << "». До свидания!\n";
    return 0;
}

bool App::ask(const std::string& prompt, std::string& out) {
    if (eof_) return false;
    if (!console::readLine(prompt, out)) {
        eof_ = true;
        cout << "\n";
        return false;
    }
    return true;
}

bool App::confirm(const std::string& question) {
    std::string a;
    if (!ask(question + " (д/н): ", a)) return false;
    return isCmd(a, {"д", "да", "y", "yes", "l"});
}

void App::pushUndo() {
    undoStack_.push_back(ledger_);
    if (undoStack_.size() > 15) undoStack_.erase(undoStack_.begin());
}

void App::undoLastAction() {
    if (undoStack_.empty()) {
        cout << paint("  История отмены пуста.", Color::Yellow) << "\n";
        return;
    }
    ledger_ = std::move(undoStack_.back());
    undoStack_.pop_back();
    persist();
    cout << paint("  ✓ Последнее изменение отменено и сохранено.", Color::Green) << "\n";
}

void App::persist() {
    std::string err;
    if (!storage_.save(ledger_, err)) cout << paint("[!] Ошибка сохранения: " + err, Color::Red) << "\n";
}

void App::printWrapped(const std::string& text, size_t width, const std::string& indent, bool indentFirst) {
    std::istringstream in(text);
    std::string word, cur;
    bool first = true;
    while (in >> word) {
        if (!cur.empty() && utf8Length(cur) + 1 + utf8Length(word) > width) {
            cout << (first && !indentFirst ? "" : indent) << cur << "\n";
            first = false;
            cur.clear();
        }
        cur += (cur.empty() ? "" : " ") + word;
    }
    if (!cur.empty()) cout << (first && !indentFirst ? "" : indent) << cur << "\n";
}

void App::printOperationTable(const std::vector<Operation>& ops) const {
    cout << paint("  " + padLeft("№", 4) + "  " + padRight("Дата", 10) + "  " + padRight("Тип", 7) + "  " +
                      padRight("Категория", 17) + "  " + padRight("Кошелёк", 9) + "  " + padLeft("Сумма", 10) +
                      "  Комментарий",
                  Color::Gray)
         << "\n";
    for (const auto& o : ops) {
        std::string cat = o.type == OpType::Transfer ? "→ " + o.toWallet : o.category;
        std::string sum = o.type == OpType::Income    ? signedMoney(o.amount, true)
                          : o.type == OpType::Expense ? signedMoney(o.amount, false)
                                                      : formatMoney(o.amount);
        Color c = o.type == OpType::Income ? Color::Green : o.type == OpType::Expense ? Color::Default : Color::Blue;
        cout << "  " << padLeft(std::to_string(o.id), 4) << "  " << o.date.str() << "  "
             << padRight(typeName(o.type), 7) << "  " << padRight(cat, 17) << "  " << padRight(o.wallet, 9) << "  "
             << paint(padLeft(sum, 10), c) << "  " << utf8Truncate(o.comment, 18) << "\n";
    }
}

// ───────────────────────── главный экран ─────────────────────────

void App::mainScreen() {
    Date today = Date::today();
    console::header("HomeLedger v1.0 — Домашняя бухгалтерия" + std::string(24, ' ') + today.str());
    Money total = ledger_.totalBalance();
    cout << " Баланс: " << paint(formatMoney(total) + " BYN", total < 0 ? Color::Red : Color::Bold) << "   (";
    bool first = true;
    for (const auto& w : ledger_.wallets()) {
        cout << (first ? "" : " · ") << w.name << ": " << formatMoney(ledger_.walletBalance(w.name));
        first = false;
    }
    cout << ")\n";
    PeriodStats s = monthStats(ledger_, today);
    FinancialHealth fh = computeFinancialHealth(ledger_, today);
    Color fhColor = fh.score >= 85 ? Color::Green : fh.score >= 65 ? Color::Cyan : fh.score >= 40 ? Color::Yellow : Color::Red;
    cout << " " << today.monthName() << ": доходы " << paint(signedMoney(s.income, true), Color::Green)
         << " · расходы " << paint(signedMoney(s.expense, false), Color::Red) << " · остаток "
         << paint((s.balance() >= 0 ? "+" : "") + formatMoney(s.balance()),
                  s.balance() >= 0 ? Color::Green : Color::Red)
         << "\n";
    cout << " Индекс фин. здоровья: " << paint(std::to_string(fh.score) + "/100 (" + fh.grade + ")", fhColor)
         << " · подушка: " << fmt1Comma(fh.safetyCushionMonths) << " мес.";
    if (!ledger_.goals().empty() || !ledger_.debts().empty()) {
        int activeGoals = 0;
        for (const auto& g : ledger_.goals())
            if (!g.completed) ++activeGoals;
        cout << " · целей: " << activeGoals << " · долги: +" << formatMoney(ledger_.totalOwedToMe()) << " / −"
             << formatMoney(ledger_.totalIOwe());
    }
    cout << "\n";

    auto due = ledger_.dueRecurring(today);
    if (!due.empty()) {
        cout << paint(" [!] Ожидают регулярные платежи (" + std::to_string(due.size()) + " шт.) — меню 9",
                      Color::Yellow)
             << "\n";
    }
    for (const auto& b : budgetStatuses(ledger_, today)) {
        if (b.state == BudgetState::Normal) continue;
        std::string msg = b.state == BudgetState::Over
                              ? "Лимит «" + b.category + "» превышен на " + formatMoney(b.spent - b.limit) + " BYN"
                              : "Лимит «" + b.category + "»: использовано " + pct0(b.usedPercent) + ", осталось " +
                                    formatMoney(b.limit - b.spent) + " BYN";
        cout << paint(" [!] " + msg, budgetColor(b.state)) << "\n";
    }
    cout << "\n";
    const char* left[] = {"1  Добавить расход",
                          "2  Добавить доход",
                          "3  Перевод между кошельками",
                          "4  Журнал операций",
                          "5  Поиск и фильтрация",
                          "6  Статистика и здоровье",
                          "7  Бюджеты (лимиты)"};
    const char* right[] = {" 8  Цели накоплений",
                           " 9  Регулярные платежи",
                           "10  Долги и займы",
                           "11  Советы по экономии",
                           "12  Кошельки и категории",
                           "13  Экспорт (CSV/HTML) и бэкап",
                           " u  Отменить действие ("};
    for (int i = 0; i < 7; ++i) {
        std::string r = right[i];
        if (i == 6) r += std::to_string(undoStack_.size()) + ")";
        cout << "  " << paint(padRight(left[i], 32), Color::Default) << "  " << r << "\n";
    }
    cout << "  " << paint("0  Выход", Color::Cyan)
         << paint("    Быстрый ввод: «-18,50 продукты хлеб» или «+250 подработка»", Color::Gray) << "\n\n";
}

// ───────────────────────── быстрый ввод и операции ─────────────────────────

void App::handleQuickInput(const std::string& input) {
    QuickParseResult qr = parseQuickInput(ledger_, input);
    if (!qr.ok) {
        cout << paint("  " + qr.error, Color::Yellow) << "\n";
        return;
    }
    std::string wallet = lastWallet_;
    if (!ledger_.hasWallet(wallet) && !ledger_.wallets().empty()) wallet = ledger_.wallets().front().name;

    Operation op;
    op.date = Date::today();
    op.type = qr.type;
    op.amount = qr.amount;
    op.category = qr.category;
    op.wallet = wallet;
    op.comment = qr.comment;

    std::string err = ledger_.validate(op);
    if (!err.empty()) {
        cout << paint("  Операция не сохранена: " + err, Color::Red) << "\n";
        return;
    }
    if (op.type == OpType::Expense && op.amount > ledger_.walletBalance(op.wallet)) {
        cout << paint("  В кошельке «" + op.wallet + "» только " + formatMoney(ledger_.walletBalance(op.wallet)) +
                          " BYN.",
                      Color::Yellow)
             << "\n";
        if (!confirm("  Всё равно сохранить расход?")) {
            cout << "  Операция не сохранена.\n";
            return;
        }
    }
    pushUndo();
    int id = ledger_.add(op);
    persist();
    lastCategory_[op.type == OpType::Income ? 1 : 0] = op.category;
    lastWallet_ = op.wallet;
    cout << paint("  ✓ Быстрый ввод: " + std::string(typeName(op.type)) + " №" + std::to_string(id) + " на " +
                      formatMoney(op.amount) + " BYN («" + op.category + "», " + op.wallet +
                      ") сохранён. Баланс: " + formatMoney(ledger_.totalBalance()) + " BYN",
                  Color::Green)
         << "\n";
    if (op.type == OpType::Expense) showBudgetWarning(op.category, op.date);
}

bool App::askMoney(const std::string& prompt, Money& out, bool allowEmpty, Money def) {
    while (true) {
        std::string in;
        if (!ask(prompt, in)) return false;
        if (in.empty() && allowEmpty) {
            out = def;
            return true;
        }
        if (in.empty()) return false;  // пустой ввод — отмена
        Money m = 0;
        if (parseMoney(in, m) && m > 0 && m <= kMaxAmount) {
            out = m;
            return true;
        }
        cout << paint("  Введите положительную сумму, например 12,50", Color::Yellow) << "\n";
    }
}

bool App::chooseCategory(OpType type, std::string& out, const std::string& def) {
    auto cats = ledger_.categories(type);
    if (cats.empty()) return false;
    int defIdx = 1;
    for (size_t i = 0; i < cats.size(); ++i)
        if (cats[i].name == def) defIdx = static_cast<int>(i) + 1;
    cout << "  Категория:\n";
    for (size_t i = 0; i < cats.size(); ++i) {
        std::string item = padLeft(std::to_string(i + 1), 2) + " " + padRight(cats[i].name, 19);
        cout << "   " << item << ((i % 3 == 2 || i + 1 == cats.size()) ? "\n" : "");
    }
    while (true) {
        std::string in;
        if (!ask("  Номер категории [" + std::to_string(defIdx) + "]: ", in)) return false;
        int n = parseIntOrDefault(in, defIdx);
        if (n >= 1 && n <= static_cast<int>(cats.size())) {
            out = cats[static_cast<size_t>(n - 1)].name;
            return true;
        }
        cout << paint("  Нет категории с таким номером.", Color::Yellow) << "\n";
    }
}

bool App::chooseWallet(const std::string& prompt, std::string& out, const std::string& def) {
    const auto& ws = ledger_.wallets();
    int defIdx = 1;
    cout << "  " << prompt << ":";
    for (size_t i = 0; i < ws.size(); ++i) {
        if (ws[i].name == def) defIdx = static_cast<int>(i) + 1;
        cout << "  " << i + 1 << " " << ws[i].name << " (" << formatMoney(ledger_.walletBalance(ws[i].name)) << ")";
    }
    cout << "\n";
    while (true) {
        std::string in;
        if (!ask("  Номер кошелька [" + std::to_string(defIdx) + "]: ", in)) return false;
        int n = parseIntOrDefault(in, defIdx);
        if (n >= 1 && n <= static_cast<int>(ws.size())) {
            out = ws[static_cast<size_t>(n - 1)].name;
            return true;
        }
        cout << paint("  Нет кошелька с таким номером.", Color::Yellow) << "\n";
    }
}

void App::addOperation(OpType type) {
    bool income = type == OpType::Income;
    console::header(income ? "Новый доход" : "Новый расход");
    cout << paint("  Пустая сумма — отмена ввода.", Color::Gray) << "\n";
    Operation op;
    op.type = type;
    if (!askMoney("  Сумма, BYN: ", op.amount, false)) {
        cout << "  Ввод отменён.\n";
        return;
    }
    if (!chooseCategory(type, op.category, lastCategory_[income ? 1 : 0])) return;
    if (!chooseWallet(income ? "Зачислить в кошелёк" : "Списать с кошелька", op.wallet, lastWallet_)) return;
    while (true) {
        std::string in;
        if (!ask("  Дата [" + Date::today().str() + "]: ", in)) return;
        if (Date::parse(in, op.date) && !(op.date > Date::today())) break;
        cout << paint("  Введите дату в формате ДД.ММ.ГГГГ, не позже сегодняшней.", Color::Yellow) << "\n";
    }
    if (!ask("  Комментарий (необязательно): ", op.comment)) return;
    op.comment = utf8Truncate(op.comment, 100);

    std::string err = ledger_.validate(op);
    if (!err.empty()) {
        cout << paint("  Операция не сохранена: " + err, Color::Red) << "\n";
        return;
    }
    if (!income && op.amount > ledger_.walletBalance(op.wallet)) {
        cout << paint("  В кошельке «" + op.wallet + "» только " + formatMoney(ledger_.walletBalance(op.wallet)) +
                          " BYN.",
                      Color::Yellow)
             << "\n";
        if (!confirm("  Всё равно сохранить расход?")) {
            cout << "  Операция не сохранена.\n";
            return;
        }
    }
    pushUndo();
    int id = ledger_.add(op);
    persist();
    lastCategory_[income ? 1 : 0] = op.category;
    lastWallet_ = op.wallet;
    cout << paint("  ✓ " + std::string(typeName(type)) + " №" + std::to_string(id) + " на " +
                      formatMoney(op.amount) + " BYN («" + op.category + "», " + op.wallet +
                      ") сохранён. Баланс: " + formatMoney(ledger_.totalBalance()) + " BYN",
                  Color::Green)
         << "\n";
    if (!income) showBudgetWarning(op.category, op.date);
}

void App::showBudgetWarning(const std::string& category, const Date& date) {
    BudgetStatus b = budgetStatus(ledger_, category, date);
    if (b.limit == 0) return;
    if (b.state == BudgetState::Over)
        cout << paint("  [!] Лимит «" + category + "» превышен: потрачено " + formatMoney(b.spent) + " из " +
                          formatMoney(b.limit) + " BYN (" + pct0(b.usedPercent) + ").",
                      Color::Red)
             << "\n";
    else if (b.state == BudgetState::Near)
        cout << paint("  [!] Лимит «" + category + "» почти исчерпан: " + pct0(b.usedPercent) + ", осталось " +
                          formatMoney(b.limit - b.spent) + " BYN.",
                      Color::Yellow)
             << "\n";
    else
        cout << "  Лимит «" << category << "»: использовано " << pct0(b.usedPercent) << ".\n";
}

void App::addTransfer() {
    console::header("Перевод между кошельками");
    if (ledger_.wallets().size() < 2) {
        cout << "  Для перевода нужно минимум два кошелька (меню 12).\n";
        return;
    }
    Operation op;
    op.type = OpType::Transfer;
    if (!askMoney("  Сумма, BYN: ", op.amount, false)) {
        cout << "  Ввод отменён.\n";
        return;
    }
    if (!chooseWallet("Откуда", op.wallet, ledger_.wallets().back().name)) return;
    if (!chooseWallet("Куда", op.toWallet, ledger_.wallets().front().name)) return;
    op.date = Date::today();
    if (!ask("  Комментарий (необязательно): ", op.comment)) return;
    op.comment = utf8Truncate(op.comment, 100);
    std::string err = ledger_.validate(op);
    if (!err.empty()) {
        cout << paint("  Перевод не выполнен: " + err, Color::Red) << "\n";
        return;
    }
    pushUndo();
    ledger_.add(op);
    persist();
    cout << paint("  ✓ Переведено " + formatMoney(op.amount) + " BYN: " + op.wallet + " → " + op.toWallet, Color::Green)
         << "\n";
}

// ───────────────────────── журнал и поиск ─────────────────────────

void App::journal() {
    Date month = Date::today().firstOfMonth();
    std::string filterCat;
    while (!eof_) {
        auto ops = ledger_.between(month.firstOfMonth(), month.lastOfMonth());
        if (!filterCat.empty())
            ops.erase(std::remove_if(ops.begin(), ops.end(),
                                     [&](const Operation& o) { return o.category != filterCat; }),
                      ops.end());
        console::header("Журнал операций — " + month.monthName() +
                        (filterCat.empty() ? "" : "  [категория: " + filterCat + "]"));
        printOperationTable(ops);
        if (ops.empty()) cout << "  Операций нет.\n";
        Money inc = 0, exp = 0;
        for (const auto& o : ops) {
            if (o.type == OpType::Income) inc += o.amount;
            if (o.type == OpType::Expense) exp += o.amount;
        }
        cout << paint(console::line(), Color::Gray) << "\n";
        cout << "  Операций: " << ops.size() << "   доходы " << paint(signedMoney(inc, true), Color::Green)
             << "   расходы " << paint(signedMoney(exp, false), Color::Red) << "\n\n";
        cout << "  [<] пред. месяц  [>] след. месяц  [к] фильтр по категории  [и N] изменить №N\n"
                "  [у N] удалить №N  [0] назад\n";
        std::string in;
        if (!ask("Команда: ", in)) return;
        int id = 0;
        if (in == "0" || in.empty()) return;
        if (in == "<" || in == ",") month = month.addMonths(-1);
        else if (in == ">" || in == ".") month = month.addMonths(1);
        else if (isCmd(in, {"к", "k"})) {
            std::string cat, t;
            if (!ask("  Фильтр: 1 — категория расходов, 2 — категория доходов, 0 — сбросить: ", t)) return;
            if (t == "1" && chooseCategory(OpType::Expense, cat, "")) filterCat = cat;
            else if (t == "2" && chooseCategory(OpType::Income, cat, "")) filterCat = cat;
            else filterCat.clear();
        } else if (cmdWithId(in, {"и", "i", "e"}, id)) {
            if (id < 0 && !ask("  Номер операции: ", in)) return;
            if (id < 0) id = parseIntOrDefault(in, 0);
            editOperation(id);
        } else if (cmdWithId(in, {"у", "d"}, id)) {
            if (id < 0 && !ask("  Номер операции: ", in)) return;
            if (id < 0) id = parseIntOrDefault(in, 0);
            deleteOperation(id);
        } else {
            cout << paint("Неизвестная команда.", Color::Yellow) << "\n";
        }
    }
}

void App::searchScreen() {
    console::header("Поиск и фильтрация по всей истории");
    SearchFilter f;
    if (!ask("  Текст поиска (категория, кошелёк или комментарий, Enter — любой): ", f.query)) return;
    std::string in;
    if (!ask("  Тип (1 — расход, 2 — доход, 3 — перевод, Enter — все): ", in)) return;
    if (in == "1") { f.hasType = true; f.type = OpType::Expense; }
    else if (in == "2") { f.hasType = true; f.type = OpType::Income; }
    else if (in == "3") { f.hasType = true; f.type = OpType::Transfer; }

    if (!ask("  Кошелёк (Enter — любой): ", in)) return;
    if (!in.empty()) {
        for (const auto& w : ledger_.wallets()) {
            if (containsIgnoreCaseUtf8(w.name, in)) {
                f.wallet = w.name;
                break;
            }
        }
        if (f.wallet.empty()) f.wallet = in;
    }

    if (!ask("  Дата с (ДД.ММ.ГГГГ, Enter — без нижней границы): ", in)) return;
    if (!in.empty() && Date::parse(in, f.from)) f.hasFrom = true;

    if (!ask("  Дата по (ДД.ММ.ГГГГ, Enter — сегодня): ", in)) return;
    if (!in.empty() && Date::parse(in, f.to)) f.hasTo = true;

    if (!ask("  Минимальная сумма, BYN (Enter — любая): ", in)) return;
    if (!in.empty()) parseMoney(in, f.minAmount);

    while (!eof_) {
        auto found = ledger_.search(f);
        console::header("Результаты поиска (найдено: " + std::to_string(found.size()) + ")");
        if (found.empty()) {
            cout << "  Подходящих операций не найдено.\n";
        } else {
            size_t limit = std::min<size_t>(found.size(), 40);
            std::vector<Operation> slice(found.begin(), found.begin() + static_cast<ptrdiff_t>(limit));
            printOperationTable(slice);
            if (found.size() > limit)
                cout << paint("  ... показано первые " + std::to_string(limit) + " из " + std::to_string(found.size()),
                              Color::Gray)
                     << "\n";
            Money inc = 0, exp = 0, tr = 0;
            for (const auto& o : found) {
                if (o.type == OpType::Income) inc += o.amount;
                else if (o.type == OpType::Expense) exp += o.amount;
                else tr += o.amount;
            }
            cout << paint(console::line(), Color::Gray) << "\n";
            cout << "  Итого найдено: доходы " << paint(signedMoney(inc, true), Color::Green) << "   расходы "
                 << paint(signedMoney(exp, false), Color::Red) << "   переводы " << formatMoney(tr) << "\n";
        }
        cout << "\n  [и N] изменить №N  [у N] удалить №N  [0] назад\n";
        std::string cmd;
        if (!ask("Команда: ", cmd)) return;
        int id = 0;
        if (cmd.empty() || cmd == "0") return;
        if (cmdWithId(cmd, {"и", "i", "e"}, id)) {
            if (id < 0 && !ask("  Номер операции: ", cmd)) return;
            if (id < 0) id = parseIntOrDefault(cmd, 0);
            editOperation(id);
        } else if (cmdWithId(cmd, {"у", "d"}, id)) {
            if (id < 0 && !ask("  Номер операции: ", cmd)) return;
            if (id < 0) id = parseIntOrDefault(cmd, 0);
            deleteOperation(id);
        } else {
            return;
        }
    }
}

bool App::editOperation(int id) {
    const Operation* found = ledger_.find(id);
    if (!found) {
        cout << paint("  Операция №" + std::to_string(id) + " не найдена.", Color::Yellow) << "\n";
        return false;
    }
    Operation op = *found;
    cout << "  Изменение операции №" << id << ". Enter — оставить прежнее значение.\n";
    if (!askMoney("  Сумма [" + formatMoney(op.amount) + "]: ", op.amount, true, op.amount)) return false;
    if (op.type != OpType::Transfer) {
        if (!chooseCategory(op.type, op.category, op.category)) return false;
        if (!chooseWallet("Кошелёк", op.wallet, op.wallet)) return false;
    }
    while (true) {
        std::string in;
        if (!ask("  Дата [" + op.date.str() + "]: ", in)) return false;
        Date d = op.date;
        if (in.empty() || (Date::parse(in, d) && !(d > Date::today()))) {
            op.date = d;
            break;
        }
        cout << paint("  Неверная дата.", Color::Yellow) << "\n";
    }
    std::string comment;
    if (!ask("  Комментарий [" + op.comment + "] (\"-\" — очистить): ", comment)) return false;
    if (comment == "-") op.comment.clear();
    else if (!comment.empty()) op.comment = utf8Truncate(comment, 100);
    std::string err = ledger_.validate(op);
    if (!err.empty()) {
        cout << paint("  Изменения не сохранены: " + err, Color::Red) << "\n";
        return false;
    }
    pushUndo();
    ledger_.update(op);
    persist();
    cout << paint("  ✓ Операция №" + std::to_string(id) + " изменена.", Color::Green) << "\n";
    if (op.type == OpType::Expense) showBudgetWarning(op.category, op.date);
    return true;
}

bool App::deleteOperation(int id) {
    const Operation* op = ledger_.find(id);
    if (!op) {
        cout << paint("  Операция №" + std::to_string(id) + " не найдена.", Color::Yellow) << "\n";
        return false;
    }
    std::string what = std::string(typeName(op->type)) + " " + formatMoney(op->amount) + " BYN от " + op->date.str() +
                       (op->category.empty() ? "" : " («" + op->category + "»)");
    if (!confirm("  Удалить операцию №" + std::to_string(id) + ": " + what + "?")) {
        cout << "  Удаление отменено.\n";
        return false;
    }
    pushUndo();
    ledger_.remove(id);
    persist();
    cout << paint("  ✓ Операция удалена.", Color::Green) << "\n";
    return true;
}

// ───────────────────────── статистика и здоровье ─────────────────────────

void App::statistics() {
    Date month = Date::today().firstOfMonth();
    while (!eof_) {
        PeriodStats s = monthStats(ledger_, month);
        FinancialHealth fh = computeFinancialHealth(ledger_, month);
        console::header("Статистика и здоровье финансов — " + month.monthName());
        cout << "  Доходы " << paint(signedMoney(s.income, true), Color::Green) << "    Расходы "
             << paint(signedMoney(s.expense, false), Color::Red) << "    Остаток "
             << paint((s.balance() >= 0 ? "+" : "") + formatMoney(s.balance()),
                      s.balance() >= 0 ? Color::Green : Color::Red)
             << "    Операций: " << s.operations << "\n";
        cout << "  Индекс фин. здоровья: " << paint(std::to_string(fh.score) + " / 100 (" + fh.grade + ")", Color::Cyan)
             << "  [сбережения " << fh.savingsScore << "/30 · 50/30/20 " << fh.rule503020Score << "/25 · лимиты "
             << fh.budgetScore << "/25 · подушка " << fh.cushionScore << "/20 (" << fmt1Comma(fh.safetyCushionMonths)
             << " мес.)]\n\n";

        cout << paint("  Расходы по категориям", Color::Bold) << "\n";
        if (s.expenseByCategory.empty()) cout << "  нет расходов\n";
        double maxShare = s.expenseByCategory.empty() ? 1 : s.expenseByCategory.front().share;
        for (const auto& c : s.expenseByCategory)
            cout << "   " << padRight(c.category, 18) << padLeft(formatMoney(c.amount), 10) << padLeft(pct0(c.share), 6)
                 << "  " << paint(console::bar(c.share / std::max(maxShare, 1.0), 24), Color::Cyan) << "\n";

        if (s.income > 0) {
            cout << "\n" << paint("  Структура расходов от дохода (правило 50/30/20)", Color::Bold) << "\n";
            struct {
                const char* name;
                Money value;
                double norm;
                bool atMost;
            } rows[] = {{"Необходимое", s.needs, 50, true},
                        {"Желания", s.wants, 30, true},
                        {"Сбережения", s.savings + std::max<Money>(0, s.balance()), 20, false}};
            for (const auto& r : rows) {
                double p = percent(r.value, s.income);
                bool ok = r.atMost ? p <= r.norm : p >= r.norm;
                cout << "   " << padRight(r.name, 14) << padLeft(pct0(p), 6) << "  "
                     << paint(console::bar(p / 100.0, 20), ok ? Color::Green : Color::Yellow) << "  норма "
                     << (r.atMost ? "≤ " : "≥ ") << r.norm << " %\n";
            }
        }

        auto wd = weekdayBreakdown(ledger_, month.firstOfMonth(), month.lastOfMonth());
        Money maxWd = 1;
        for (const auto& w : wd) maxWd = std::max(maxWd, w.total);
        cout << "\n" << paint("  Расходы по дням недели", Color::Bold) << "\n";
        cout << "   ";
        for (const auto& w : wd) {
            cout << w.name << ": " << formatMoney(w.total) << "  ";
        }
        cout << "\n";

        cout << "\n" << paint("  Динамика за 6 месяцев", Color::Bold) << "\n";
        auto dyn = monthlyDynamics(ledger_, month, 6);
        Money maxV = 1;
        for (const auto& d : dyn) maxV = std::max({maxV, d.income, d.expense});
        for (const auto& d : dyn) {
            cout << "   " << padRight(d.from.monthName(), 15) << " доходы "
                 << paint(console::bar(static_cast<double>(d.income) / maxV, 12), Color::Green)
                 << padLeft(formatMoney(d.income), 10) << "  расходы "
                 << paint(console::bar(static_cast<double>(d.expense) / maxV, 12), Color::Red)
                 << padLeft(formatMoney(d.expense), 10) << "\n";
        }
        cout << "\n  [<] пред. месяц  [>] след. месяц  [0] назад\n";
        std::string in;
        if (!ask("Команда: ", in)) return;
        if (in == "<" || in == ",") month = month.addMonths(-1);
        else if (in == ">" || in == ".") month = month.addMonths(1);
        else return;
    }
}

// ───────────────────────── бюджеты ─────────────────────────

void App::budgets() {
    while (!eof_) {
        Date today = Date::today();
        console::header("Бюджеты (месячные лимиты) — " + today.monthName());
        auto list = budgetStatuses(ledger_, today);
        if (list.empty()) {
            cout << "  Лимиты не установлены. Лимит помогает не потратить на категорию больше, чем запланировано.\n";
        } else {
            cout << paint("   " + padRight("Категория", 18) + padLeft("Лимит", 10) + padLeft("Потрачено", 11) +
                              padLeft("Осталось", 10) + "  Использовано",
                          Color::Gray)
                 << "\n";
            for (const auto& b : list) {
                cout << "   " << padRight(b.category, 18) << padLeft(formatMoney(b.limit), 10)
                     << padLeft(formatMoney(b.spent), 11) << padLeft(formatMoney(std::max<Money>(0, b.limit - b.spent)), 10)
                     << "  " << paint(console::bar(b.usedPercent / 100.0, 15), budgetColor(b.state)) << " "
                     << paint(padLeft(pct0(b.usedPercent), 6), budgetColor(b.state))
                     << (b.state == BudgetState::Over ? paint("  превышен", Color::Red)
                         : b.state == BudgetState::Near ? paint("  ≥ 80 %", Color::Yellow)
                                                        : "")
                     << "\n";
            }
        }
        cout << "\n  [1] установить или изменить лимит  [2] снять лимит  [0] назад\n";
        std::string in;
        if (!ask("Команда: ", in)) return;
        if (in == "1") {
            std::string cat;
            if (!chooseCategory(OpType::Expense, cat, "")) continue;
            Money spentNow = ledger_.spent(cat, today.firstOfMonth(), today.lastOfMonth());
            cout << "  В этом месяце на «" << cat << "» уже потрачено " << formatMoney(spentNow) << " BYN.\n";
            Money limit = 0;
            if (!askMoney("  Лимит в месяц, BYN: ", limit, false)) continue;
            pushUndo();
            ledger_.setBudget(cat, limit);
            persist();
            cout << paint("  ✓ Лимит для «" + cat + "»: " + formatMoney(limit) + " BYN в месяц.", Color::Green) << "\n";
        } else if (in == "2") {
            std::string cat;
            if (!chooseCategory(OpType::Expense, cat, "")) continue;
            pushUndo();
            ledger_.setBudget(cat, 0);
            persist();
            cout << "  Лимит для «" << cat << "» снят.\n";
        } else {
            return;
        }
    }
}

// ───────────────────────── цели накоплений ─────────────────────────

void App::goalsScreen() {
    while (!eof_) {
        Date today = Date::today();
        console::header("Финансовые цели накоплений");
        if (ledger_.goals().empty()) {
            cout << "  Цели не заданы. Поставьте цель — например, «Финансовая подушка» или «Отпуск».\n";
        } else {
            for (const auto& g : ledger_.goals()) {
                GoalProjection gp = projectGoal(g, today);
                Color c = g.completed ? Color::Green : gp.overdue ? Color::Red : Color::Cyan;
                cout << "  №" << padLeft(std::to_string(g.id), 2) << "  " << padRight(g.name, 24)
                     << "  " << paint(console::bar(gp.percentDone / 100.0, 16), c) << " "
                     << padLeft(pct0(gp.percentDone), 5) << "  (" << formatMoney(g.savedAmount) << " / "
                     << formatMoney(g.targetAmount) << " BYN)\n";
                if (g.completed) {
                    cout << paint("      ✓ Цель достигнута!", Color::Green) << "\n";
                } else {
                    cout << "      Срок: " << g.deadline.str() << " · осталось накопить "
                         << formatMoney(gp.remaining) << " BYN";
                    if (gp.overdue) cout << paint(" · Срок наступил!", Color::Yellow);
                    else cout << " (≈ " << formatMoney(gp.monthlyNeeded) << " BYN/мес.)";
                    cout << "\n";
                }
            }
        }
        cout << "\n  [1] новая цель  [2] пополнить цель  [3] удалить цель  [0] назад\n";
        std::string in;
        if (!ask("Команда: ", in)) return;
        if (in == "1") {
            SavingsGoal g;
            if (!ask("  Название цели: ", g.name) || g.name.empty()) continue;
            g.name = utf8Truncate(g.name, 30);
            if (!askMoney("  Целевая сумма, BYN: ", g.targetAmount, false)) continue;
            if (!askMoney("  Уже накоплено, BYN [0]: ", g.savedAmount, true, 0)) continue;
            while (true) {
                std::string ds;
                if (!ask("  Срок достижения (ДД.ММ.ГГГГ) [" + today.addMonths(6).str() + "]: ", ds)) return;
                if (ds.empty()) {
                    g.deadline = today.addMonths(6);
                    break;
                }
                if (Date::parse(ds, g.deadline)) break;
                cout << paint("  Неверная дата.", Color::Yellow) << "\n";
            }
            pushUndo();
            std::string err;
            if (ledger_.addGoal(g, err) > 0) {
                persist();
                cout << paint("  ✓ Цель добавлена.", Color::Green) << "\n";
            } else {
                undoStack_.pop_back();
                cout << paint("  " + err, Color::Red) << "\n";
            }
        } else if (in == "2") {
            std::string idStr;
            if (!ask("  Номер цели (№): ", idStr)) return;
            int id = parseIntOrDefault(idStr, 0);
            if (!ledger_.findGoal(id)) {
                cout << paint("  Цель не найдена.", Color::Yellow) << "\n";
                continue;
            }
            Money amount = 0;
            if (!askMoney("  Сумма пополнения, BYN: ", amount, false)) continue;
            std::string wallet;
            if (!chooseWallet("Списать с кошелька", wallet, lastWallet_)) continue;
            pushUndo();
            std::string err;
            int opId = ledger_.contributeGoal(id, amount, wallet, today, err);
            if (opId > 0) {
                persist();
                cout << paint("  ✓ Взнос проведён (операция №" + std::to_string(opId) + ").", Color::Green) << "\n";
            } else {
                undoStack_.pop_back();
                cout << paint("  " + err, Color::Red) << "\n";
            }
        } else if (in == "3") {
            std::string idStr;
            if (!ask("  Номер цели для удаления: ", idStr)) return;
            int id = parseIntOrDefault(idStr, 0);
            if (confirm("  Удалить цель №" + std::to_string(id) + "?")) {
                pushUndo();
                if (ledger_.removeGoal(id)) {
                    persist();
                    cout << paint("  ✓ Цель удалена.", Color::Green) << "\n";
                } else {
                    undoStack_.pop_back();
                    cout << paint("  Цель не найдена.", Color::Yellow) << "\n";
                }
            }
        } else {
            return;
        }
    }
}

// ───────────────────────── регулярные платежи ─────────────────────────

void App::recurringScreen() {
    while (!eof_) {
        Date today = Date::today();
        auto dueList = ledger_.dueRecurring(today);
        console::header("Регулярные платежи и подписки");
        if (ledger_.recurring().empty()) {
            cout << "  Шаблоны не заданы. Добавьте повторяющиеся траты или поступления (ЖКУ, интернет, стипендия).\n";
        } else {
            for (const auto& r : ledger_.recurring()) {
                bool isDue = std::any_of(dueList.begin(), dueList.end(),
                                         [&](const RecurringPayment& d) { return d.id == r.id; });
                std::string st = !r.active ? paint("[пауза]  ", Color::Gray)
                                 : isDue   ? paint("[ОЖИДАЕТ]", Color::Yellow)
                                           : paint("[выполн.]", Color::Green);
                cout << "  №" << padLeft(std::to_string(r.id), 2) << "  " << st << "  " << padRight(r.name, 20)
                     << "  " << padLeft(std::to_string(r.dayOfMonth), 2) << "-е число  "
                     << padRight(typeName(r.type), 7) << "  " << padLeft(formatMoney(r.amount), 10) << " BYN  ("
                     << r.category << ", " << r.wallet << ")\n";
            }
        }
        cout << "\n  [1] провести все ожидающие (" << dueList.size()
             << ")  [2] новый шаблон  [3] пауза/включить  [4] удалить  [0] назад\n";
        std::string in;
        if (!ask("Команда: ", in)) return;
        if (in == "1") {
            if (dueList.empty()) {
                cout << "  На сегодня ожидающих платежей нет.\n";
                continue;
            }
            pushUndo();
            auto created = ledger_.applyAllDueRecurring(today);
            persist();
            cout << paint("  ✓ Проведено регулярных операций: " + std::to_string(created.size()), Color::Green) << "\n";
        } else if (in == "2") {
            RecurringPayment r;
            if (!ask("  Название (напр. Интернет, Аренда): ", r.name) || r.name.empty()) continue;
            r.name = utf8Truncate(r.name, 28);
            std::string tp;
            if (!ask("  Тип: 1 — расход, 2 — доход [1]: ", tp)) return;
            r.type = (tp == "2") ? OpType::Income : OpType::Expense;
            if (!askMoney("  Сумма, BYN: ", r.amount, false)) continue;
            if (!chooseCategory(r.type, r.category, "")) continue;
            if (!chooseWallet("Кошелёк", r.wallet, lastWallet_)) continue;
            std::string dStr;
            if (!ask("  День месяца (1–31) [" + std::to_string(today.d) + "]: ", dStr)) return;
            int day = parseIntOrDefault(dStr, today.d);
            r.dayOfMonth = std::clamp(day, 1, 31);
            pushUndo();
            std::string err;
            if (ledger_.addRecurring(r, err) > 0) {
                persist();
                cout << paint("  ✓ Шаблон регулярного платежа создан.", Color::Green) << "\n";
            } else {
                undoStack_.pop_back();
                cout << paint("  " + err, Color::Red) << "\n";
            }
        } else if (in == "3") {
            std::string idStr;
            if (!ask("  Номер шаблона: ", idStr)) return;
            int id = parseIntOrDefault(idStr, 0);
            pushUndo();
            if (ledger_.toggleRecurring(id)) {
                persist();
                cout << paint("  ✓ Статус изменён.", Color::Green) << "\n";
            } else {
                undoStack_.pop_back();
                cout << paint("  Шаблон не найден.", Color::Yellow) << "\n";
            }
        } else if (in == "4") {
            std::string idStr;
            if (!ask("  Номер шаблона для удаления: ", idStr)) return;
            int id = parseIntOrDefault(idStr, 0);
            if (confirm("  Удалить шаблон №" + std::to_string(id) + "?")) {
                pushUndo();
                if (ledger_.removeRecurring(id)) {
                    persist();
                    cout << paint("  ✓ Шаблон удалён.", Color::Green) << "\n";
                } else {
                    undoStack_.pop_back();
                    cout << paint("  Шаблон не найден.", Color::Yellow) << "\n";
                }
            }
        } else {
            return;
        }
    }
}

// ───────────────────────── долги и займы ─────────────────────────

void App::debtsScreen() {
    while (!eof_) {
        Date today = Date::today();
        console::header("Учёт долгов и займов");
        cout << "  Вам должны: " << paint(formatMoney(ledger_.totalOwedToMe()) + " BYN", Color::Green)
             << "   ·   Вы должны: " << paint(formatMoney(ledger_.totalIOwe()) + " BYN", Color::Red) << "\n\n";
        if (ledger_.debts().empty()) {
            cout << "  Записей о долгах нет.\n";
        } else {
            for (const auto& d : ledger_.debts()) {
                Color c = d.settled() ? Color::Gray
                          : d.direction == DebtDirection::OwedToMe ? Color::Green : Color::Yellow;
                cout << "  №" << padLeft(std::to_string(d.id), 2) << "  "
                     << paint(padRight(debtDirectionName(d.direction), 11), c) << "  " << padRight(d.person, 18)
                     << "  ост. " << padLeft(formatMoney(d.remaining()), 10) << " из "
                     << formatMoney(d.amount) << " BYN  до " << d.dueDate.str()
                     << (d.settled() ? " [закрыт]" : "") << (d.comment.empty() ? "" : "  (" + d.comment + ")") << "\n";
            }
        }
        cout << "\n  [1] добавить долг  [2] погасить / отметить возврат  [3] удалить запись  [0] назад\n";
        std::string in;
        if (!ask("Команда: ", in)) return;
        if (in == "1") {
            Debt d;
            std::string dir;
            if (!ask("  Направление: 1 — мне должны, 2 — я должен [1]: ", dir)) return;
            d.direction = (dir == "2") ? DebtDirection::IOwe : DebtDirection::OwedToMe;
            if (!ask("  Контрагент (имя): ", d.person) || d.person.empty()) continue;
            d.person = utf8Truncate(d.person, 24);
            if (!askMoney("  Сумма, BYN: ", d.amount, false)) continue;
            d.created = today;
            std::string due;
            if (!ask("  Вернуть до (ДД.ММ.ГГГГ) [" + today.addMonths(1).str() + "]: ", due)) return;
            if (due.empty() || !Date::parse(due, d.dueDate)) d.dueDate = today.addMonths(1);
            if (!ask("  Комментарий (необязательно): ", d.comment)) return;
            d.comment = utf8Truncate(d.comment, 60);
            pushUndo();
            std::string err;
            if (ledger_.addDebt(d, err) > 0) {
                persist();
                cout << paint("  ✓ Запись о долге добавлена.", Color::Green) << "\n";
            } else {
                undoStack_.pop_back();
                cout << paint("  " + err, Color::Red) << "\n";
            }
        } else if (in == "2") {
            std::string idStr;
            if (!ask("  Номер записи (№): ", idStr)) return;
            int id = parseIntOrDefault(idStr, 0);
            const Debt* found = ledger_.findDebt(id);
            if (!found || found->settled()) {
                cout << paint("  Активный долг не найден.", Color::Yellow) << "\n";
                continue;
            }
            Money pay = 0;
            if (!askMoney("  Сумма погашения [" + formatMoney(found->remaining()) + "]: ", pay, true, found->remaining()))
                continue;
            bool createOp = confirm("  Отразить операцию в кошельке и журнале?");
            std::string wallet = lastWallet_;
            if (createOp && !chooseWallet("Кошелёк расчёта", wallet, lastWallet_)) continue;
            pushUndo();
            std::string err;
            if (ledger_.repayDebt(id, pay, createOp, wallet, today, err)) {
                persist();
                cout << paint("  ✓ Погашение зафиксировано.", Color::Green) << "\n";
            } else {
                undoStack_.pop_back();
                cout << paint("  " + err, Color::Red) << "\n";
            }
        } else if (in == "3") {
            std::string idStr;
            if (!ask("  Номер записи для удаления: ", idStr)) return;
            int id = parseIntOrDefault(idStr, 0);
            if (confirm("  Удалить запись №" + std::to_string(id) + "?")) {
                pushUndo();
                if (ledger_.removeDebt(id)) {
                    persist();
                    cout << paint("  ✓ Запись удалена.", Color::Green) << "\n";
                } else {
                    undoStack_.pop_back();
                    cout << paint("  Запись не найдена.", Color::Yellow) << "\n";
                }
            }
        } else {
            return;
        }
    }
}

// ───────────────────────── советы ─────────────────────────

void App::advice() {
    Date today = Date::today();
    console::header("Советы по организации трат — " + today.monthName());
    for (const auto& a : makeAdvice(ledger_, today, today)) {
        const char* mark = a.level == AdviceLevel::Good ? "[+]" : a.level == AdviceLevel::Warning ? "[!]" : "[i]";
        Color c = a.level == AdviceLevel::Good ? Color::Green : a.level == AdviceLevel::Warning ? Color::Yellow : Color::Cyan;
        cout << "  " << paint(std::string(mark) + " " + a.title, c) << "\n";
        printWrapped(a.text, 68, "      ");
    }
    std::string in;
    ask("\nEnter — вернуться в меню ", in);
}

// ───────────────────────── кошельки и категории ─────────────────────────

void App::walletsAndCategories() {
    while (!eof_) {
        console::header("Кошельки и категории");
        cout << paint("  Кошельки", Color::Bold) << "\n";
        for (const auto& w : ledger_.wallets())
            cout << "   " << padRight(w.name, 18) << padLeft(formatMoney(ledger_.walletBalance(w.name)), 12)
                 << " BYN  (нач. ост. " << formatMoney(w.initial) << ")\n";
        cout << "\n" << paint("  Категории расходов", Color::Bold) << "\n";
        for (Group g : {Group::Needs, Group::Wants, Group::Savings}) {
            std::string names;
            for (const auto& c : ledger_.categories(OpType::Expense))
                if (c.group == g) names += (names.empty() ? "" : ", ") + c.name;
            cout << "   " << padRight(std::string(groupName(g)) + ":", 13);
            printWrapped(names, 56, std::string(16, ' '), false);
        }
        std::string inc;
        for (const auto& c : ledger_.categories(OpType::Income)) inc += (inc.empty() ? "" : ", ") + c.name;
        cout << "\n" << paint("  Категории доходов", Color::Bold) << "\n";
        printWrapped(inc, 66, "   ");
        cout << "\n  [1] добавить кошелёк  [2] переименовать кошелёк  [3] удалить пустой кошелёк\n"
                "  [4] категорию расходов  [5] категорию доходов  [6] удалить пустую категорию  [0] назад\n";
        std::string in;
        if (!ask("Команда: ", in)) return;
        if (in == "1") {
            Wallet w;
            if (!ask("  Название кошелька: ", w.name) || w.name.empty()) continue;
            w.name = utf8Truncate(w.name, 20);
            Money initial = 0;
            if (!askMoney("  Начальный остаток, BYN [0]: ", initial, true, 0)) continue;
            w.initial = initial;
            pushUndo();
            if (ledger_.addWallet(w)) {
                persist();
                cout << paint("  ✓ Кошелёк добавлен.", Color::Green) << "\n";
            } else {
                undoStack_.pop_back();
                cout << paint("  Кошелёк с таким названием уже есть.", Color::Yellow) << "\n";
            }
        } else if (in == "2") {
            std::string oldW, newW, err;
            if (!chooseWallet("Какой кошелёк переименовать", oldW, "")) continue;
            if (!ask("  Новое название: ", newW)) continue;
            newW = utf8Truncate(trim(newW), 20);
            pushUndo();
            if (ledger_.renameWallet(oldW, newW, err)) {
                if (lastWallet_ == oldW) lastWallet_ = newW;
                persist();
                cout << paint("  ✓ Кошелёк переименован.", Color::Green) << "\n";
            } else {
                undoStack_.pop_back();
                cout << paint("  " + err, Color::Yellow) << "\n";
            }
        } else if (in == "3") {
            std::string w, err;
            if (!chooseWallet("Какой кошелёк удалить", w, "")) continue;
            pushUndo();
            if (ledger_.removeWallet(w, err)) {
                persist();
                cout << paint("  ✓ Кошелёк удалён.", Color::Green) << "\n";
            } else {
                undoStack_.pop_back();
                cout << paint("  " + err, Color::Yellow) << "\n";
            }
        } else if (in == "4" || in == "5") {
            Category c;
            c.type = in == "4" ? OpType::Expense : OpType::Income;
            c.group = Group::Income;
            if (!ask("  Название категории: ", c.name) || c.name.empty()) continue;
            c.name = utf8Truncate(c.name, 30);
            if (c.type == OpType::Expense) {
                std::string g;
                if (!ask("  Группа: 1 — необходимое, 2 — желания, 3 — сбережения [2]: ", g)) return;
                c.group = g == "1" ? Group::Needs : g == "3" ? Group::Savings : Group::Wants;
            }
            pushUndo();
            if (ledger_.addCategory(c)) {
                persist();
                cout << paint("  ✓ Категория добавлена.", Color::Green) << "\n";
            } else {
                undoStack_.pop_back();
                cout << paint("  Такая категория уже есть.", Color::Yellow) << "\n";
            }
        } else if (in == "6") {
            std::string tp, cat, err;
            if (!ask("  Тип: 1 — расход, 2 — доход: ", tp)) continue;
            OpType t = tp == "2" ? OpType::Income : OpType::Expense;
            if (!chooseCategory(t, cat, "")) continue;
            pushUndo();
            if (ledger_.removeCategory(cat, t, err)) {
                persist();
                cout << paint("  ✓ Категория удалена.", Color::Green) << "\n";
            } else {
                undoStack_.pop_back();
                cout << paint("  " + err, Color::Yellow) << "\n";
            }
        } else {
            return;
        }
    }
}

// ───────────────────────── экспорт и резервные копии ─────────────────────────

bool App::askMonth(Date& month) {
    std::string in;
    if (!ask("  Месяц в формате ММ.ГГГГ [" + std::to_string(month.m) + "." + std::to_string(month.y) + "]: ", in))
        return false;
    if (in.empty()) return true;
    int m = 0, y = 0;
    if (std::sscanf(in.c_str(), "%d.%d", &m, &y) == 2 && Date::isValid(y, m, 1)) {
        month = Date{y, m, 1};
        return true;
    }
    cout << paint("  Неверный формат месяца.", Color::Yellow) << "\n";
    return false;
}

void App::exportAndBackups() {
    while (!eof_) {
        console::header("Экспорт отчётов и резервные копии");
        cout << "  [1] Экспорт отчёта за месяц в CSV (для Excel / Calc)\n"
             << "  [2] Экспорт интерактивного HTML-дашборда (с графиками SVG)\n"
             << "  [3] Создать именованный снимок резервной копии\n"
             << "  [4] Просмотр и восстановление резервных копий\n"
             << "  [0] Назад\n\n";
        std::string in;
        if (!ask("Команда: ", in)) return;
        if (in == "1" || in == "2") {
            Date month = Date::today().firstOfMonth();
            if (!askMonth(month)) continue;
            std::string outPath, err;
            bool ok = (in == "1") ? storage_.exportMonth(ledger_, month, outPath, err)
                                  : storage_.exportMonthHtml(ledger_, month, outPath, err);
            if (ok) {
                cout << paint("  ✓ Отчёт за " + month.monthName() + " сохранён: " + outPath, Color::Green) << "\n";
                if (in == "2") cout << "  Откройте HTML-файл в любом браузере (интернет не требуется).\n";
            } else {
                cout << paint("  Не удалось сохранить отчёт: " + err, Color::Red) << "\n";
            }
        } else if (in == "3") {
            std::string tag, name, err;
            if (!ask("  Короткая метка снимка (латиница/цифры, Enter — без метки): ", tag)) return;
            persist();
            if (storage_.createNamedBackup(tag, name, err))
                cout << paint("  ✓ Снимок создан: " + name, Color::Green) << "\n";
            else
                cout << paint("  " + err, Color::Red) << "\n";
        } else if (in == "4") {
            auto snaps = storage_.listNamedBackups();
            if (snaps.empty()) {
                cout << "  Именованные снимки ещё не создавались.\n";
                continue;
            }
            cout << "\n  Доступные снимки:\n";
            for (size_t i = 0; i < snaps.size(); ++i) {
                cout << "   " << i + 1 << "  " << snaps[i].label << "  (" << snaps[i].fileCount << " файлов)\n";
            }
            std::string idxStr;
            if (!ask("  Номер снимка для восстановления (Enter — отмена): ", idxStr) || idxStr.empty()) continue;
            int idx = parseIntOrDefault(idxStr, 0);
            if (idx < 1 || idx > static_cast<int>(snaps.size())) {
                cout << paint("  Неверный номер снимка.", Color::Yellow) << "\n";
                continue;
            }
            if (confirm("  Восстановить данные из снимка «" + snaps[static_cast<size_t>(idx - 1)].label + "»?")) {
                pushUndo();
                std::vector<std::string> warnings;
                std::string err;
                if (storage_.restoreNamedBackup(snaps[static_cast<size_t>(idx - 1)].name, ledger_, warnings, err)) {
                    for (const auto& w : warnings) cout << paint("  [!] " + w, Color::Yellow) << "\n";
                    cout << paint("  ✓ Данные восстановлены из резервной копии.", Color::Green) << "\n";
                } else {
                    undoStack_.pop_back();
                    cout << paint("  " + err, Color::Red) << "\n";
                }
            }
        } else {
            return;
        }
    }
}

// ───────────────────────── демонстрационные данные ─────────────────────────

void App::generateDemo() {
    std::mt19937 rng(2026);
    auto rnd = [&](int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(rng); };
    Date today = Date::today();
    Date start = today.firstOfMonth().addMonths(-5);
    ledger_.clear();
    ledger_.setDefaults();
    ledger_.setWalletInitial("Наличные", 5000);
    ledger_.setWalletInitial("Карта", 30000);
    ledger_.setBudget("Продукты", 45000);
    ledger_.setBudget("Кафе и рестораны", 15000);
    ledger_.setBudget("Развлечения", 10000);

    auto add = [&](const Date& d, OpType t, Money amount, const char* cat, const char* wallet, const char* comment,
                   const char* to = "") {
        if (d > today) return;
        Operation op;
        op.date = d;
        op.type = t;
        op.amount = amount;
        op.category = cat;
        op.wallet = wallet;
        op.toWallet = to;
        op.comment = comment;
        ledger_.add(op);
    };
    for (Date m = start; !(m > today); m = m.addMonths(1)) {
        int days = Date::daysInMonth(m.y, m.m);
        auto day = [&](int d) { return Date{m.y, m.m, std::min(d, days)}; };
        add(day(5), OpType::Income, 90000, "Зарплата", "Карта", "основная часть");
        add(day(20), OpType::Income, 60000, "Зарплата", "Карта", "аванс");
        add(day(25), OpType::Income, 18500, "Стипендия", "Карта", "");
        if (rnd(0, 1)) add(day(rnd(10, 18)), OpType::Income, rnd(80, 200) * 100, "Подработка", "Наличные", "репетиторство");
        add(day(1), OpType::Expense, 4500, "Транспорт", "Карта", "проездной");
        add(day(3), OpType::Expense, 2490, "Связь и интернет", "Карта", "мобильная связь");
        add(day(10), OpType::Expense, rnd(15000, 17500), "Жильё и ЖКУ", "Карта", "коммунальные услуги");
        add(day(6), OpType::Transfer, 20000, "", "Карта", "снятие в банкомате", "Наличные");
        add(day(21), OpType::Expense, 15000, "Сбережения", "Карта", "на отпуск");
        for (int d = 1; d <= days; ++d) {
            Date dt = day(d);
            if (d % 3 == 1)
                add(dt, OpType::Expense, rnd(1500, 6000) + rnd(0, 99), "Продукты", d % 2 ? "Карта" : "Наличные", "");
            if (d % 4 == 2)
                add(dt, OpType::Expense, rnd(700, 3500), "Кафе и рестораны", "Карта", d % 8 == 2 ? "обед" : "кофе");
            if (d % 7 == 5) add(dt, OpType::Expense, rnd(1000, 3500), "Развлечения", "Карта", "кино / игры");
        }
        add(day(rnd(8, 24)), OpType::Expense, rnd(40, 140) * 100, "Одежда", "Карта", "");
        if (rnd(0, 2) == 0) add(day(rnd(2, 27)), OpType::Expense, rnd(15, 60) * 100, "Здоровье", "Карта", "аптека");
        if (rnd(0, 1))
            add(day(rnd(2, 27)), OpType::Expense, rnd(20, 50) * 100, "Подарки", "Наличные", "день рождения друга");
    }

    // Демонстрационные цели накоплений, регулярные платежи и долги
    std::string err;
    ledger_.addGoal(SavingsGoal{0, "Финансовая подушка", 300000, 165000, today.addMonths(6), false}, err);
    ledger_.addGoal(SavingsGoal{0, "Ноутбук для учёбы", 220000, 95000, today.addMonths(4), false}, err);
    ledger_.addGoal(SavingsGoal{0, "Проездной и техника", 40000, 40000, today.addMonths(-1), true}, err);

    std::string prevYm = today.firstOfMonth().addMonths(-1).yearMonth();
    std::string curYm = today.yearMonth();
    ledger_.addRecurring(
        RecurringPayment{0, "Домашний интернет", OpType::Expense, 3400, "Связь и интернет", "Карта", 1, prevYm, true},
        err);
    ledger_.addRecurring(
        RecurringPayment{0, "Абонемент в спортзал", OpType::Expense, 6500, "Здоровье", "Карта", 15, prevYm, true}, err);
    ledger_.addRecurring(
        RecurringPayment{0, "Проездной билет", OpType::Expense, 4500, "Транспорт", "Карта", 1, curYm, true}, err);
    ledger_.addRecurring(
        RecurringPayment{0, "Стипендия", OpType::Income, 18500, "Стипендия", "Карта", 25, prevYm, true}, err);

    ledger_.addDebt(Debt{0, "Алексей К.", DebtDirection::OwedToMe, 8500, 2500, today.addDays(-14), today.addDays(16),
                         "билеты в кино"},
                    err);
    ledger_.addDebt(
        Debt{0, "Дмитрий С.", DebtDirection::IOwe, 12000, 0, today.addDays(-10), today.addDays(20), "покупка роутера"},
        err);

    persist();
}
