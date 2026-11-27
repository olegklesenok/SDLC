// Analytics.h — статистика, контроль бюджета, индекс финансового здоровья, прогнозы и советы
#pragma once
#include <string>
#include <utility>
#include <vector>

#include "Ledger.h"

struct CategoryTotal {
    std::string category;
    Money amount = 0;
    double share = 0;  // доля от всех расходов (доходов) периода, %
};

struct PeriodStats {
    Date from, to;
    Money income = 0;
    Money expense = 0;
    Money balance() const { return income - expense; }
    std::vector<CategoryTotal> expenseByCategory;  // по убыванию суммы
    std::vector<CategoryTotal> incomeByCategory;
    Money needs = 0, wants = 0, savings = 0;  // расходы по группам 50/30/20
    int operations = 0;
};

enum class BudgetState { Normal, Near, Over };

struct BudgetStatus {
    std::string category;
    Money limit = 0;
    Money spent = 0;
    double usedPercent = 0;
    BudgetState state = BudgetState::Normal;
};

enum class AdviceLevel { Good, Info, Warning };

struct Advice {
    AdviceLevel level;
    std::string title;
    std::string text;
};

// Индекс финансового здоровья (0..100 баллов)
struct FinancialHealth {
    int score = 0;
    int savingsScore = 0;      // 0..30
    int rule503020Score = 0;   // 0..25
    int budgetScore = 0;       // 0..25
    int cushionScore = 0;      // 0..20
    double safetyCushionMonths = 0.0;
    std::string grade;
};

// Распределение расходов по дням недели
struct WeekdayStat {
    int dow = 0;  // 0 = Пн .. 6 = Вс
    std::string name;
    Money total = 0;
    int count = 0;
    Money average() const { return count > 0 ? total / count : 0; }
};

// Прогноз достижения финансовой цели
struct GoalProjection {
    int goalId = 0;
    std::string name;
    Money remaining = 0;
    double percentDone = 0.0;
    int daysLeft = 0;
    Money dailyNeeded = 0;
    Money monthlyNeeded = 0;
    bool overdue = false;
};

// Результат разбора строки быстрого ввода ("-18,50 продукты хлеб")
struct QuickParseResult {
    bool ok = false;
    OpType type = OpType::Expense;
    Money amount = 0;
    std::string category;
    std::string comment;
    std::string error;
};

constexpr double kBudgetNearPercent = 80.0;  // порог предупреждения о приближении к лимиту

PeriodStats computeStats(const Ledger& ledger, const Date& from, const Date& to);
PeriodStats monthStats(const Ledger& ledger, const Date& anyDayOfMonth);
std::vector<BudgetStatus> budgetStatuses(const Ledger& ledger, const Date& anyDayOfMonth);
BudgetStatus budgetStatus(const Ledger& ledger, const std::string& category, const Date& anyDayOfMonth);
// Помесячная динамика за months месяцев, заканчивая месяцем lastMonth
std::vector<PeriodStats> monthlyDynamics(const Ledger& ledger, const Date& lastMonth, int months);
// Советы для месяца (today — текущая дата, нужна для прогноза)
std::vector<Advice> makeAdvice(const Ledger& ledger, const Date& month, const Date& today);

FinancialHealth computeFinancialHealth(const Ledger& ledger, const Date& month);
std::vector<WeekdayStat> weekdayBreakdown(const Ledger& ledger, const Date& from, const Date& to);
GoalProjection projectGoal(const SavingsGoal& goal, const Date& today);
QuickParseResult parseQuickInput(const Ledger& ledger, const std::string& input);
