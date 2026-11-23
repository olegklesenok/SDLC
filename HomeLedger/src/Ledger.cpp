// Ledger.cpp — правила ведения учёта, цели, регулярные платежи, долги и поиск
#include "Ledger.h"

#include <algorithm>

#include "Text.h"

const char* typeName(OpType t) {
    switch (t) {
        case OpType::Income: return "Доход";
        case OpType::Expense: return "Расход";
        case OpType::Transfer: return "Перевод";
    }
    return "";
}

const char* groupName(Group g) {
    switch (g) {
        case Group::Needs: return "Необходимое";
        case Group::Wants: return "Желания";
        case Group::Savings: return "Сбережения";
        case Group::Income: return "Доход";
    }
    return "";
}

const char* debtDirectionName(DebtDirection d) {
    return d == DebtDirection::OwedToMe ? "Мне должны" : "Я должен";
}

std::string Ledger::validate(const Operation& op) const {
    if (op.amount <= 0) return "Сумма должна быть больше нуля.";
    if (op.amount > kMaxAmount) return "Сумма слишком большая.";
    if (op.date > Date::today()) return "Дата операции не может быть позже сегодняшней.";
    if (!hasWallet(op.wallet)) return "Кошелёк «" + op.wallet + "» не найден.";
    if (utf8Length(op.comment) > 100) return "Комментарий длиннее 100 символов.";
    if (op.type == OpType::Transfer) {
        if (!hasWallet(op.toWallet)) return "Кошелёк «" + op.toWallet + "» не найден.";
        if (op.toWallet == op.wallet) return "Нельзя перевести деньги в тот же кошелёк.";
    } else if (!category(op.category, op.type)) {
        return "Категория «" + op.category + "» не найдена.";
    }
    return {};
}

int Ledger::add(Operation op) {
    op.id = nextId_++;
    ops_.push_back(op);
    return op.id;
}

void Ledger::loadOperation(const Operation& op) {
    ops_.push_back(op);
    nextId_ = std::max(nextId_, op.id + 1);
}

bool Ledger::update(const Operation& op) {
    for (auto& o : ops_)
        if (o.id == op.id) {
            o = op;
            return true;
        }
    return false;
}

bool Ledger::remove(int id) {
    auto it = std::find_if(ops_.begin(), ops_.end(), [id](const Operation& o) { return o.id == id; });
    if (it == ops_.end()) return false;
    ops_.erase(it);
    return true;
}

const Operation* Ledger::find(int id) const {
    for (const auto& o : ops_)
        if (o.id == id) return &o;
    return nullptr;
}

std::vector<Operation> Ledger::between(const Date& from, const Date& to) const {
    std::vector<Operation> res;
    for (const auto& o : ops_)
        if (from <= o.date && o.date <= to) res.push_back(o);
    std::stable_sort(res.begin(), res.end(), [](const Operation& a, const Operation& b) {
        if (!(a.date == b.date)) return a.date < b.date;
        return a.id < b.id;
    });
    return res;
}

std::vector<Operation> Ledger::search(const SearchFilter& f) const {
    std::vector<Operation> res;
    for (const auto& o : ops_) {
        if (f.hasFrom && o.date < f.from) continue;
        if (f.hasTo && o.date > f.to) continue;
        if (f.hasType && o.type != f.type) continue;
        if (!f.wallet.empty() && o.wallet != f.wallet &&
            !(o.type == OpType::Transfer && o.toWallet == f.wallet))
            continue;
        if (!f.category.empty() && o.category != f.category) continue;
        if (f.minAmount > 0 && o.amount < f.minAmount) continue;
        if (f.maxAmount > 0 && o.amount > f.maxAmount) continue;
        if (!f.query.empty()) {
            bool match = containsIgnoreCaseUtf8(o.category, f.query) ||
                         containsIgnoreCaseUtf8(o.wallet, f.query) ||
                         containsIgnoreCaseUtf8(o.toWallet, f.query) ||
                         containsIgnoreCaseUtf8(o.comment, f.query) ||
                         containsIgnoreCaseUtf8(typeName(o.type), f.query);
            if (!match) continue;
        }
        res.push_back(o);
    }
    std::stable_sort(res.begin(), res.end(), [](const Operation& a, const Operation& b) {
        if (!(a.date == b.date)) return a.date < b.date;
        return a.id < b.id;
    });
    return res;
}

Money Ledger::walletBalance(const std::string& wallet) const {
    Money sum = 0;
    for (const auto& w : wallets_)
        if (w.name == wallet) sum += w.initial;
    for (const auto& o : ops_) {
        if (o.type == OpType::Income && o.wallet == wallet) sum += o.amount;
        if (o.type == OpType::Expense && o.wallet == wallet) sum -= o.amount;
        if (o.type == OpType::Transfer) {
            if (o.wallet == wallet) sum -= o.amount;
            if (o.toWallet == wallet) sum += o.amount;
        }
    }
    return sum;
}

Money Ledger::totalBalance() const {
    Money sum = 0;
    for (const auto& w : wallets_) sum += walletBalance(w.name);
    return sum;
}

Money Ledger::spent(const std::string& category, const Date& from, const Date& to) const {
    Money sum = 0;
    for (const auto& o : ops_)
        if (o.type == OpType::Expense && o.category == category && from <= o.date && o.date <= to) sum += o.amount;
    return sum;
}

std::vector<Category> Ledger::categories(OpType type) const {
    std::vector<Category> res;
    for (const auto& c : cats_)
        if (c.type == type) res.push_back(c);
    return res;
}

const Category* Ledger::category(const std::string& name, OpType type) const {
    for (const auto& c : cats_)
        if (c.name == name && c.type == type) return &c;
    return nullptr;
}

bool Ledger::addCategory(const Category& c) {
    if (c.name.empty() || category(c.name, c.type)) return false;
    cats_.push_back(c);
    return true;
}

bool Ledger::removeCategory(const std::string& name, OpType type, std::string& error) {
    if (!category(name, type)) {
        error = "Категория не найдена.";
        return false;
    }
    if (categories(type).size() <= 1) {
        error = "Нельзя удалить последнюю категорию этого типа.";
        return false;
    }
    for (const auto& o : ops_) {
        if (o.type == type && o.category == name) {
            error = "Категория используется в операциях журнала.";
            return false;
        }
    }
    for (const auto& r : recurring_) {
        if (r.type == type && r.category == name) {
            error = "Категория используется в регулярных платежах.";
            return false;
        }
    }
    if (type == OpType::Expense && budgetFor(name) > 0) {
        error = "Для категории установлен бюджетный лимит — сначала снимите его.";
        return false;
    }
    cats_.erase(std::remove_if(cats_.begin(), cats_.end(),
                               [&](const Category& c) { return c.name == name && c.type == type; }),
                cats_.end());
    return true;
}

bool Ledger::hasWallet(const std::string& name) const {
    return std::any_of(wallets_.begin(), wallets_.end(), [&](const Wallet& w) { return w.name == name; });
}

bool Ledger::addWallet(const Wallet& w) {
    if (w.name.empty() || hasWallet(w.name)) return false;
    wallets_.push_back(w);
    return true;
}

bool Ledger::setWalletInitial(const std::string& name, Money initial) {
    for (auto& w : wallets_)
        if (w.name == name) {
            w.initial = initial;
            return true;
        }
    return false;
}

bool Ledger::renameWallet(const std::string& oldName, const std::string& newName, std::string& error) {
    if (newName.empty()) {
        error = "Новое название не может быть пустым.";
        return false;
    }
    if (!hasWallet(oldName)) {
        error = "Кошелёк «" + oldName + "» не найден.";
        return false;
    }
    if (oldName == newName) return true;
    if (hasWallet(newName)) {
        error = "Кошелёк «" + newName + "» уже существует.";
        return false;
    }
    for (auto& w : wallets_)
        if (w.name == oldName) w.name = newName;
    for (auto& o : ops_) {
        if (o.wallet == oldName) o.wallet = newName;
        if (o.toWallet == oldName) o.toWallet = newName;
    }
    for (auto& r : recurring_)
        if (r.wallet == oldName) r.wallet = newName;
    return true;
}

bool Ledger::removeWallet(const std::string& name, std::string& error) {
    if (!hasWallet(name)) {
        error = "Кошелёк не найден.";
        return false;
    }
    if (wallets_.size() <= 1) {
        error = "Должен оставаться хотя бы один кошелёк.";
        return false;
    }
    for (const auto& o : ops_) {
        if (o.wallet == name || o.toWallet == name) {
            error = "Нельзя удалить кошелёк, по которому есть операции.";
            return false;
        }
    }
    for (const auto& r : recurring_) {
        if (r.wallet == name) {
            error = "Кошелёк используется в регулярных платежах.";
            return false;
        }
    }
    wallets_.erase(std::remove_if(wallets_.begin(), wallets_.end(),
                                  [&](const Wallet& w) { return w.name == name; }),
                   wallets_.end());
    return true;
}

void Ledger::setBudget(const std::string& category, Money limit) {
    budgets_.erase(std::remove_if(budgets_.begin(), budgets_.end(),
                                  [&](const Budget& b) { return b.category == category; }),
                   budgets_.end());
    if (limit > 0) budgets_.push_back(Budget{category, limit});
}

Money Ledger::budgetFor(const std::string& category) const {
    for (const auto& b : budgets_)
        if (b.category == category) return b.limit;
    return 0;
}

// ───────────────────────── Финансовые цели ─────────────────────────

const SavingsGoal* Ledger::findGoal(int id) const {
    for (const auto& g : goals_)
        if (g.id == id) return &g;
    return nullptr;
}

int Ledger::addGoal(SavingsGoal goal, std::string& error) {
    goal.name = trim(goal.name);
    if (goal.name.empty()) {
        error = "Название цели не может быть пустым.";
        return -1;
    }
    if (goal.targetAmount <= 0 || goal.targetAmount > kMaxAmount) {
        error = "Целевая сумма должна быть больше нуля.";
        return -1;
    }
    if (goal.savedAmount < 0) {
        error = "Накопленная сумма не может быть отрицательной.";
        return -1;
    }
    goal.id = nextGoalId_++;
    goal.completed = goal.savedAmount >= goal.targetAmount;
    goals_.push_back(goal);
    return goal.id;
}

bool Ledger::removeGoal(int id) {
    auto it = std::find_if(goals_.begin(), goals_.end(), [id](const SavingsGoal& g) { return g.id == id; });
    if (it == goals_.end()) return false;
    goals_.erase(it);
    return true;
}

int Ledger::contributeGoal(int goalId, Money amount, const std::string& wallet, const Date& date, std::string& error) {
    if (amount <= 0 || amount > kMaxAmount) {
        error = "Сумма взноса должна быть положительной.";
        return -1;
    }
    if (!hasWallet(wallet)) {
        error = "Кошелёк «" + wallet + "» не найден.";
        return -1;
    }
    if (date > Date::today()) {
        error = "Дата взноса не может быть позже сегодняшней.";
        return -1;
    }
    SavingsGoal* target = nullptr;
    for (auto& g : goals_)
        if (g.id == goalId) target = &g;
    if (!target) {
        error = "Цель №" + std::to_string(goalId) + " не найдена.";
        return -1;
    }
    std::string savCat = "Сбережения";
    if (!category(savCat, OpType::Expense)) {
        auto expCats = categories(OpType::Expense);
        if (!expCats.empty()) savCat = expCats.front().name;
        else addCategory(Category{savCat, OpType::Expense, Group::Savings});
    }
    Operation op;
    op.date = date;
    op.type = OpType::Expense;
    op.amount = amount;
    op.category = savCat;
    op.wallet = wallet;
    op.comment = utf8Truncate("Цель: " + target->name, 100);
    error = validate(op);
    if (!error.empty()) return -1;

    int opId = add(op);
    target->savedAmount += amount;
    if (target->savedAmount >= target->targetAmount) target->completed = true;
    return opId;
}

void Ledger::loadGoal(const SavingsGoal& g) {
    goals_.push_back(g);
    nextGoalId_ = std::max(nextGoalId_, g.id + 1);
}

// ───────────────────────── Регулярные платежи ─────────────────────────

const RecurringPayment* Ledger::findRecurring(int id) const {
    for (const auto& r : recurring_)
        if (r.id == id) return &r;
    return nullptr;
}

int Ledger::addRecurring(RecurringPayment rec, std::string& error) {
    rec.name = trim(rec.name);
    if (rec.name.empty()) {
        error = "Название регулярного платежа не может быть пустым.";
        return -1;
    }
    if (rec.type == OpType::Transfer) {
        error = "Регулярный платёж должен быть доходом или расходом.";
        return -1;
    }
    if (rec.amount <= 0 || rec.amount > kMaxAmount) {
        error = "Сумма платежа должна быть больше нуля.";
        return -1;
    }
    if (!category(rec.category, rec.type)) {
        error = "Категория «" + rec.category + "» не найдена.";
        return -1;
    }
    if (!hasWallet(rec.wallet)) {
        error = "Кошелёк «" + rec.wallet + "» не найден.";
        return -1;
    }
    if (rec.dayOfMonth < 1) rec.dayOfMonth = 1;
    if (rec.dayOfMonth > 31) rec.dayOfMonth = 31;
    rec.id = nextRecId_++;
    recurring_.push_back(rec);
    return rec.id;
}

bool Ledger::toggleRecurring(int id) {
    for (auto& r : recurring_)
        if (r.id == id) {
            r.active = !r.active;
            return true;
        }
    return false;
}

bool Ledger::removeRecurring(int id) {
    auto it = std::find_if(recurring_.begin(), recurring_.end(), [id](const RecurringPayment& r) { return r.id == id; });
    if (it == recurring_.end()) return false;
    recurring_.erase(it);
    return true;
}

std::vector<RecurringPayment> Ledger::dueRecurring(const Date& today) const {
    std::vector<RecurringPayment> res;
    std::string ym = today.yearMonth();
    int dim = Date::daysInMonth(today.y, today.m);
    for (const auto& r : recurring_) {
        if (!r.active) continue;
        if (r.lastAppliedYm == ym) continue;
        int targetDay = std::min(r.dayOfMonth, dim);
        if (targetDay <= today.d) res.push_back(r);
    }
    return res;
}

int Ledger::applyRecurring(int recId, const Date& today, std::string& error) {
    RecurringPayment* rec = nullptr;
    for (auto& r : recurring_)
        if (r.id == recId) rec = &r;
    if (!rec) {
        error = "Регулярный платёж №" + std::to_string(recId) + " не найден.";
        return -1;
    }
    int dim = Date::daysInMonth(today.y, today.m);
    int d = std::min(std::min(rec->dayOfMonth, dim), today.d);
    Operation op;
    op.date = Date{today.y, today.m, std::max(d, 1)};
    op.type = rec->type;
    op.amount = rec->amount;
    op.category = rec->category;
    op.wallet = rec->wallet;
    op.comment = utf8Truncate("Авто: " + rec->name, 100);
    error = validate(op);
    if (!error.empty()) return -1;

    int opId = add(op);
    rec->lastAppliedYm = today.yearMonth();
    return opId;
}

std::vector<int> Ledger::applyAllDueRecurring(const Date& today) {
    std::vector<int> created;
    auto due = dueRecurring(today);
    for (const auto& d : due) {
        std::string err;
        int opId = applyRecurring(d.id, today, err);
        if (opId > 0) created.push_back(opId);
    }
    return created;
}

void Ledger::loadRecurring(const RecurringPayment& r) {
    recurring_.push_back(r);
    nextRecId_ = std::max(nextRecId_, r.id + 1);
}

// ───────────────────────── Долги и займы ─────────────────────────

const Debt* Ledger::findDebt(int id) const {
    for (const auto& d : debts_)
        if (d.id == id) return &d;
    return nullptr;
}

int Ledger::addDebt(Debt debt, std::string& error) {
    debt.person = trim(debt.person);
    if (debt.person.empty()) {
        error = "Укажите контрагента (кому или от кого долг).";
        return -1;
    }
    if (debt.amount <= 0 || debt.amount > kMaxAmount) {
        error = "Сумма долга должна быть положительной.";
        return -1;
    }
    if (debt.repaid < 0 || debt.repaid > debt.amount) {
        error = "Погашенная сумма некорректна.";
        return -1;
    }
    debt.id = nextDebtId_++;
    debts_.push_back(debt);
    return debt.id;
}

bool Ledger::removeDebt(int id) {
    auto it = std::find_if(debts_.begin(), debts_.end(), [id](const Debt& d) { return d.id == id; });
    if (it == debts_.end()) return false;
    debts_.erase(it);
    return true;
}

bool Ledger::repayDebt(int debtId, Money amount, bool createOperation, const std::string& wallet, const Date& date,
                       std::string& error) {
    Debt* target = nullptr;
    for (auto& d : debts_)
        if (d.id == debtId) target = &d;
    if (!target) {
        error = "Запись о долге №" + std::to_string(debtId) + " не найдена.";
        return false;
    }
    if (amount <= 0 || amount > target->remaining()) {
        error = "Сумма погашения должна быть от 0,01 до остатка долга (" + formatMoney(target->remaining()) + " BYN).";
        return false;
    }
    if (createOperation) {
        OpType opType = (target->direction == DebtDirection::OwedToMe) ? OpType::Income : OpType::Expense;
        std::string cat = (opType == OpType::Income) ? "Прочие доходы" : "Прочее";
        if (!category(cat, opType)) {
            auto cats = categories(opType);
            if (!cats.empty()) cat = cats.front().name;
            else addCategory(Category{cat, opType, opType == OpType::Income ? Group::Income : Group::Wants});
        }
        Operation op;
        op.date = date;
        op.type = opType;
        op.amount = amount;
        op.category = cat;
        op.wallet = wallet;
        op.comment = utf8Truncate(
            (target->direction == DebtDirection::OwedToMe ? "Возврат долга: " : "Погашение долга: ") + target->person,
            100);
        error = validate(op);
        if (!error.empty()) return false;
        add(op);
    }
    target->repaid += amount;
    return true;
}

Money Ledger::totalOwedToMe() const {
    Money sum = 0;
    for (const auto& d : debts_)
        if (d.direction == DebtDirection::OwedToMe) sum += d.remaining();
    return sum;
}

Money Ledger::totalIOwe() const {
    Money sum = 0;
    for (const auto& d : debts_)
        if (d.direction == DebtDirection::IOwe) sum += d.remaining();
    return sum;
}

void Ledger::loadDebt(const Debt& d) {
    debts_.push_back(d);
    nextDebtId_ = std::max(nextDebtId_, d.id + 1);
}

// ───────────────────────── Сброс и начальные значения ─────────────────────────

void Ledger::clear() {
    ops_.clear();
    cats_.clear();
    wallets_.clear();
    budgets_.clear();
    goals_.clear();
    recurring_.clear();
    debts_.clear();
    nextId_ = 1;
    nextGoalId_ = 1;
    nextRecId_ = 1;
    nextDebtId_ = 1;
}

void Ledger::setDefaults() {
    const struct {
        const char* name;
        Group group;
    } expense[] = {
        {"Продукты", Group::Needs},         {"Жильё и ЖКУ", Group::Needs}, {"Транспорт", Group::Needs},
        {"Связь и интернет", Group::Needs}, {"Здоровье", Group::Needs},    {"Кафе и рестораны", Group::Wants},
        {"Развлечения", Group::Wants},      {"Одежда", Group::Wants},      {"Подарки", Group::Wants},
        {"Прочее", Group::Wants},           {"Сбережения", Group::Savings},
    };
    for (const auto& e : expense) cats_.push_back(Category{e.name, OpType::Expense, e.group});
    for (const char* name : {"Зарплата", "Стипендия", "Подработка", "Подарки", "Прочие доходы"})
        cats_.push_back(Category{name, OpType::Income, Group::Income});
    wallets_.push_back(Wallet{"Наличные", 0});
    wallets_.push_back(Wallet{"Карта", 0});
}
