// Ledger.h — предметная модель: операции, категории, кошельки, бюджеты, цели, регулярные платежи, долги
#pragma once
#include <string>
#include <vector>

#include "Date.h"
#include "Money.h"

enum class OpType { Income, Expense, Transfer };

// Группа категории для правила 50/30/20
enum class Group { Needs, Wants, Savings, Income };

struct Operation {
    int id = 0;
    Date date;
    OpType type = OpType::Expense;
    Money amount = 0;
    std::string category;  // для перевода — пусто
    std::string wallet;    // кошелёк списания/зачисления
    std::string toWallet;  // только для перевода
    std::string comment;
};

struct Category {
    std::string name;
    OpType type = OpType::Expense;
    Group group = Group::Needs;
};

struct Wallet {
    std::string name;
    Money initial = 0;  // начальный остаток
};

struct Budget {
    std::string category;
    Money limit = 0;  // месячный лимит расходов
};

// Финансовая цель накоплений (REQ-FUNC-019)
struct SavingsGoal {
    int id = 0;
    std::string name;
    Money targetAmount = 0;
    Money savedAmount = 0;
    Date deadline;
    bool completed = false;
};

// Регулярный платёж / подписка (REQ-FUNC-020)
struct RecurringPayment {
    int id = 0;
    std::string name;
    OpType type = OpType::Expense;  // Income или Expense
    Money amount = 0;
    std::string category;
    std::string wallet;
    int dayOfMonth = 1;         // 1..31
    std::string lastAppliedYm;  // "ГГГГ-ММ" или ""
    bool active = true;
};

// Направление долга (REQ-FUNC-021)
enum class DebtDirection { OwedToMe, IOwe };

struct Debt {
    int id = 0;
    std::string person;
    DebtDirection direction = DebtDirection::OwedToMe;
    Money amount = 0;
    Money repaid = 0;
    Date created;
    Date dueDate;
    std::string comment;

    Money remaining() const { return amount > repaid ? amount - repaid : 0; }
    bool settled() const { return remaining() == 0; }
};

// Параметры расширенного поиска операций (REQ-FUNC-022)
struct SearchFilter {
    std::string query;
    bool hasFrom = false;
    Date from;
    bool hasTo = false;
    Date to;
    bool hasType = false;
    OpType type = OpType::Expense;
    std::string wallet;
    std::string category;
    Money minAmount = 0;
    Money maxAmount = 0;
};

const char* typeName(OpType t);
const char* groupName(Group g);
const char* debtDirectionName(DebtDirection d);

class Ledger {
public:
    // Возвращает пустую строку, если операция корректна, иначе текст ошибки
    std::string validate(const Operation& op) const;
    int add(Operation op);             // присваивает id, возвращает его
    bool update(const Operation& op);  // по id
    bool remove(int id);
    const Operation* find(int id) const;

    std::vector<Operation> between(const Date& from, const Date& to) const;  // отсортировано по дате
    std::vector<Operation> search(const SearchFilter& filter) const;
    const std::vector<Operation>& operations() const { return ops_; }

    Money walletBalance(const std::string& wallet) const;
    Money totalBalance() const;
    Money spent(const std::string& category, const Date& from, const Date& to) const;

    // Категории
    std::vector<Category> categories(OpType type) const;
    const Category* category(const std::string& name, OpType type) const;
    bool addCategory(const Category& c);
    bool removeCategory(const std::string& name, OpType type, std::string& error);
    const std::vector<Category>& allCategories() const { return cats_; }

    // Кошельки
    const std::vector<Wallet>& wallets() const { return wallets_; }
    bool hasWallet(const std::string& name) const;
    bool addWallet(const Wallet& w);
    bool setWalletInitial(const std::string& name, Money initial);
    bool renameWallet(const std::string& oldName, const std::string& newName, std::string& error);
    bool removeWallet(const std::string& name, std::string& error);

    // Бюджеты
    const std::vector<Budget>& budgets() const { return budgets_; }
    void setBudget(const std::string& category, Money limit);  // limit == 0 — удалить
    Money budgetFor(const std::string& category) const;

    // Финансовые цели накоплений (REQ-FUNC-019)
    const std::vector<SavingsGoal>& goals() const { return goals_; }
    const SavingsGoal* findGoal(int id) const;
    int addGoal(SavingsGoal goal, std::string& error);
    bool removeGoal(int id);
    int contributeGoal(int goalId, Money amount, const std::string& wallet, const Date& date, std::string& error);

    // Регулярные платежи (REQ-FUNC-020)
    const std::vector<RecurringPayment>& recurring() const { return recurring_; }
    const RecurringPayment* findRecurring(int id) const;
    int addRecurring(RecurringPayment rec, std::string& error);
    bool toggleRecurring(int id);
    bool removeRecurring(int id);
    std::vector<RecurringPayment> dueRecurring(const Date& today) const;
    int applyRecurring(int recId, const Date& today, std::string& error);
    std::vector<int> applyAllDueRecurring(const Date& today);

    // Долги и займы (REQ-FUNC-021)
    const std::vector<Debt>& debts() const { return debts_; }
    const Debt* findDebt(int id) const;
    int addDebt(Debt debt, std::string& error);
    bool removeDebt(int id);
    bool repayDebt(int debtId, Money amount, bool createOperation, const std::string& wallet, const Date& date,
                   std::string& error);
    Money totalOwedToMe() const;
    Money totalIOwe() const;

    void setDefaults();  // стандартные категории и кошельки для нового пользователя
    void clear();

    // Доступ для хранилища
    void loadOperation(const Operation& op);
    void loadCategory(const Category& c) { cats_.push_back(c); }
    void loadWallet(const Wallet& w) { wallets_.push_back(w); }
    void loadBudget(const Budget& b) { budgets_.push_back(b); }
    void loadGoal(const SavingsGoal& g);
    void loadRecurring(const RecurringPayment& r);
    void loadDebt(const Debt& d);

private:
    std::vector<Operation> ops_;
    std::vector<Category> cats_;
    std::vector<Wallet> wallets_;
    std::vector<Budget> budgets_;
    std::vector<SavingsGoal> goals_;
    std::vector<RecurringPayment> recurring_;
    std::vector<Debt> debts_;
    int nextId_ = 1;
    int nextGoalId_ = 1;
    int nextRecId_ = 1;
    int nextDebtId_ = 1;
};
