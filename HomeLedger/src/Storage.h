// Storage.h — хранение данных в CSV-файлах, именованные снимки и экспорт CSV/HTML
#pragma once
#include <string>
#include <vector>

#include "Ledger.h"

struct BackupInfo {
    std::string name;       // имя подкаталога снимка
    std::string label;      // человекочитаемая метка
    int fileCount = 0;
};

class Storage {
public:
    explicit Storage(std::string dir) : dir_(std::move(dir)) {}

    // Загружает данные. Если каталога нет — создаёт его и заполняет стандартными категориями и кошельками.
    // Повреждённые строки пропускаются, описание проблем возвращается в warnings.
    bool load(Ledger& ledger, std::vector<std::string>& warnings);
    bool save(const Ledger& ledger, std::string& error) const;
    bool backup(std::string& error) const;  // копия файлов в каталог backup (*.bak)

    // Именованные снимки резервных копий (REQ-FUNC-025)
    bool createNamedBackup(const std::string& tag, std::string& outName, std::string& error) const;
    std::vector<BackupInfo> listNamedBackups() const;
    bool restoreNamedBackup(const std::string& name, Ledger& ledger, std::vector<std::string>& warnings,
                            std::string& error) const;

    // Отчёт за месяц для Excel (CSV, UTF-8 с BOM, разделитель ';') (REQ-FUNC-014)
    bool exportMonth(const Ledger& ledger, const Date& month, std::string& outPath, std::string& error) const;
    // Автономный интерактивный HTML-дашборд за месяц (REQ-FUNC-024)
    bool exportMonthHtml(const Ledger& ledger, const Date& month, std::string& outPath, std::string& error) const;

    const std::string& dir() const { return dir_; }

private:
    std::string path(const std::string& file) const;
    bool loadFromDir(const std::string& targetDir, Ledger& ledger, std::vector<std::string>& warnings) const;
    std::string dir_;
};

std::string moneyToCsv(Money m);  // 1234.56 (точка, без пробелов)
