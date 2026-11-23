// Storage.cpp — CSV-хранилище, резервные копии и экспорт в Excel CSV / HTML-дашборд
#include "Storage.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "Analytics.h"
#include "Text.h"

namespace fs = std::filesystem;

namespace {

const char* kOps = "operations.csv";
const char* kCats = "categories.csv";
const char* kWallets = "wallets.csv";
const char* kBudgets = "budgets.csv";
const char* kGoals = "goals.csv";
const char* kRecurring = "recurring.csv";
const char* kDebts = "debts.csv";

const char* typeCode(OpType t) {
    return t == OpType::Income ? "income" : t == OpType::Expense ? "expense" : "transfer";
}

bool parseType(const std::string& s, OpType& t) {
    if (s == "income") t = OpType::Income;
    else if (s == "expense") t = OpType::Expense;
    else if (s == "transfer") t = OpType::Transfer;
    else return false;
    return true;
}

const char* groupCode(Group g) {
    switch (g) {
        case Group::Needs: return "needs";
        case Group::Wants: return "wants";
        case Group::Savings: return "savings";
        default: return "income";
    }
}

Group parseGroup(const std::string& s) {
    if (s == "needs") return Group::Needs;
    if (s == "savings") return Group::Savings;
    if (s == "income") return Group::Income;
    return Group::Wants;
}

const char* debtCode(DebtDirection d) { return d == DebtDirection::OwedToMe ? "owed_to_me" : "i_owe"; }

bool parseDebtDirection(const std::string& s, DebtDirection& d) {
    if (s == "owed_to_me") {
        d = DebtDirection::OwedToMe;
        return true;
    }
    if (s == "i_owe") {
        d = DebtDirection::IOwe;
        return true;
    }
    return false;
}

bool readLines(const std::string& file, std::vector<std::string>& lines) {
    std::ifstream in(fs::u8path(file), std::ios::binary);
    if (!in) return false;
    std::string line;
    bool first = true;
    while (std::getline(in, line)) {
        if (first && line.rfind("\xEF\xBB\xBF", 0) == 0) line.erase(0, 3);  // BOM
        if (first) {
            first = false;  // заголовок
            continue;
        }
        if (!trim(line).empty()) lines.push_back(line);
    }
    return true;
}

// Атомарная запись: сначала во временный файл, затем замена
bool writeFile(const std::string& file, const std::string& content, std::string& error) {
    fs::path target = fs::u8path(file), tmp = fs::u8path(file + ".tmp");
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) {
            error = "Не удалось открыть файл для записи: " + file;
            return false;
        }
        out << content;
        if (!out.good()) {
            error = "Ошибка записи в файл: " + file;
            return false;
        }
    }
    std::error_code ec;
    fs::rename(tmp, target, ec);
    if (ec) {
        fs::remove(target, ec);
        ec.clear();
        fs::rename(tmp, target, ec);
    }
    if (ec) {
        error = "Не удалось сохранить файл " + file + ": " + ec.message();
        return false;
    }
    return true;
}

std::string moneyForExcel(Money m) {
    std::string s = moneyToCsv(m);
    for (auto& c : s)
        if (c == '.') c = ',';
    return s;
}

std::string sanitizeTag(const std::string& tag) {
    std::string r;
    for (unsigned char c : tag) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_')
            r += static_cast<char>(c);
        else if (c == ' ')
            r += '_';
    }
    return r;
}

std::string currentTimestamp() {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[32];
    std::snprintf(buf, sizeof buf, "%04d%02d%02d_%02d%02d%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                  tm.tm_hour, tm.tm_min, tm.tm_sec);
    return buf;
}

std::string fmtPct1(double v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.1f", v);
    return buf;
}

std::string fmtDouble1Comma(double v) {
    std::string s = fmtPct1(v);
    for (char& c : s)
        if (c == '.') c = ',';
    return s;
}

}  // namespace

std::string moneyToCsv(Money m) {
    std::string s = formatMoney(m);
    std::string r;
    for (char c : s) {
        if (c == ' ') continue;
        r += (c == ',') ? '.' : c;
    }
    return r;
}

std::string Storage::path(const std::string& file) const { return (fs::u8path(dir_) / fs::u8path(file)).u8string(); }

bool Storage::loadFromDir(const std::string& targetDir, Ledger& ledger, std::vector<std::string>& warnings) const {
    auto fileIn = [&](const char* f) { return (fs::u8path(targetDir) / fs::u8path(f)).u8string(); };
    ledger.clear();
    std::vector<std::string> lines;
    readLines(fileIn(kCats), lines);
    for (const auto& l : lines) {
        auto f = csvSplit(l);
        OpType t;
        if (f.size() < 3 || !parseType(f[1], t) || f[0].empty()) {
            warnings.push_back("categories.csv: пропущена строка «" + l + "»");
            continue;
        }
        ledger.loadCategory(Category{f[0], t, parseGroup(f[2])});
    }
    lines.clear();
    readLines(fileIn(kWallets), lines);
    for (const auto& l : lines) {
        auto f = csvSplit(l);
        Money m = 0;
        if (f.size() < 2 || f[0].empty() || !parseMoney(f[1].rfind('-', 0) == 0 ? f[1].substr(1) : f[1], m)) {
            warnings.push_back("wallets.csv: пропущена строка «" + l + "»");
            continue;
        }
        if (f[1].rfind('-', 0) == 0) m = -m;
        ledger.loadWallet(Wallet{f[0], m});
    }
    if (ledger.wallets().empty()) ledger.loadWallet(Wallet{"Наличные", 0});

    lines.clear();
    readLines(fileIn(kBudgets), lines);
    for (const auto& l : lines) {
        auto f = csvSplit(l);
        Money m = 0;
        if (f.size() < 2 || !parseMoney(f[1], m)) {
            warnings.push_back("budgets.csv: пропущена строка «" + l + "»");
            continue;
        }
        ledger.loadBudget(Budget{f[0], m});
    }

    lines.clear();
    readLines(fileIn(kOps), lines);
    for (const auto& l : lines) {
        auto f = csvSplit(l);
        Operation op;
        if (f.size() < 8 || !parseType(f[2], op.type) || !Date::parse(f[1], op.date) || f[1].empty() ||
            !parseMoney(f[3], op.amount)) {
            warnings.push_back("operations.csv: пропущена повреждённая строка «" + l + "»");
            continue;
        }
        try {
            op.id = std::stoi(f[0]);
        } catch (...) {
            warnings.push_back("operations.csv: неверный номер в строке «" + l + "»");
            continue;
        }
        op.category = f[4];
        op.wallet = f[5];
        op.toWallet = f[6];
        op.comment = f[7];
        ledger.loadOperation(op);
    }

    // Новые файлы (v1.0.0) — если отсутствуют, пропускаются без ошибок (обратная совместимость с v0.2)
    lines.clear();
    if (readLines(fileIn(kGoals), lines)) {
        for (const auto& l : lines) {
            auto f = csvSplit(l);
            SavingsGoal g;
            if (f.size() < 6 || f[1].empty() || !parseMoney(f[2], g.targetAmount) || !parseMoney(f[3], g.savedAmount) ||
                !Date::parse(f[4], g.deadline) || f[4].empty()) {
                warnings.push_back("goals.csv: пропущена строка «" + l + "»");
                continue;
            }
            try {
                g.id = std::stoi(f[0]);
            } catch (...) {
                warnings.push_back("goals.csv: неверный номер в строке «" + l + "»");
                continue;
            }
            g.name = f[1];
            g.completed = (f[5] == "1" || f[5] == "true") || (g.savedAmount >= g.targetAmount);
            ledger.loadGoal(g);
        }
    }

    lines.clear();
    if (readLines(fileIn(kRecurring), lines)) {
        for (const auto& l : lines) {
            auto f = csvSplit(l);
            RecurringPayment r;
            if (f.size() < 9 || f[1].empty() || !parseType(f[2], r.type) || r.type == OpType::Transfer ||
                !parseMoney(f[3], r.amount)) {
                warnings.push_back("recurring.csv: пропущена строка «" + l + "»");
                continue;
            }
            try {
                r.id = std::stoi(f[0]);
                r.dayOfMonth = std::stoi(f[6]);
            } catch (...) {
                warnings.push_back("recurring.csv: неверное число в строке «" + l + "»");
                continue;
            }
            r.name = f[1];
            r.category = f[4];
            r.wallet = f[5];
            r.lastAppliedYm = f[7];
            r.active = (f[8] == "1" || f[8] == "true");
            ledger.loadRecurring(r);
        }
    }

    lines.clear();
    if (readLines(fileIn(kDebts), lines)) {
        for (const auto& l : lines) {
            auto f = csvSplit(l);
            Debt d;
            if (f.size() < 8 || f[1].empty() || !parseDebtDirection(f[2], d.direction) || !parseMoney(f[3], d.amount) ||
                !parseMoney(f[4], d.repaid) || !Date::parse(f[5], d.created) || f[5].empty() ||
                !Date::parse(f[6], d.dueDate) || f[6].empty()) {
                warnings.push_back("debts.csv: пропущена строка «" + l + "»");
                continue;
            }
            try {
                d.id = std::stoi(f[0]);
            } catch (...) {
                warnings.push_back("debts.csv: неверный номер в строке «" + l + "»");
                continue;
            }
            d.person = f[1];
            d.comment = f[7];
            ledger.loadDebt(d);
        }
    }
    return true;
}

bool Storage::load(Ledger& ledger, std::vector<std::string>& warnings) {
    ledger.clear();
    std::error_code ec;
    if (!fs::exists(fs::u8path(path(kCats)), ec)) {
        fs::create_directories(fs::u8path(dir_), ec);
        ledger.setDefaults();
        std::string err;
        if (!save(ledger, err)) warnings.push_back(err);
        return true;
    }
    return loadFromDir(dir_, ledger, warnings);
}

bool Storage::save(const Ledger& ledger, std::string& error) const {
    std::error_code ec;
    fs::create_directories(fs::u8path(dir_), ec);
    std::ostringstream ops, cats, wallets, budgets, goals, rec, debts;
    ops << "id;date;type;amount;category;wallet;to_wallet;comment\n";
    for (const auto& o : ledger.operations())
        ops << o.id << ';' << o.date.iso() << ';' << typeCode(o.type) << ';' << moneyToCsv(o.amount) << ';'
            << csvField(o.category) << ';' << csvField(o.wallet) << ';' << csvField(o.toWallet) << ';'
            << csvField(o.comment) << '\n';

    cats << "name;type;group\n";
    for (const auto& c : ledger.allCategories())
        cats << csvField(c.name) << ';' << typeCode(c.type) << ';' << groupCode(c.group) << '\n';

    wallets << "name;initial\n";
    for (const auto& w : ledger.wallets()) wallets << csvField(w.name) << ';' << moneyToCsv(w.initial) << '\n';

    budgets << "category;limit\n";
    for (const auto& b : ledger.budgets()) budgets << csvField(b.category) << ';' << moneyToCsv(b.limit) << '\n';

    goals << "id;name;target;saved;deadline;completed\n";
    for (const auto& g : ledger.goals())
        goals << g.id << ';' << csvField(g.name) << ';' << moneyToCsv(g.targetAmount) << ';' << moneyToCsv(g.savedAmount)
              << ';' << g.deadline.iso() << ';' << (g.completed ? "1" : "0") << '\n';

    rec << "id;name;type;amount;category;wallet;day;last_ym;active\n";
    for (const auto& r : ledger.recurring())
        rec << r.id << ';' << csvField(r.name) << ';' << typeCode(r.type) << ';' << moneyToCsv(r.amount) << ';'
            << csvField(r.category) << ';' << csvField(r.wallet) << ';' << r.dayOfMonth << ';' << csvField(r.lastAppliedYm)
            << ';' << (r.active ? "1" : "0") << '\n';

    debts << "id;person;direction;amount;repaid;created;due_date;comment\n";
    for (const auto& d : ledger.debts())
        debts << d.id << ';' << csvField(d.person) << ';' << debtCode(d.direction) << ';' << moneyToCsv(d.amount) << ';'
              << moneyToCsv(d.repaid) << ';' << d.created.iso() << ';' << d.dueDate.iso() << ';' << csvField(d.comment)
              << '\n';

    return writeFile(path(kOps), ops.str(), error) && writeFile(path(kCats), cats.str(), error) &&
           writeFile(path(kWallets), wallets.str(), error) && writeFile(path(kBudgets), budgets.str(), error) &&
           writeFile(path(kGoals), goals.str(), error) && writeFile(path(kRecurring), rec.str(), error) &&
           writeFile(path(kDebts), debts.str(), error);
}

bool Storage::backup(std::string& error) const {
    std::error_code ec;
    fs::path dst = fs::u8path(dir_) / "backup";
    fs::create_directories(dst, ec);
    for (const char* f : {kOps, kCats, kWallets, kBudgets, kGoals, kRecurring, kDebts}) {
        fs::path src = fs::u8path(path(f));
        if (!fs::exists(src, ec)) continue;
        fs::copy_file(src, dst / (std::string(f) + ".bak"), fs::copy_options::overwrite_existing, ec);
        if (ec) {
            error = "Не удалось создать резервную копию: " + ec.message();
            return false;
        }
    }
    return true;
}

bool Storage::createNamedBackup(const std::string& tag, std::string& outName, std::string& error) const {
    std::error_code ec;
    fs::path snapRoot = fs::u8path(dir_) / "backup" / "snapshots";
    fs::create_directories(snapRoot, ec);
    std::string safeTag = sanitizeTag(tag);
    std::string base = currentTimestamp() + (safeTag.empty() ? "" : "_" + safeTag);
    std::string name = base;
    int suffix = 1;
    while (fs::exists(snapRoot / fs::u8path(name), ec)) {
        name = base + "_" + std::to_string(suffix++);
    }
    fs::path dst = snapRoot / fs::u8path(name);
    fs::create_directories(dst, ec);
    if (ec) {
        error = "Не удалось создать каталог снимка: " + ec.message();
        return false;
    }
    int copied = 0;
    for (const char* f : {kOps, kCats, kWallets, kBudgets, kGoals, kRecurring, kDebts}) {
        fs::path src = fs::u8path(path(f));
        if (!fs::exists(src, ec)) continue;
        fs::copy_file(src, dst / fs::u8path(f), fs::copy_options::overwrite_existing, ec);
        if (ec) {
            error = "Ошибка копирования в снимок: " + ec.message();
            return false;
        }
        ++copied;
    }
    if (copied == 0) {
        fs::remove_all(dst, ec);
        error = "Нет файлов данных для резервного копирования.";
        return false;
    }
    // Ротация: храним не более 10 последних снимков
    auto snaps = listNamedBackups();
    while (snaps.size() > 10) {
        fs::remove_all(snapRoot / fs::u8path(snaps.back().name), ec);
        snaps.pop_back();
    }
    outName = name;
    return true;
}

std::vector<BackupInfo> Storage::listNamedBackups() const {
    std::vector<BackupInfo> res;
    std::error_code ec;
    fs::path snapRoot = fs::u8path(dir_) / "backup" / "snapshots";
    if (!fs::exists(snapRoot, ec)) return res;
    for (const auto& entry : fs::directory_iterator(snapRoot, ec)) {
        if (!entry.is_directory(ec)) continue;
        std::string name = entry.path().filename().u8string();
        int count = 0;
        for (const auto& f : fs::directory_iterator(entry.path(), ec)) {
            if (f.is_regular_file(ec)) ++count;
        }
        std::string label = name;
        if (name.size() >= 15 && name[8] == '_') {
            label = name.substr(6, 2) + "." + name.substr(4, 2) + "." + name.substr(0, 4) + " " +
                    name.substr(9, 2) + ":" + name.substr(11, 2) + ":" + name.substr(13, 2);
            if (name.size() > 16) label += " (" + name.substr(16) + ")";
        }
        res.push_back(BackupInfo{name, label, count});
    }
    std::sort(res.begin(), res.end(), [](const BackupInfo& a, const BackupInfo& b) { return a.name > b.name; });
    return res;
}

bool Storage::restoreNamedBackup(const std::string& name, Ledger& ledger, std::vector<std::string>& warnings,
                                 std::string& error) const {
    std::error_code ec;
    fs::path srcDir = fs::u8path(dir_) / "backup" / "snapshots" / fs::u8path(name);
    if (!fs::exists(srcDir / fs::u8path(kCats), ec)) {
        error = "Снимок «" + name + "» не найден или повреждён.";
        return false;
    }
    Ledger loaded;
    loadFromDir(srcDir.u8string(), loaded, warnings);
    ledger = std::move(loaded);
    return save(ledger, error);
}

bool Storage::exportMonth(const Ledger& ledger, const Date& month, std::string& outPath, std::string& error) const {
    char name[64];
    std::snprintf(name, sizeof name, "report_%04d-%02d.csv", month.y, month.m);
    outPath = path(name);
    PeriodStats s = monthStats(ledger, month);
    std::ostringstream out;
    out << "\xEF\xBB\xBF";  // BOM — чтобы Excel распознал UTF-8
    out << "Отчёт HomeLedger;" << month.monthName() << "\n\n";
    out << "Показатель;Сумма, BYN\n";
    out << "Доходы;" << moneyForExcel(s.income) << "\nРасходы;" << moneyForExcel(s.expense) << "\nОстаток месяца;"
        << moneyForExcel(s.balance()) << "\nБаланс всех кошельков;" << moneyForExcel(ledger.totalBalance()) << "\n\n";
    out << "Категория расходов;Сумма, BYN;Доля, %;Лимит, BYN\n";
    for (const auto& c : s.expenseByCategory) {
        Money limit = ledger.budgetFor(c.category);
        out << csvField(c.category) << ';' << moneyForExcel(c.amount) << ';' << fmtDouble1Comma(c.share) << ';'
            << (limit ? moneyForExcel(limit) : "") << '\n';
    }
    out << "\nДата;Тип;Категория;Кошелёк;Сумма, BYN;Комментарий\n";
    for (const auto& o : ledger.between(month.firstOfMonth(), month.lastOfMonth())) {
        std::string cat = o.type == OpType::Transfer ? "→ " + o.toWallet : o.category;
        out << o.date.str() << ';' << typeName(o.type) << ';' << csvField(cat) << ';' << csvField(o.wallet) << ';'
            << moneyForExcel(o.amount) << ';' << csvField(o.comment) << '\n';
    }
    return writeFile(outPath, out.str(), error);
}

bool Storage::exportMonthHtml(const Ledger& ledger, const Date& month, std::string& outPath, std::string& error) const {
    char name[64];
    std::snprintf(name, sizeof name, "report_%04d-%02d.html", month.y, month.m);
    outPath = path(name);

    PeriodStats s = monthStats(ledger, month);
    FinancialHealth fh = computeFinancialHealth(ledger, month);
    auto dyn = monthlyDynamics(ledger, month, 6);
    auto budgets = budgetStatuses(ledger, month);
    auto advices = makeAdvice(ledger, month, Date::today());
    auto ops = ledger.between(month.firstOfMonth(), month.lastOfMonth());

    const char* kPalette[] = {"#2563eb", "#0ea5e9", "#10b981", "#f59e0b", "#ef4444", "#8b5cf6", "#64748b"};
    constexpr double kPi = 3.14159265358979323846;

    std::ostringstream h;
    h << "<!DOCTYPE html>\n<html lang=\"ru\">\n<head>\n<meta charset=\"UTF-8\">\n"
      << "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n"
      << "<title>HomeLedger — Отчёт за " << htmlEscape(month.monthName()) << "</title>\n"
      << "<style>\n"
      << ":root{--bg:#f1f5f9;--card:#ffffff;--text:#0f172a;--muted:#64748b;--border:#e2e8f0;"
      << "--green:#16a34a;--red:#dc2626;--blue:#2563eb;--amber:#d97706;}\n"
      << "*{box-sizing:border-box;margin:0;padding:0;}\n"
      << "body{font-family:system-ui,-apple-system,\"Segoe UI\",Roboto,sans-serif;background:var(--bg);color:var(--text);padding:28px 20px;line-height:1.45;}\n"
      << ".wrap{max-width:1180px;margin:0 auto;display:flex;flex-direction:column;gap:20px;}\n"
      << ".hdr{display:flex;justify-content:space-between;align-items:center;flex-wrap:wrap;gap:12px;background:linear-gradient(135deg,#0f172a,#1e3a8a);color:#fff;padding:22px 26px;border-radius:14px;}\n"
      << ".hdr h1{font-size:22px;font-weight:700;letter-spacing:-0.01em;}\n"
      << ".hdr .sub{font-size:13px;color:#cbd5e1;margin-top:4px;}\n"
      << ".badge{background:rgba(255,255,255,0.14);padding:6px 14px;border-radius:999px;font-size:13px;font-weight:600;}\n"
      << ".grid-kpi{display:grid;grid-template-columns:repeat(auto-fit,minmax(200px,1fr));gap:14px;}\n"
      << ".kpi{background:var(--card);border:1px solid var(--border);border-radius:12px;padding:16px 18px;}\n"
      << ".kpi .lbl{font-size:12px;color:var(--muted);text-transform:uppercase;letter-spacing:0.04em;font-weight:600;}\n"
      << ".kpi .val{font-size:22px;font-weight:700;margin-top:6px;}\n"
      << ".kpi .hint{font-size:12px;color:var(--muted);margin-top:4px;}\n"
      << ".pos{color:var(--green);} .neg{color:var(--red);} .acc{color:var(--blue);}\n"
      << ".row2{display:grid;grid-template-columns:1fr 1fr;gap:18px;}\n"
      << "@media(max-width:880px){.row2{grid-template-columns:1fr;}}\n"
      << ".card{background:var(--card);border:1px solid var(--border);border-radius:12px;padding:20px;}\n"
      << ".card h2{font-size:16px;font-weight:700;margin-bottom:14px;color:var(--text);}\n"
      << ".donut-wrap{display:flex;align-items:center;gap:22px;flex-wrap:wrap;}\n"
      << ".legend{flex:1;min-width:210px;display:flex;flex-direction:column;gap:8px;}\n"
      << ".leg-item{display:flex;align-items:center;justify-content:space-between;font-size:13px;gap:8px;}\n"
      << ".leg-left{display:flex;align-items:center;gap:8px;}\n"
      << ".dot{width:10px;height:10px;border-radius:3px;flex-shrink:0;}\n"
      << ".bar-row{margin-bottom:12px;}\n"
      << ".bar-top{display:flex;justify-content:space-between;font-size:13px;margin-bottom:5px;}\n"
      << ".track{height:9px;background:#e2e8f0;border-radius:999px;overflow:hidden;}\n"
      << ".fill{height:100%;border-radius:999px;}\n"
      << ".adv{padding:10px 14px;border-radius:9px;margin-bottom:8px;font-size:13px;border-left:4px solid;}\n"
      << ".adv-g{background:#f0fdf4;border-color:var(--green);} .adv-w{background:#fffbeb;border-color:var(--amber);} .adv-i{background:#eff6ff;border-color:var(--blue);}\n"
      << ".adv strong{display:block;margin-bottom:2px;}\n"
      << ".toolbar{display:flex;gap:10px;flex-wrap:wrap;margin-bottom:12px;}\n"
      << ".toolbar input,.toolbar select{padding:8px 12px;border:1px solid var(--border);border-radius:8px;font-size:13px;background:#fff;}\n"
      << ".toolbar input{flex:1;min-width:200px;}\n"
      << "table{width:100%;border-collapse:collapse;font-size:13px;}\n"
      << "th,td{padding:9px 10px;border-bottom:1px solid var(--border);text-align:left;}\n"
      << "th{color:var(--muted);font-weight:600;background:#f8fafc;}\n"
      << "td.num,th.num{text-align:right;font-variant-numeric:tabular-nums;white-space:nowrap;}\n"
      << ".tag{display:inline-block;padding:2px 8px;border-radius:6px;font-size:11px;font-weight:600;}\n"
      << ".tag-inc{background:#dcfce7;color:#166534;} .tag-exp{background:#fee2e2;color:#991b1b;} .tag-tr{background:#dbeafe;color:#1e40af;}\n"
      << "</style>\n</head>\n<body>\n<div class=\"wrap\">\n";

    // Шапка
    h << "<div class=\"hdr\"><div><h1>HomeLedger — Финансовый отчёт за " << htmlEscape(month.monthName())
      << "</h1><div class=\"sub\">Автономный аналитический дашборд · Сформировано "
      << htmlEscape(Date::today().str()) << "</div></div>"
      << "<div class=\"badge\">Индекс здоровья: " << fh.score << " / 100 (" << htmlEscape(fh.grade) << ")</div></div>\n";

    // KPI-карточки
    h << "<div class=\"grid-kpi\">\n";
    h << "<div class=\"kpi\"><div class=\"lbl\">Общий баланс</div><div class=\"val acc\">"
      << htmlEscape(formatMoney(ledger.totalBalance())) << " BYN</div><div class=\"hint\">";
    bool firstW = true;
    for (const auto& w : ledger.wallets()) {
        if (!firstW) h << " · ";
        h << htmlEscape(w.name) << ": " << htmlEscape(formatMoney(ledger.walletBalance(w.name)));
        firstW = false;
    }
    h << "</div></div>\n";

    h << "<div class=\"kpi\"><div class=\"lbl\">Доходы за месяц</div><div class=\"val pos\">+"
      << htmlEscape(formatMoney(s.income)) << " BYN</div><div class=\"hint\">Поступления без переводов</div></div>\n";

    h << "<div class=\"kpi\"><div class=\"lbl\">Расходы за месяц</div><div class=\"val neg\">−"
      << htmlEscape(formatMoney(s.expense)) << " BYN</div><div class=\"hint\">Операций за период: " << s.operations
      << "</div></div>\n";

    h << "<div class=\"kpi\"><div class=\"lbl\">Остаток месяца</div><div class=\"val "
      << (s.balance() >= 0 ? "pos" : "neg") << "\">" << (s.balance() >= 0 ? "+" : "")
      << htmlEscape(formatMoney(s.balance())) << " BYN</div><div class=\"hint\">Подушка безопасности: "
      << fmtDouble1Comma(fh.safetyCushionMonths) << " мес.</div></div>\n";
    h << "</div>\n";

    // Графики: Donut + 6-месячная динамика
    h << "<div class=\"row2\">\n";

    // Левая карточка: Donut Chart
    h << "<div class=\"card\"><h2>Структура расходов по категориям</h2><div class=\"donut-wrap\">\n";
    if (s.expense <= 0 || s.expenseByCategory.empty()) {
        h << "<p style=\"color:var(--muted);font-size:13px\">В выбранном месяце расходов нет.</p>";
    } else {
        struct Slice {
            std::string label;
            Money amount;
            double share;
            const char* color;
        };
        std::vector<Slice> slices;
        Money otherSum = 0;
        for (size_t i = 0; i < s.expenseByCategory.size(); ++i) {
            if (i < 6) {
                slices.push_back(Slice{s.expenseByCategory[i].category, s.expenseByCategory[i].amount,
                                       s.expenseByCategory[i].share, kPalette[i]});
            } else {
                otherSum += s.expenseByCategory[i].amount;
            }
        }
        if (otherSum > 0) {
            slices.push_back(Slice{"Прочие категории", otherSum, percent(otherSum, s.expense), kPalette[6]});
        }
        h << "<svg width=\"190\" height=\"190\" viewBox=\"0 0 190 190\">\n";
        double cx = 95.0, cy = 95.0, rOut = 84.0, rIn = 50.0;
        if (slices.size() == 1) {
            h << "<circle cx=\"95\" cy=\"95\" r=\"67\" fill=\"none\" stroke=\"" << slices[0].color
              << "\" stroke-width=\"34\"/>\n";
        } else {
            double angle = -kPi / 2.0;
            for (const auto& sl : slices) {
                double frac = static_cast<double>(sl.amount) / static_cast<double>(s.expense);
                double sweep = frac * 2.0 * kPi;
                if (sweep <= 0.001) continue;
                if (sweep > 2.0 * kPi - 0.002) sweep = 2.0 * kPi - 0.002;
                double endAngle = angle + sweep;
                int largeArc = sweep > kPi ? 1 : 0;
                double x1 = cx + rOut * std::cos(angle), y1 = cy + rOut * std::sin(angle);
                double x2 = cx + rOut * std::cos(endAngle), y2 = cy + rOut * std::sin(endAngle);
                double x3 = cx + rIn * std::cos(endAngle), y3 = cy + rIn * std::sin(endAngle);
                double x4 = cx + rIn * std::cos(angle), y4 = cy + rIn * std::sin(angle);
                h << "<path d=\"M " << x1 << " " << y1 << " A " << rOut << " " << rOut << " 0 " << largeArc << " 1 "
                  << x2 << " " << y2 << " L " << x3 << " " << y3 << " A " << rIn << " " << rIn << " 0 " << largeArc
                  << " 0 " << x4 << " " << y4 << " Z\" fill=\"" << sl.color << "\" stroke=\"#ffffff\" stroke-width=\"1.5\"/>\n";
                angle = endAngle;
            }
        }
        h << "<text x=\"95\" y=\"91\" text-anchor=\"middle\" font-size=\"11\" fill=\"#64748b\">Всего</text>\n"
          << "<text x=\"95\" y=\"108\" text-anchor=\"middle\" font-size=\"13\" font-weight=\"700\" fill=\"#0f172a\">"
          << htmlEscape(formatMoney(s.expense)) << "</text>\n</svg>\n<div class=\"legend\">\n";
        for (const auto& sl : slices) {
            h << "<div class=\"leg-item\"><span class=\"leg-left\"><span class=\"dot\" style=\"background:"
              << sl.color << "\"></span>" << htmlEscape(sl.label) << "</span><span><b>"
              << htmlEscape(formatMoney(sl.amount)) << "</b> (" << fmtDouble1Comma(sl.share) << " %)</span></div>\n";
        }
        h << "</div>\n";
    }
    h << "</div></div>\n";

    // Правая карточка: 6-месячная динамика SVG
    h << "<div class=\"card\"><h2>Динамика доходов и расходов за 6 месяцев</h2>\n";
    Money maxDyn = 1;
    for (const auto& d : dyn) maxDyn = std::max({maxDyn, d.income, d.expense});
    h << "<svg width=\"100%\" height=\"210\" viewBox=\"0 0 500 210\">\n";
    for (int g = 0; g <= 4; ++g) {
        int y = 20 + g * 38;
        h << "<line x1=\"36\" y1=\"" << y << "\" x2=\"490\" y2=\"" << y << "\" stroke=\"#e2e8f0\" stroke-width=\"1\"/>\n";
    }
    for (size_t i = 0; i < dyn.size(); ++i) {
        double baseX = 48.0 + static_cast<double>(i) * 74.0;
        double incH = 152.0 * static_cast<double>(dyn[i].income) / static_cast<double>(maxDyn);
        double expH = 152.0 * static_cast<double>(dyn[i].expense) / static_cast<double>(maxDyn);
        double incY = 172.0 - incH;
        double expY = 172.0 - expH;
        h << "<rect x=\"" << baseX << "\" y=\"" << incY << "\" width=\"22\" height=\"" << incH
          << "\" rx=\"4\" fill=\"#16a34a\"/>\n";
        h << "<rect x=\"" << (baseX + 26.0) << "\" y=\"" << expY << "\" width=\"22\" height=\"" << expH
          << "\" rx=\"4\" fill=\"#dc2626\"/>\n";
        char mLbl[16];
        std::snprintf(mLbl, sizeof mLbl, "%02d.%04d", dyn[i].from.m, dyn[i].from.y);
        h << "<text x=\"" << (baseX + 24.0) << "\" y=\"192\" text-anchor=\"middle\" font-size=\"11\" fill=\"#64748b\">"
          << mLbl << "</text>\n";
    }
    h << "<rect x=\"36\" y=\"2\" width=\"10\" height=\"10\" rx=\"2\" fill=\"#16a34a\"/>"
      << "<text x=\"50\" y=\"11\" font-size=\"11\" fill=\"#475569\">Доходы</text>"
      << "<rect x=\"108\" y=\"2\" width=\"10\" height=\"10\" rx=\"2\" fill=\"#dc2626\"/>"
      << "<text x=\"122\" y=\"11\" font-size=\"11\" fill=\"#475569\">Расходы</text>"
      << "</svg></div>\n";
    h << "</div>\n";

    // Правило 50/30/20 и Лимиты
    h << "<div class=\"row2\">\n";
    h << "<div class=\"card\"><h2>Правило 50/30/20 и бюджетные лимиты</h2>\n";
    if (s.income > 0) {
        struct R50 {
            const char* name;
            Money val;
            double norm;
            bool atMost;
        } r50[] = {{"Необходимое (≤ 50 %)", s.needs, 50.0, true},
                   {"Желания (≤ 30 %)", s.wants, 30.0, true},
                   {"Сбережения и остаток (≥ 20 %)", s.savings + std::max<Money>(0, s.balance()), 20.0, false}};
        for (const auto& r : r50) {
            double p = percent(r.val, s.income);
            bool ok = r.atMost ? (p <= r.norm) : (p >= r.norm);
            double w = std::min(100.0, p);
            h << "<div class=\"bar-row\"><div class=\"bar-top\"><span>" << r.name << "</span><span><b>"
              << fmtDouble1Comma(p) << " %</b> (" << htmlEscape(formatMoney(r.val))
              << " BYN)</span></div><div class=\"track\"><div class=\"fill\" style=\"width:" << w << "%;background:"
              << (ok ? "#16a34a" : "#d97706") << "\"></div></div></div>\n";
        }
    }
    if (!budgets.empty()) {
        h << "<h3 style=\"font-size:14px;margin:14px 0 8px\">Контроль лимитов</h3>\n";
        for (const auto& b : budgets) {
            const char* col = b.state == BudgetState::Over ? "#dc2626"
                              : b.state == BudgetState::Near ? "#d97706" : "#2563eb";
            double w = std::min(100.0, b.usedPercent);
            h << "<div class=\"bar-row\"><div class=\"bar-top\"><span>" << htmlEscape(b.category) << "</span><span>"
              << htmlEscape(formatMoney(b.spent)) << " / " << htmlEscape(formatMoney(b.limit)) << " BYN ("
              << fmtDouble1Comma(b.usedPercent) << " %)</span></div><div class=\"track\"><div class=\"fill\" style=\"width:"
              << w << "%;background:" << col << "\"></div></div></div>\n";
        }
    }
    if (!ledger.debts().empty()) {
        h << "<h3 style=\"font-size:14px;margin:14px 0 8px\">Долги и займы</h3>\n";
        for (const auto& d : ledger.debts()) {
            h << "<div class=\"bar-row\"><div class=\"bar-top\"><span>" << htmlEscape(debtDirectionName(d.direction))
              << ": " << htmlEscape(d.person) << " (до " << htmlEscape(d.dueDate.str()) << ")</span><span>"
              << htmlEscape(formatMoney(d.remaining())) << " / " << htmlEscape(formatMoney(d.amount))
              << " BYN</span></div><div class=\"track\"><div class=\"fill\" style=\"width:"
              << std::min(100.0, percent(d.repaid, d.amount)) << "%;background:"
              << (d.settled() ? "#16a34a" : "#64748b") << "\"></div></div></div>\n";
        }
    }
    h << "</div>\n";

    // Советы + цели и долги
    h << "<div class=\"card\"><h2>Советы и финансовые цели</h2>\n";
    for (const auto& a : advices) {
        const char* cls = a.level == AdviceLevel::Good ? "adv-g" : a.level == AdviceLevel::Warning ? "adv-w" : "adv-i";
        h << "<div class=\"adv " << cls << "\"><strong>" << htmlEscape(a.title) << "</strong>"
          << htmlEscape(a.text) << "</div>\n";
    }
    if (!ledger.goals().empty()) {
        h << "<h3 style=\"font-size:14px;margin:14px 0 8px\">Цели накоплений</h3>\n";
        for (const auto& g : ledger.goals()) {
            double pct = std::min(100.0, percent(g.savedAmount, g.targetAmount));
            h << "<div class=\"bar-row\"><div class=\"bar-top\"><span>" << htmlEscape(g.name) << " (до "
              << htmlEscape(g.deadline.str()) << ")</span><span>" << htmlEscape(formatMoney(g.savedAmount)) << " / "
              << htmlEscape(formatMoney(g.targetAmount)) << " BYN (" << fmtDouble1Comma(pct)
              << " %)</span></div><div class=\"track\"><div class=\"fill\" style=\"width:" << pct
              << "%;background:" << (g.completed ? "#16a34a" : "#2563eb") << "\"></div></div></div>\n";
        }
    }
    h << "</div>\n</div>\n";

    // Таблица операций с клиентским фильтром
    h << "<div class=\"card\"><h2>Журнал операций за месяц (" << ops.size() << ")</h2>\n"
      << "<div class=\"toolbar\"><input id=\"q\" type=\"text\" placeholder=\"Поиск по категории, кошельку или комментарию...\">"
      << "<select id=\"t\"><option value=\"\">Все типы</option><option value=\"Расход\">Расход</option>"
      << "<option value=\"Доход\">Доход</option><option value=\"Перевод\">Перевод</option></select></div>\n"
      << "<table><thead><tr><th>№</th><th>Дата</th><th>Тип</th><th>Категория</th><th>Кошелёк</th><th class=\"num\">Сумма, BYN</th><th>Комментарий</th></tr></thead>\n<tbody id=\"tb\">\n";
    for (const auto& o : ops) {
        std::string cat = o.type == OpType::Transfer ? "→ " + o.toWallet : o.category;
        const char* tagCls = o.type == OpType::Income ? "tag-inc" : o.type == OpType::Expense ? "tag-exp" : "tag-tr";
        std::string sum = (o.type == OpType::Income ? "+" : o.type == OpType::Expense ? "−" : "") + formatMoney(o.amount);
        h << "<tr data-type=\"" << htmlEscape(typeName(o.type)) << "\"><td>" << o.id << "</td><td>"
          << htmlEscape(o.date.str()) << "</td><td><span class=\"tag " << tagCls << "\">" << htmlEscape(typeName(o.type))
          << "</span></td><td>" << htmlEscape(cat) << "</td><td>" << htmlEscape(o.wallet)
          << "</td><td class=\"num\">" << htmlEscape(sum) << "</td><td>" << htmlEscape(o.comment) << "</td></tr>\n";
    }
    h << "</tbody></table></div>\n</div>\n"
      << "<script>\n"
      << "(function(){var q=document.getElementById('q'),t=document.getElementById('t'),tb=document.getElementById('tb');"
      << "function f(){var s=q.value.toLowerCase(),tp=t.value;Array.prototype.forEach.call(tb.rows,function(r){"
      << "var ok=(!tp||r.getAttribute('data-type')===tp)&&(!s||r.textContent.toLowerCase().indexOf(s)!==-1);"
      << "r.style.display=ok?'':'none';});}q.addEventListener('input',f);t.addEventListener('change',f);})();\n"
      << "</script>\n</body>\n</html>\n";

    return writeFile(outPath, h.str(), error);
}
