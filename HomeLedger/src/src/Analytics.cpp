// Analytics.cpp — расчёт статистики, финансового здоровья, прогнозов и советов
#include "Analytics.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <sstream>

#include "Text.h"

namespace {

std::vector<CategoryTotal> toSortedTotals(const std::map<std::string, Money>& m, Money total) {
    std::vector<CategoryTotal> v;
    for (const auto& [name, sum] : m) v.push_back(CategoryTotal{name, sum, percent(sum, total)});
    std::sort(v.begin(), v.end(), [](const CategoryTotal& a, const CategoryTotal& b) {
        return a.amount != b.amount ? a.amount > b.amount : a.category < b.category;
    });
    return v;
}

std::string pct(double v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.0f %%", v);
    return buf;
}

// неразрывные пробелы, чтобы сумма не разрывалась при переносе строки
std::string money(Money m) {
    std::string s = formatMoney(m), r;
    for (char c : s) r += (c == ' ') ? std::string("\xC2\xA0") : std::string(1, c);
    return r + "\xC2\xA0" "BYN";
}

}  // namespace

PeriodStats computeStats(const Ledger& ledger, const Date& from, const Date& to) {
    PeriodStats s;
    s.from = from;
    s.to = to;
    std::map<std::string, Money> exp, inc;
    for (const auto& o : ledger.between(from, to)) {
        if (o.type == OpType::Transfer) continue;  // переводы не меняют общий баланс
        ++s.operations;
        if (o.type == OpType::Income) {
            s.income += o.amount;
            inc[o.category] += o.amount;
        } else {
            s.expense += o.amount;
            exp[o.category] += o.amount;
            const Category* c = ledger.category(o.category, OpType::Expense);
            Group g = c ? c->group : Group::Wants;
            if (g == Group::Needs) s.needs += o.amount;
            else if (g == Group::Savings) s.savings += o.amount;
            else s.wants += o.amount;
        }
    }
    s.expenseByCategory = toSortedTotals(exp, s.expense);
    s.incomeByCategory = toSortedTotals(inc, s.income);
    return s;
}

PeriodStats monthStats(const Ledger& ledger, const Date& d) {
    return computeStats(ledger, d.firstOfMonth(), d.lastOfMonth());
}

BudgetStatus budgetStatus(const Ledger& ledger, const std::string& category, const Date& d) {
    BudgetStatus b;
    b.category = category;
    b.limit = ledger.budgetFor(category);
    b.spent = ledger.spent(category, d.firstOfMonth(), d.lastOfMonth());
    b.usedPercent = percent(b.spent, b.limit);
    if (b.limit > 0) {
        if (b.spent > b.limit) b.state = BudgetState::Over;
        else if (b.usedPercent >= kBudgetNearPercent) b.state = BudgetState::Near;
    }
    return b;
}

std::vector<BudgetStatus> budgetStatuses(const Ledger& ledger, const Date& d) {
    std::vector<BudgetStatus> res;
    for (const auto& b : ledger.budgets()) res.push_back(budgetStatus(ledger, b.category, d));
    std::sort(res.begin(), res.end(),
              [](const BudgetStatus& a, const BudgetStatus& b) { return a.usedPercent > b.usedPercent; });
    return res;
}

std::vector<PeriodStats> monthlyDynamics(const Ledger& ledger, const Date& lastMonth, int months) {
    std::vector<PeriodStats> res;
    for (int i = months - 1; i >= 0; --i) res.push_back(monthStats(ledger, lastMonth.firstOfMonth().addMonths(-i)));
    return res;
}

FinancialHealth computeFinancialHealth(const Ledger& ledger, const Date& month) {
    FinancialHealth fh;
    PeriodStats s = monthStats(ledger, month);

    // 1. Норма сбережений (0..30)
    if (s.income > 0) {
        Money savedTotal = s.savings + std::max<Money>(0, s.balance());
        double savePct = percent(savedTotal, s.income);
        double ratio = std::clamp(savePct / 20.0, 0.0, 1.0);
        fh.savingsScore = static_cast<int>(std::lround(30.0 * ratio));
    }

    // 2. Правило 50/30/20 (0..25)
    if (s.income > 0) {
        double needsPct = percent(s.needs, s.income);
        double wantsPct = percent(s.wants, s.income);
        int needsPts = 13;
        if (needsPct > 50.0) {
            double penalty = std::clamp((needsPct - 50.0) / 25.0, 0.0, 1.0);
            needsPts = static_cast<int>(std::lround(13.0 * (1.0 - penalty)));
        }
        int wantsPts = 12;
        if (wantsPct > 30.0) {
            double penalty = std::clamp((wantsPct - 30.0) / 25.0, 0.0, 1.0);
            wantsPts = static_cast<int>(std::lround(12.0 * (1.0 - penalty)));
        }
        fh.rule503020Score = needsPts + wantsPts;
    }

    // 3. Соблюдение лимитов (0..25)
    auto budgets = budgetStatuses(ledger, month);
    if (budgets.empty()) {
        fh.budgetScore = 15;
    } else {
        int pts = 25;
        for (const auto& b : budgets) {
            if (b.state == BudgetState::Over) pts -= 10;
            else if (b.state == BudgetState::Near) pts -= 4;
        }
        fh.budgetScore = std::max(0, pts);
    }

    // 4. Подушка безопасности в месяцах средних расходов (0..20)
    auto dyn = monthlyDynamics(ledger, month, 3);
    Money expSum = 0;
    int expMonths = 0;
    for (const auto& d : dyn) {
        if (d.expense > 0) {
            expSum += d.expense;
            ++expMonths;
        }
    }
    Money avgExpense = expMonths > 0 ? (expSum / expMonths) : 0;
    Money totalBal = ledger.totalBalance();
    if (avgExpense > 0 && totalBal > 0) {
        fh.safetyCushionMonths = static_cast<double>(totalBal) / static_cast<double>(avgExpense);
        double ratio = std::clamp(fh.safetyCushionMonths / 6.0, 0.0, 1.0);
        fh.cushionScore = static_cast<int>(std::lround(20.0 * ratio));
    } else if (totalBal > 0) {
        fh.safetyCushionMonths = 6.0;
        fh.cushionScore = 20;
    }

    fh.score = std::clamp(fh.savingsScore + fh.rule503020Score + fh.budgetScore + fh.cushionScore, 0, 100);
    if (fh.score >= 85) fh.grade = "Отлично";
    else if (fh.score >= 65) fh.grade = "Хорошо";
    else if (fh.score >= 40) fh.grade = "Удовлетворительно";
    else fh.grade = "Требует внимания";
    return fh;
}

std::vector<WeekdayStat> weekdayBreakdown(const Ledger& ledger, const Date& from, const Date& to) {
    std::vector<WeekdayStat> stats(7);
    for (int i = 0; i < 7; ++i) {
        stats[static_cast<size_t>(i)].dow = i;
        stats[static_cast<size_t>(i)].name = Date::weekdayShort(i);
    }
    for (const auto& o : ledger.between(from, to)) {
        if (o.type != OpType::Expense) continue;
        int dow = o.date.dayOfWeek();
        auto& st = stats[static_cast<size_t>(dow)];
        st.total += o.amount;
        ++st.count;
    }
    return stats;
}

GoalProjection projectGoal(const SavingsGoal& goal, const Date& today) {
    GoalProjection gp;
    gp.goalId = goal.id;
    gp.name = goal.name;
    gp.remaining = goal.targetAmount > goal.savedAmount ? goal.targetAmount - goal.savedAmount : 0;
    gp.percentDone = std::clamp(percent(goal.savedAmount, goal.targetAmount), 0.0, 100.0);
    if (gp.remaining == 0 || goal.completed) {
        gp.daysLeft = 0;
        return gp;
    }
    long diff = goal.deadline.toDays() - today.toDays();
    gp.overdue = diff <= 0;
    gp.daysLeft = diff > 0 ? static_cast<int>(diff) : 0;
    if (gp.daysLeft > 0) {
        gp.dailyNeeded = (gp.remaining + gp.daysLeft - 1) / gp.daysLeft;
        double monthsLeft = std::max(1.0, static_cast<double>(gp.daysLeft) / 30.0);
        gp.monthlyNeeded = static_cast<Money>(std::ceil(static_cast<double>(gp.remaining) / monthsLeft));
    } else {
        gp.dailyNeeded = gp.remaining;
        gp.monthlyNeeded = gp.remaining;
    }
    return gp;
}

QuickParseResult parseQuickInput(const Ledger& ledger, const std::string& input) {
    QuickParseResult res;
    std::string s = trim(input);
    if (s.empty() || (s[0] != '-' && s[0] != '+')) {
        res.error = "Быстрый ввод должен начинаться с «-» (расход) или «+» (доход), например: -18,50 продукты хлеб";
        return res;
    }
    res.type = (s[0] == '+') ? OpType::Income : OpType::Expense;
    std::string rest = trim(s.substr(1));
    std::istringstream iss(rest);
    std::string amountToken;
    if (!(iss >> amountToken)) {
        res.error = "Укажите сумму после знака, например: -18,50 продукты хлеб";
        return res;
    }
    if (!parseMoney(amountToken, res.amount) || res.amount <= 0) {
        res.error = "Некорректная сумма «" + amountToken + "». Пример: -18,50 продукты хлеб";
        return res;
    }

    std::string catToken;
    std::string tail;
    if (iss >> catToken) {
        std::getline(iss, tail);
        tail = trim(tail);
    }

    auto cats = ledger.categories(res.type);
    if (cats.empty()) {
        res.error = "Нет доступных категорий.";
        return res;
    }

    std::string matched;
    if (!catToken.empty()) {
        for (const auto& c : cats) {
            if (toLowerUtf8(c.name) == toLowerUtf8(catToken)) {
                matched = c.name;
                break;
            }
        }
        if (matched.empty()) {
            for (const auto& c : cats) {
                if (startsWithIgnoreCaseUtf8(c.name, catToken)) {
                    matched = c.name;
                    break;
                }
            }
        }
        if (matched.empty()) {
            for (const auto& c : cats) {
                if (containsIgnoreCaseUtf8(c.name, catToken)) {
                    matched = c.name;
                    break;
                }
            }
        }
    }

    if (!matched.empty()) {
        res.category = matched;
        res.comment = utf8Truncate(tail, 100);
    } else {
        std::string fallback = (res.type == OpType::Expense) ? "Прочее" : "Прочие доходы";
        if (ledger.category(fallback, res.type)) res.category = fallback;
        else res.category = cats.front().name;
        std::string fullComment = catToken;
        if (!tail.empty()) fullComment += (fullComment.empty() ? "" : " ") + tail;
        res.comment = utf8Truncate(fullComment, 100);
    }

    res.ok = true;
    return res;
}

std::vector<Advice> makeAdvice(const Ledger& ledger, const Date& month, const Date& today) {
    std::vector<Advice> res;
    PeriodStats s = monthStats(ledger, month);
    PeriodStats prev = monthStats(ledger, month.firstOfMonth().addMonths(-1));

    // Ожидающие регулярные платежи проверяем всегда, даже если операций в месяце ещё мало
    auto due = ledger.dueRecurring(today);
    if (!due.empty()) {
        Money sumDueExp = 0;
        std::string names;
        for (size_t i = 0; i < due.size(); ++i) {
            if (i) names += ", ";
            names += due[i].name + " (" + money(due[i].amount) + ")";
            if (due[i].type == OpType::Expense) sumDueExp += due[i].amount;
        }
        res.push_back({AdviceLevel::Warning,
                       "Ожидают регулярные платежи (" + std::to_string(due.size()) + ")",
                       "Пора провести: " + names + ". Откройте пункт «9 Регулярные платежи», чтобы учесть их в балансе."});
        if (sumDueExp > ledger.totalBalance()) {
            res.push_back({AdviceLevel::Warning, "Недостаточно средств для обязательных платежей",
                           "На счетах " + money(ledger.totalBalance()) + ", а предстоящие списания составляют " +
                               money(sumDueExp) + "."});
        }
    }

    if (s.operations == 0) {
        res.push_back({AdviceLevel::Info, "Нет данных",
                       "За " + month.monthName() + " операций нет. Вносите доходы и расходы сразу после покупки — "
                       "так статистика будет точной."});
        return res;
    }
    if (s.income == 0) {
        res.push_back({AdviceLevel::Warning, "Нет доходов",
                       "В этом месяце не записано ни одного дохода. Без доходов нельзя оценить долю трат: "
                       "добавьте зарплату, стипендию или другие поступления."});
    }

    // 1. Правило 50/30/20: необходимое / желания / сбережения от дохода
    if (s.income > 0) {
        double needs = percent(s.needs, s.income), wants = percent(s.wants, s.income),
               savings = percent(s.savings + std::max<Money>(0, s.balance()), s.income);
        std::string fact = "Необходимое " + pct(needs) + " (норма ≤ 50 %), желания " + pct(wants) +
                           " (≤ 30 %), сбережения и остаток " + pct(savings) + " (≥ 20 %).";
        if (needs <= 50 && wants <= 30 && savings >= 20) {
            res.push_back({AdviceLevel::Good, "Правило 50/30/20 выполняется", fact});
        } else {
            std::string tip;
            if (needs > 50) tip += " Обязательные траты велики: проверьте тарифы связи, ЖКУ, транспорт.";
            if (wants > 30)
                tip += " На желания уходит больше 30 % — установите лимиты на кафе, развлечения и покупки.";
            if (savings < 20) tip += " Откладывайте 20 % дохода сразу в день его получения.";
            res.push_back({AdviceLevel::Warning, "Правило 50/30/20 нарушено", fact + tip});
        }
    }

    // 2. Прогноз до конца текущего месяца и дневной лимит
    if (month.sameMonth(today)) {
        int day = today.d, days = Date::daysInMonth(today.y, today.m), left = days - day;
        Money forecast = s.expense * days / std::max(day, 1);
        std::string text = "Средний расход " + money(s.expense / std::max(day, 1)) + " в день, прогноз расходов на месяц " +
                           money(forecast) + ".";
        if (s.income > 0 && forecast > s.income) {
            res.push_back({AdviceLevel::Warning, "Прогноз: расходы превысят доходы",
                           text + " Это больше доходов (" + money(s.income) + ") на " + money(forecast - s.income) + "."});
        } else {
            res.push_back({AdviceLevel::Info, "Прогноз на месяц", text});
        }
        if (s.income > 0 && left > 0) {
            Money free = s.income - s.expense - s.income / 5;  // остаток после откладывания 20 %
            if (free > 0)
                res.push_back({AdviceLevel::Info, "Дневной лимит",
                               "Чтобы отложить 20 % дохода, тратьте не больше " + money(free / left) +
                                   " в день до конца месяца (осталось дней: " + std::to_string(left) + ")."});
            else
                res.push_back({AdviceLevel::Warning, "Дневной лимит исчерпан",
                               "Отложить 20 % дохода в этом месяце уже не получится. Сократите траты на желания до "
                               "конца месяца."});
        }
    }

    // 3. Крупнейшие категории расходов
    if (!s.expenseByCategory.empty()) {
        std::string text;
        for (size_t i = 0; i < s.expenseByCategory.size() && i < 3; ++i) {
            const auto& c = s.expenseByCategory[i];
            if (i) text += ", ";
            text += c.category + " — " + pct(c.share);
        }
        res.push_back({AdviceLevel::Info, "Больше всего денег уходит на", text + "."});
    }

    // 4. Рост расходов по сравнению с прошлым месяцем
    for (const auto& c : s.expenseByCategory) {
        Money before = 0;
        for (const auto& p : prev.expenseByCategory)
            if (p.category == c.category) before = p.amount;
        if (before > 0 && c.amount > before * 12 / 10 && c.amount - before >= 2000) {
            res.push_back({AdviceLevel::Warning, "Рост расходов: " + c.category,
                           "По сравнению с прошлым месяцем траты выросли на " +
                               pct(percent(c.amount - before, before)) + " (" + money(before) + " → " +
                               money(c.amount) + ")."});
        }
    }

    // 5. Бюджеты
    bool anyBudget = false;
    for (const auto& b : budgetStatuses(ledger, month)) {
        anyBudget = true;
        if (b.state == BudgetState::Over)
            res.push_back({AdviceLevel::Warning, "Лимит превышен: " + b.category,
                           "Потрачено " + money(b.spent) + " при лимите " + money(b.limit) + "."});
        else if (b.state == BudgetState::Near)
            res.push_back({AdviceLevel::Warning, "Лимит почти исчерпан: " + b.category,
                           "Использовано " + pct(b.usedPercent) + " лимита, осталось " +
                               money(b.limit - b.spent) + "."});
    }
    if (!anyBudget && !s.expenseByCategory.empty()) {
        const auto& top = s.expenseByCategory.front();
        res.push_back({AdviceLevel::Info, "Установите лимиты",
                       "Лимиты не заданы. Начните с самой затратной категории «" + top.category +
                           "»: например, " + money(top.amount * 9 / 10 / 100 * 100) + " в месяц (на 10 % меньше, чем сейчас)."});
    }

    // 6. Подушка безопасности и пиковый день трат
    FinancialHealth fh = computeFinancialHealth(ledger, month);
    char cushionBuf[32];
    std::snprintf(cushionBuf, sizeof cushionBuf, "%.1f", fh.safetyCushionMonths);
    for (char* p = cushionBuf; *p; ++p)
        if (*p == '.') *p = ',';
    res.push_back({fh.safetyCushionMonths >= 3.0 ? AdviceLevel::Good : AdviceLevel::Info,
                   "Индекс финансового здоровья: " + std::to_string(fh.score) + " / 100 (" + fh.grade + ")",
                   "Текущая подушка безопасности покрывает " + std::string(cushionBuf) +
                       " мес. средних расходов (рекомендуется 3–6 мес.)."});

    auto wd = weekdayBreakdown(ledger, month.firstOfMonth(), month.lastOfMonth());
    auto peakIt = std::max_element(wd.begin(), wd.end(),
                                   [](const WeekdayStat& a, const WeekdayStat& b) { return a.total < b.total; });
    if (peakIt != wd.end() && peakIt->total > 0) {
        res.push_back({AdviceLevel::Info, "Пиковый день расходов: " + std::string(Date::weekdayName(peakIt->dow)),
                       "В этот день недели потрачено " + money(peakIt->total) + " (" + std::to_string(peakIt->count) +
                           " оп., в среднем " + money(peakIt->average()) + ")."});
    }

    // 7. Финансовые цели и долги
    for (const auto& g : ledger.goals()) {
        if (g.completed) continue;
        GoalProjection gp = projectGoal(g, today);
        if (gp.overdue) {
            res.push_back({AdviceLevel::Warning, "Срок цели «" + g.name + "» наступил",
                           "До цели осталось накопить " + money(gp.remaining) + "."});
        } else if (gp.daysLeft <= 60) {
            res.push_back({AdviceLevel::Info, "Близкая цель «" + g.name + "»",
                           "Осталось " + std::to_string(gp.daysLeft) + " дн. Для достижения срока откладывайте по " +
                               money(gp.monthlyNeeded) + " в месяц."});
        }
    }
    Money owedToMe = ledger.totalOwedToMe(), iOwe = ledger.totalIOwe();
    if (owedToMe > 0 || iOwe > 0) {
        res.push_back({AdviceLevel::Info, "Баланс по долгам",
                       "Вам должны: " + money(owedToMe) + "; вы должны: " + money(iOwe) + "."});
    }

    // 8. Итог месяца
    if (s.income > 0 && s.balance() >= 0 && s.balance() >= s.income / 5)
        res.push_back({AdviceLevel::Good, "Хороший месяц",
                       "Остаток месяца " + money(s.balance()) + " — " + pct(percent(s.balance(), s.income)) +
                           " дохода. Переведите его в «Сбережения»."});
    return res;
}
