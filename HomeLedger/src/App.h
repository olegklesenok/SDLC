// App.h — консольный интерфейс приложения HomeLedger v1.0.0
#pragma once
#include <string>
#include <vector>

#include "Ledger.h"
#include "Storage.h"

class App {
public:
    explicit App(Storage storage) : storage_(std::move(storage)) {}
    int run(bool demo);

private:
    void mainScreen();
    void handleQuickInput(const std::string& input);
    void addOperation(OpType type);
    void addTransfer();
    void journal();
    void searchScreen();
    void statistics();
    void budgets();
    void goalsScreen();
    void recurringScreen();
    void debtsScreen();
    void advice();
    void walletsAndCategories();
    void exportAndBackups();

    bool editOperation(int id);
    bool deleteOperation(int id);

    // Вспомогательные методы ввода, Undo и сохранения
    bool ask(const std::string& prompt, std::string& out);  // false — конец ввода
    bool confirm(const std::string& question);
    bool askMoney(const std::string& prompt, Money& out, bool allowEmpty, Money def = 0);
    bool chooseCategory(OpType type, std::string& out, const std::string& def);
    bool chooseWallet(const std::string& prompt, std::string& out, const std::string& def);
    bool askMonth(Date& month);
    void pushUndo();
    void undoLastAction();
    void persist();
    void showBudgetWarning(const std::string& category, const Date& date);
    void printWrapped(const std::string& text, size_t width, const std::string& indent, bool indentFirst = true);
    void printOperationTable(const std::vector<Operation>& ops) const;
    void generateDemo();

    Storage storage_;
    Ledger ledger_;
    std::vector<Ledger> undoStack_;
    bool eof_ = false;
    std::string lastCategory_[2];  // последняя выбранная категория расходов / доходов
    std::string lastWallet_;
};
