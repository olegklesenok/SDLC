// tests.cpp — модульные и нагрузочные тесты логики HomeLedger v1.0.0 (без сторонних библиотек)
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include "Analytics.h"
#include "Storage.h"
#include "Text.h"

static int g_failed = 0, g_total = 0;

#define CHECK(...)                                                                           \
    do {                                                                                     \
        ++g_total;                                                                           \
        if (!(__VA_ARGS__)) {                                                                \
            ++g_failed;                                                                      \
            std::cout << "FAIL " << __FILE__ << ":" << __LINE__ << "  " #__VA_ARGS__ "\n";   \
        }                                                                                    \
    } while (0)

static Operation makeOp(OpType t, Money amount, const char* cat, const char* wallet, Date d, const char* comment = "") {
    Operation op;
    op.type = t;
    op.amount = amount;
    op.category = cat;
    op.wallet = wallet;
    op.date = d;
    op.comment = comment;
    return op;
}

static void testMoney() {
    Money m = 0;
    CHECK(parseMoney("12,5", m) && m == 1250);
    CHECK(parseMoney("1 200.05", m) && m == 120005);
    CHECK(parseMoney("7", m) && m == 700);
    CHECK(!parseMoney("12,345", m));
    CHECK(!parseMoney("abc", m));
    CHECK(!parseMoney("", m));
    CHECK(formatMoney(123456789) == "1 234 567,89");
    CHECK(formatMoney(-5) == "-0,05");
    CHECK(moneyToCsv(123456) == "1234.56");
    Money a = 0, b = 0, c = 0;  // целые копейки: 0,10 + 0,20 == 0,30 ровно
    CHECK(parseMoney("0,10", a) && parseMoney("0,20", b) && parseMoney("0,30", c) && a + b == c);
}

static void testDate() {
    Date d;
    CHECK(Date::parse("2026-02-28", d) && d.toDays() == Date{2026, 2, 28}.toDays());
    CHECK(!Date::parse("2026-02-29", d));
    CHECK(Date::parse("29.02.2024", d) && d.d == 29);
    CHECK(Date{2026, 12, 31}.addDays(1) == (Date{2027, 1, 1}));
    CHECK(Date{2026, 1, 15}.addMonths(-1) == (Date{2025, 12, 1}));
    CHECK(Date::daysInMonth(2024, 2) == 29);
    CHECK((Date{2026, 11, 23}).dayOfWeek() == 0);  // 23.11.2026 — понедельник
    CHECK((Date{2026, 11, 29}).dayOfWeek() == 6);  // воскресенье
    CHECK((Date{2026, 11, 23}).yearMonth() == "2026-11");
}

static void testText() {
    CHECK(utf8Length("Привет") == 6);
    CHECK(padRight("Да", 4) == "Да  ");
    CHECK(toLowerUtf8("ПРИВЕТ Ёжик") == "привет ёжик");
    CHECK(containsIgnoreCaseUtf8("Продукты питания и МОЛОКО", "молоко"));
    CHECK(startsWithIgnoreCaseUtf8("Кафе и рестораны", "кафе"));
    CHECK(htmlEscape("<a & \"b\">") == "&lt;a &amp; &quot;b&quot;&gt;");
    auto f = csvSplit("1;\"а;б\";\"в \"\"г\"\"\"");
    CHECK(f.size() == 3 && f[1] == "а;б" && f[2] == "в \"г\"");
    CHECK(csvField("а;б") == "\"а;б\"");
}

static void testLedgerCore() {
    Ledger l;
    l.setDefaults();
    l.setWalletInitial("Наличные", 1000);
    Date d{2026, 10, 1};
    CHECK(l.validate(makeOp(OpType::Expense, 0, "Продукты", "Карта", d)) != "");
    CHECK(l.validate(makeOp(OpType::Expense, 100, "Неизвестно", "Карта", d)) != "");
    CHECK(l.validate(makeOp(OpType::Expense, 100, "Продукты", "Сейф", d)) != "");
    CHECK(l.validate(makeOp(OpType::Expense, 100, "Продукты", "Карта", Date::today().addDays(1))) != "");
    CHECK(l.validate(makeOp(OpType::Expense, 100, "Продукты", "Карта", d)).empty());

    l.add(makeOp(OpType::Income, 100000, "Зарплата", "Карта", d, "аванс"));
    l.add(makeOp(OpType::Expense, 30000, "Продукты", "Карта", d, "Хлеб и Молоко"));
    int id = l.add(makeOp(OpType::Expense, 5000, "Кафе и рестораны", "Наличные", d, "кофе"));
    Operation tr = makeOp(OpType::Transfer, 20000, "", "Карта", d);
    tr.toWallet = "Наличные";
    l.add(tr);
    CHECK(l.walletBalance("Карта") == 100000 - 30000 - 20000);
    CHECK(l.walletBalance("Наличные") == 1000 - 5000 + 20000);
    CHECK(l.totalBalance() == 1000 + 100000 - 35000);

    PeriodStats s = monthStats(l, d);
    CHECK(s.income == 100000 && s.expense == 35000 && s.balance() == 65000);
    CHECK(s.expenseByCategory.front().category == "Продукты");
    CHECK(s.needs == 30000 && s.wants == 5000);

    // Поиск и фильтрация (регистронезависимый UTF-8)
    SearchFilter sf;
    sf.query = "молоко";
    auto found = l.search(sf);
    CHECK(found.size() == 1 && found[0].category == "Продукты");

    l.setBudget("Кафе и рестораны", 6000);
    CHECK(budgetStatus(l, "Кафе и рестораны", d).state == BudgetState::Near);
    l.setBudget("Кафе и рестораны", 4000);
    CHECK(budgetStatus(l, "Кафе и рестораны", d).state == BudgetState::Over);
    l.setBudget("Кафе и рестораны", 0);
    CHECK(l.budgets().empty());

    // Переименование кошелька
    std::string err;
    CHECK(l.renameWallet("Наличные", "Кошелёк", err));
    CHECK(l.walletBalance("Кошелёк") == 1000 - 5000 + 20000);
    CHECK(!l.removeWallet("Кошелёк", err));  // есть операции
    CHECK(l.remove(id) && !l.find(id));
}

static void testGoalsRecurringDebtsAndQuick() {
    Ledger l;
    l.setDefaults();
    l.setWalletInitial("Карта", 200000);
    Date today = Date::today();
    std::string err;

    // Быстрый ввод
    QuickParseResult q1 = parseQuickInput(l, "-18,50 прод молоко и сыр");
    CHECK(q1.ok && q1.type == OpType::Expense && q1.amount == 1850 && q1.category == "Продукты" &&
          q1.comment == "молоко и сыр");
    QuickParseResult q2 = parseQuickInput(l, "+250 подраб сайт");
    CHECK(q2.ok && q2.type == OpType::Income && q2.amount == 25000 && q2.category == "Подработка");
    QuickParseResult q3 = parseQuickInput(l, "-0 продукты");
    CHECK(!q3.ok);

    // Цели накоплений
    int gid = l.addGoal(SavingsGoal{0, "Ноутбук", 100000, 20000, today.addDays(30), false}, err);
    CHECK(gid > 0 && err.empty());
    int opId = l.contributeGoal(gid, 80000, "Карта", today, err);
    CHECK(opId > 0 && l.findGoal(gid)->completed && l.findGoal(gid)->savedAmount == 100000);

    // Регулярные платежи
    std::string prevYm = today.firstOfMonth().addMonths(-1).yearMonth();
    int rid = l.addRecurring(
        RecurringPayment{0, "Интернет", OpType::Expense, 3500, "Связь и интернет", "Карта", 1, prevYm, true}, err);
    CHECK(rid > 0);
    CHECK(l.dueRecurring(today).size() == 1);
    auto applied = l.applyAllDueRecurring(today);
    CHECK(applied.size() == 1 && l.dueRecurring(today).empty());

    // Долги
    int did = l.addDebt(Debt{0, "Иван", DebtDirection::OwedToMe, 15000, 0, today, today.addDays(10), "билет"}, err);
    CHECK(did > 0 && l.totalOwedToMe() == 15000);
    CHECK(l.repayDebt(did, 5000, true, "Карта", today, err));
    CHECK(l.totalOwedToMe() == 10000);
    CHECK(!l.repayDebt(did, 20000, false, "Карта", today, err));  // больше остатка

    // Индекс финансового здоровья и статистика дней недели
    l.add(makeOp(OpType::Income, 150000, "Зарплата", "Карта", today));
    FinancialHealth fh = computeFinancialHealth(l, today);
    CHECK(fh.score >= 0 && fh.score <= 100 && !fh.grade.empty());
    auto wd = weekdayBreakdown(l, today.firstOfMonth(), today.lastOfMonth());
    CHECK(wd.size() == 7);
    CHECK(!makeAdvice(l, today, today).empty());
}

static void testStorageBackupAndHtml() {
    namespace fs = std::filesystem;
    std::string dir = (fs::temp_directory_path() / "homeledger_v1_test").u8string();
    fs::remove_all(fs::u8path(dir));
    Storage st(dir);
    Ledger a;
    std::vector<std::string> w;
    CHECK(st.load(a, w) && w.empty() && !a.allCategories().empty());

    Date today = Date::today();
    Operation op = makeOp(OpType::Expense, 1234, "Продукты", "Карта", today, "хлеб; молоко \"2,5%\"");
    a.add(op);
    a.add(makeOp(OpType::Income, 90000, "Зарплата", "Карта", today));
    a.setBudget("Продукты", 50000);
    std::string err, snapName;
    a.addGoal(SavingsGoal{0, "Отпуск", 150000, 50000, today.addMonths(5), false}, err);
    a.addRecurring(RecurringPayment{0, "Связь", OpType::Expense, 2500, "Связь и интернет", "Карта", 1, "", true}, err);
    a.addDebt(Debt{0, "Олег", DebtDirection::IOwe, 4000, 1000, today, today.addDays(7), ""}, err);

    CHECK(st.save(a, err));
    CHECK(st.createNamedBackup("test", snapName, err));
    CHECK(st.listNamedBackups().size() == 1);

    Ledger b;
    CHECK(st.load(b, w) && w.empty());
    CHECK(b.operations().size() == 2 && b.operations()[0].comment == op.comment && b.operations()[0].amount == 1234);
    CHECK(b.budgetFor("Продукты") == 50000);
    CHECK(b.goals().size() == 1 && b.goals()[0].name == "Отпуск");
    CHECK(b.recurring().size() == 1 && b.recurring()[0].name == "Связь");
    CHECK(b.debts().size() == 1 && b.debts()[0].remaining() == 3000);

    // Проверка восстановления из снимка
    b.add(makeOp(OpType::Expense, 9999, "Одежда", "Карта", today));
    CHECK(b.operations().size() == 3);
    CHECK(st.restoreNamedBackup(snapName, b, w, err));
    CHECK(b.operations().size() == 2);

    std::string csvPath, htmlPath;
    CHECK(st.exportMonth(b, today, csvPath, err) && fs::exists(fs::u8path(csvPath)));
    CHECK(st.exportMonthHtml(b, today, htmlPath, err) && fs::exists(fs::u8path(htmlPath)));
    std::string html;
    {
        std::ifstream hin(fs::u8path(htmlPath), std::ios::binary);
        html.assign((std::istreambuf_iterator<char>(hin)), std::istreambuf_iterator<char>());
    }
    CHECK(html.find("<svg") != std::string::npos && html.find("Отпуск") != std::string::npos);

    std::error_code ec;
    fs::remove_all(fs::u8path(dir), ec);
}

static void testPerformance10k() {
    Ledger l;
    l.setDefaults();
    Date base{2026, 1, 1};
    for (int i = 0; i < 10000; ++i) {
        Date d = base.addDays(i % 300);
        l.add(makeOp(i % 5 == 0 ? OpType::Income : OpType::Expense, 100 + (i % 5000),
                     i % 5 == 0 ? "Зарплата" : "Продукты", "Карта", d, "тест"));
    }
    auto t0 = std::chrono::steady_clock::now();
    PeriodStats st = monthStats(l, Date{2026, 5, 10});
    FinancialHealth fh = computeFinancialHealth(l, Date{2026, 5, 10});
    SearchFilter sf;
    sf.query = "тест";
    auto sr = l.search(sf);
    auto t1 = std::chrono::steady_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
    CHECK(st.operations > 0 && fh.score >= 0 && sr.size() == 10000 && ms < 1000);
    std::cout << "  [PERF] 10 000 ops analytics+search: " << ms << " ms\n";
}

int main() {
    testMoney();
    testDate();
    testText();
    testLedgerCore();
    testGoalsRecurringDebtsAndQuick();
    testStorageBackupAndHtml();
    testPerformance10k();
    std::cout << (g_total - g_failed) << " из " << g_total << " проверок пройдено\n";
    return g_failed ? 1 : 0;
}
