# Описание дизайна программного обеспечения

Для **HomeLedger — Домашняя бухгалтерия**

Версия 2.0 (ЛР № 4 — базовая архитектура, ЛР № 5 — динамическое поведение UML 2.5)

Подготовил: Клещенок Олег Вячеславович, студент гр. 434701

БГУИР, военный факультет, кафедра ЭВМ

2026-11-19

## Оглавление

* [1. Введение](#1-introduction)
* [2. Обзор дизайна](#2-design-overview)
* [3. Проектные представления](#3-design-views)
* [4. Решения](#4-decisions)
* [5. Приложения](#5-appendixes)

## История изменений

| Автор | Дата | Причина изменений | Версия |
|---|---|---|---|
| Клещенок О. В. | 2026-11-05 | Первичная архитектура по SRS v0.2 (ЛР № 4, Раунд 1) | 1.0 |
| Клещенок О. В. | 2026-11-12 | Добавление целей накоплений, регулярных платежей, долгов, быстрого ввода, HTML-дашборда и снимков (ЛР № 4, Раунд 2) | 1.1 |
| Клещенок О. В. | 2026-11-19 | Внесение UML-моделей динамического поведения, глоссария и дополнительных спецификаций (ЛР № 5) | 2.0 |

---

## 1. Введение {#1-introduction}

### 1.1 Назначение документа {#11-document-purpose}

Настоящий документ описания проектирования программного обеспечения (Software Design Description, SDD) фиксирует архитектуру, компонентное разбиение, структуру данных и динамическое поведение системы «HomeLedger — Домашняя бухгалтерия». Документ разработан на основе стандарта IEEE 1016 / ISO/IEC/IEEE 42010:2011 и является прямым руководством к реализации для команды разработчиков (Middle- и Junior-разработчик), а также основой для рецензирования и приёмочного тестирования. Связанные документы: спецификация требований [SRS v0.2](../srs/srs-homeledger.md), записи архитектурных решений [ADR-0001..0006](../adr/), итерационный план [iterations-and-negotiation.md](../planning/iterations-and-negotiation.md), глоссарий [glossary.md](../uml/glossary.md) и дополнительные спецификации [supplementary-spec.md](../uml/supplementary-spec.md).

### 1.2 Область применения {#12-subject-scope}

Объект проектирования — настольное кроссплатформенное консольное приложение **HomeLedger v1.0.0** на языке C++17 (только стандартная библиотека и системное API консоли Windows/POSIX). Программа решает бизнес-цели БЦ-1…БЦ-5 из SRS: оперативный учёт доходов, расходов и переводов между кошельками, контроль месячных лимитов, расчёт статистики (правило 50/30/20, динамика 6 мес., индекс финансового здоровья), управление целями накоплений, регулярными платежами и долгами, а также экспорт отчётов в CSV (Excel) и автономный HTML-дашборд. Вне рамок релиза 1.0 остаются мобильное/веб-приложение, онлайн-синхронизация с банками, мультивалютность и парольная аутентификация (CON-1, CON-5).

### 1.3 Определения, акронимы и сокращения {#13-definitions-acronyms-and-abbreviations}

Подробный предметный и архитектурный глоссарий вынесен в отдельный файл [docs/uml/glossary.md](../uml/glossary.md). Базовые сокращения документа:

| Термин | Определение |
|---|---|
| ADR | Architecture Decision Record — запись принятого архитектурного решения в формате MADR 4.0 |
| BYN | Белорусский рубль; единственная расчётная валюта системы (CON-5) |
| CLI | Command Line Interface — текстовый консольный пользовательский интерфейс |
| DTO | Data Transfer Object — структура данных без логики хранения (`Operation`, `SavingsGoal`, `Debt`) |
| SDD | Software Design Document — описание проектирования ПО (IEEE 1016 / ISO 42010) |
| SRS | Software Requirements Specification — спецификация требований к ПО |
| UML | Unified Modeling Language — унифицированный язык моделирования версии 2.5 |

### 1.4 Ссылки {#14-references}

1. IEEE Std 1016-2009. Systems Design — Software Design Descriptions (нормативная база шаблона SDD).
2. ISO/IEC/IEEE 42010:2011. Systems and software engineering — Architecture description (точки зрения и представления).
3. SRS HomeLedger v0.2: [docs/srs/srs-homeledger.md](../srs/srs-homeledger.md) (нормативная основа требований).
4. Архитектурные решения MADR 4.0: [docs/adr/](../adr/) (обоснование ключевых решений).
5. План итераций и согласование объёма: [docs/planning/iterations-and-negotiation.md](../planning/iterations-and-negotiation.md).

### 1.5 Обзор документа {#15-document-overview}

Раздел 2 фиксирует заинтересованных лиц (стейкхолдеров), их ключевые задачи и выбранные архитектурные точки зрения. Раздел 3 описывает конкретные проектные представления (`DV-001`…`DV-010`) со сквозной трассировкой на требования SRS и ADR. Раздел 4 сводит принятые архитектурные решения. Раздел 5 содержит ссылки на UML-исходники, глоссарий и дополнительные спецификации.

---

## 2. Обзор дизайна {#2-design-overview}

### 2.1 Задачи стейкхолдеров {#21-stakeholder-concerns}

| Стейкхолдер | Ключевые задачи и риски | Как адресуется в дизайне |
|---|---|---|
| Владелец бюджета | Быстрый ввод трат (< 15 с), точные расчёты без копеечных погрешностей, контроль перерасхода и накоплений | Быстрый ввод одной строкой (`parseQuickInput`), `Money = long long` (ADR-0002), мгновенная проверка лимита, цели и советы |
| Член семьи | Понятный русскоязычный интерфейс, невозможность случайной потери записей | Подтверждение удаления, стек отмены `Undo`, автобэкап при запуске и именованные снимки (ADR-0003) |
| Разработчик (Middle / Junior) | Чёткие границы модулей для параллельной работы, быстрые автотесты | Статическая библиотека `ledger_core` без консольного ввода-вывода (ADR-0001), цели CMake `HomeLedger` и `ledger_tests` |
| Сопровождающий / GUI-инженер (релиз 2) | Переиспользование всей бизнес-логики под Qt 6 без переписывания | Изоляция классов `Ledger`, `Analytics`, `Storage` в `ledger_core` (REQ-REUSE-001) |

### 2.2 Выбранные точки зрения {#22-selected-viewpoints}

Для описания архитектуры HomeLedger выбраны следующие точки зрения по ISO/IEC/IEEE 42010:

1. **Контекстная (`Context`, п. 2.2.1):** определяет внешнее окружение системы (акторы, локальное хранилище CSV, браузер, Excel). Реализуется диаграммой вариантов использования UML 2.5 и контекстным представлением (`DV-001`).
2. **Композиционная (`Composition`, п. 2.2.2):** задаёт декомпозицию на статическую библиотеку ядра `ledger_core`, консольный контроллер `App/Console` и набор тестов (`DV-002`).
3. **Зависимостей (`Dependency`, п. 2.2.6):** фиксирует направленный ациклический граф включения заголовочных файлов без циклических ссылок (`DV-003`).
4. **Информационная (`Information`, п. 2.2.7):** определяет 7 табличных сущностей в CSV-хранилище, ключи и правила ссылочной целостности (`DV-004`).
5. **Логическая (`Logical`, п. 2.2.3) и интерфейсная (`Interface`, п. 2.2.8):** фиксирует классы C++17, структуры DTO и публичные контракты модулей (`DV-005`).
6. **Алгоритмическая (`Algorithm`, п. 2.2.10):** описывает детерминированные алгоритмы расчёта индекса финансового здоровья, советов 50/30/20, парсера быстрого ввода и генератора SVG (`DV-006`).
7. **Взаимодействия (`Interaction`, п. 2.2.9):** раскрывает обмен сообщениями во времени на диаграммах последовательности UML (`sd add_expense`, `sd recurring_and_export`, `DV-007`).
8. **Динамики состояний и потоков (`State Dynamics`, п. 2.2.11 / Activity):** формализует жизненный цикл лимита бюджета и финансовой цели на диаграммах состояний и активности UML (`DV-008`, `DV-009`).
9. **Развёртывания и ресурсов (`Deployment & Resources`, п. 2.2.14–2.2.15):** задаёт статическую сборку единого `.exe` без внешних DLL и управление файлами (`DV-010`).

---

## 3. Проектные представления {#3-design-views}

### DV-001 — Контекст и модель вариантов использования
- **Title:** Системный контекст и прецеденты HomeLedger
- **Viewpoint:** Контекстная (`Context`)
- **Representation:** Система работает полностью автономно на локальном ПК (CON-1, REQ-SEC-001), взаимодействуя с актёрами «Владелец бюджета», «Член семьи» и «Системные часы (Время)». Внешними файловыми потребителями выступают Excel/Calc (CSV) и веб-браузер (автономный HTML-дашборд). Модель прецедентов представлена на диаграмме [01-usecase.puml](../uml/puml/01-usecase.puml).

  ![Диаграмма вариантов использования](../uml/images/uml-01-usecase.png)
- **More Information:** Связанные требования `REQ-INT-005..007`, `REQ-SEC-001..003`, спецификации [usecases.md](../uml/usecases.md).

### DV-002 — Композиционная архитектура подсистем (`ledger_core` + `App`)
- **Title:** Трёхслойная компонентная архитектура
- **Viewpoint:** Композиционная (`Composition`)
- **Representation:** См. рисунок `sdd-01-architecture.png`. Система разделена на три слоя:
  1. **Слой представления и диалога (`App`, `Console`, `main`)** — отрисовка цветных ANSI-экранов (UTF-8 / `ReadConsoleW`), чтение команд, поддержка стека отмены `undoStack_`, вызов быстрого ввода.
  2. **Слой предметной логики и аналитики (`ledger_core`: `Ledger`, `Analytics`, `Date`, `Money`, `Text`)** — хранение состояния в оперативной памяти, валидация, расчёт балансов, индекса здоровья, прогнозов и советов.
  3. **Слой персистентности и экспорта (`Storage`)** — атомарная запись/чтение 7 CSV-таблиц через `.tmp` + `rename`, автобэкап, именованные снимки с ротацией, экспорт Excel CSV и HTML5+SVG.

  ![Слоистая компонентная архитектура](images/sdd-01-architecture.png)
- **More Information:** Реализует `ADR-0001`, `REQ-REUSE-001`, `REQ-MAINT-001`, `REQ-BUILD-001`.

### DV-003 — Граф зависимостей модулей
- **Title:** Направленные зависимости заголовков C++17
- **Viewpoint:** Зависимостей (`Dependency`)
- **Representation:** См. рисунок `sdd-02-dependencies.png`. Базовый уровень (`Date`, `Money`, `Text`) не зависит от доменных классов. `Ledger` зависит только от `Date`, `Money`, `Text`. `Analytics` зависит от `Ledger` и `Text`. `Storage` зависит от `Ledger`, `Analytics`, `Text`. `App` и `Console` зависят от `ledger_core`, при этом ни один модуль ядра не включает `App.h` или `Console.h` — цикл зависимостей исключён.

  ![Граф зависимостей модулей](images/sdd-02-dependencies.png)
- **More Information:** Реализует `ADR-0001`, `REQ-COMP-002`.

### DV-004 — Информационная модель данных и схема CSV
- **Title:** Структура постоянных данных и DTO
- **Viewpoint:** Информационная (`Information`)
- **Representation:** См. рисунок `sdd-03-data-model.png`. Постоянное состояние каталога `data/` представлено семью таблицами UTF-8 (разделитель `;`, RFC 4180):
  - `operations.csv`: `id;date;type;amount;category;wallet;to_wallet;comment`
  - `categories.csv`: `name;type;group`
  - `wallets.csv`: `name;initial`
  - `budgets.csv`: `category;limit`
  - `goals.csv`: `id;name;target;saved;deadline;completed`
  - `recurring.csv`: `id;name;type;amount;category;wallet;day;last_ym;active`
  - `debts.csv`: `id;person;direction;amount;repaid;created;due_date;comment`
  Все денежные поля хранятся с двумя знаками в типе `Money` (`long long` копеек, `ADR-0002`).

  ![Информационная модель данных](images/sdd-03-data-model.png)
- **More Information:** Реализует `ADR-0002`, `ADR-0003`, `REQ-INT-005`, `REQ-COMP-001`, `REQ-REL-005`.

### DV-005 — Интерфейсы и контракты классов ядра `ledger_core`
- **Title:** Публичное API доменной модели, аналитики и хранилища
- **Viewpoint:** Логическая (`Logical`) и интерфейсная (`Interface`)
- **Representation:**
  - `Ledger`: `validate(op)`, `add(op)`, `update(op)`, `remove(id)`, `between(from, to)`, `search(filter)`, `walletBalance(w)`, `totalBalance()`, `addGoal` / `contributeGoal`, `addRecurring` / `dueRecurring` / `applyRecurring`, `addDebt` / `repayDebt`.
  - `Analytics`: `computeStats`, `monthStats`, `budgetStatuses`, `monthlyDynamics`, `computeFinancialHealth`, `weekdayBreakdown`, `projectGoal`, `parseQuickInput`, `makeAdvice`.
  - `Storage`: `load`, `save`, `backup`, `createNamedBackup`, `listNamedBackups`, `restoreNamedBackup`, `exportMonth`, `exportMonthHtml`.
- **More Information:** Реализует `REQ-FUNC-001..025`.

### DV-006 — Алгоритмы аналитики, быстрого ввода и SVG-визуализации
- **Title:** Детерминированные вычисления и рендеринг отчётов
- **Viewpoint:** Алгоритмическая (`Algorithm`)
- **Representation:**
  1. **Индекс финансового здоровья (`computeFinancialHealth`):** сумма четырёх компонент $S = S_{\text{сбер}} + S_{50/30/20} + S_{\text{лим}} + S_{\text{под}} \in [0; 100]$, где $S_{\text{сбер}} \le 30$, $S_{50/30/20} \le 25$, $S_{\text{лим}} \le 25$, $S_{\text{под}} \le 20$ по 3-месячной подушке безопасности.
  2. **Быстрый ввод (`parseQuickInput`):** разбор префикса (`-`/`+`), суммы (`parseMoney`), каскадный поиск категории (`exact` → `prefix` → `substring` с кириллическим `toLowerUtf8`), откат на «Прочее» с сохранением текста в комментарии.
  3. **Векторный HTML-дашборд (`exportMonthHtml`):** расчёт угловых координат секторов кольцевой диаграммы `<path d="M ... A ...">` и нормирование столбцов 6-месячной гистограммы с экранированием `htmlEscape`.
- **More Information:** Реализует `ADR-0004`, `ADR-0005`, `REQ-FUNC-012-2`, `REQ-FUNC-023`, `REQ-FUNC-024`.

### DV-007 — Динамическое взаимодействие объектов (Sequence Diagrams UML 2.5)
- **Title:** Временные диаграммы ключевых сценариев
- **Viewpoint:** Взаимодействия (`Interaction`)
- **Representation:** В рамках ЛР № 5 разработаны две диаграммы последовательности:
  - `sd add_expense` ([06-sequence-add-expense.puml](../uml/puml/06-sequence-add-expense.puml)): взаимодействие `:App` → `:Ledger` → `:Storage` → `:Analytics` при добавлении расхода, включающее блок `alt` проверки остатка, условное сохранение `opt [confirmed]` и вызов `budgetStatus` с ограничением времени `{< 1000 ms}`.
  - `sd recurring_and_export` ([07-sequence-recurring-export.puml](../uml/puml/07-sequence-recurring-export.puml)): сценарий старта приложения, проверки `dueRecurring(today)`, проведения ожидающих регулярных платежей и генерации автономного HTML-дашборда `exportMonthHtml`.

  ![Диаграмма последовательности sd add_expense](../uml/images/uml-06-sequence-add-expense.png)

  ![Диаграмма последовательности sd recurring_and_export](../uml/images/uml-07-sequence-recurring-export.png)
- **More Information:** Реализует `ADR-0006`, `REQ-PERF-002`, `REQ-FUNC-011-2`, `REQ-FUNC-020`, `REQ-FUNC-024`.

### DV-008 — Диаграммы активности (Activity Diagrams UML 2.5)
- **Title:** Потоки управления ввода расхода и запуска приложения
- **Viewpoint:** Поведенческая / Потоки работ (`Activity`)
- **Representation:** Разработаны две диаграммы активности с дорожками ответственности:
  - `02-activity-add-expense.puml` — пошаговый сценарий добавления расхода с циклом валидации, проверкой баланса кошелька и мгновенным оповещением о лимите.
  - `03-activity-startup-recurring.puml` — процесс запуска, автоматического резервного копирования, проверки ожидающих регулярных платежей и перехода к главному меню.

  ![Диаграмма активности добавления расхода](../uml/images/uml-02-activity-add-expense.png)

  ![Диаграмма активности запуска и регулярных платежей](../uml/images/uml-03-activity-recurring.png)
- **More Information:** Реализует `ADR-0006`, `REQ-FUNC-001`, `REQ-FUNC-003`, `REQ-FUNC-018`, `REQ-REL-003`.

### DV-009 — Диаграммы состояний (State Machine Diagrams UML 2.5)
- **Title:** Конечные автоматы бюджетного лимита и финансовой цели
- **Viewpoint:** Динамики состояний (`State Dynamics`)
- **Representation:**
  - `04-state-budget.puml` — автомат `BudgetLimit`: состояния `Не задан`, `Норма (< 80 %)`, `Почти исчерпан (80–100 %)`, `Превышен (> 100 %)` и переходы по событиям `addExpense`, `editOperation`, `setBudget`, `newMonth`.
  - `05-state-goal-and-debt.puml` — автомат `SavingsGoal`: состояния `Активна (в процессе)`, `Срок наступил (просрочена)`, `Достигнута` с действиями пересчёта ежемесячного взноса.

  ![Диаграмма состояний лимита категории](../uml/images/uml-04-state-budget.png)

  ![Диаграмма состояний финансовой цели](../uml/images/uml-05-state-goal.png)
- **More Information:** Реализует `ADR-0006`, `REQ-FUNC-010`, `REQ-FUNC-011-2`, `REQ-FUNC-019`.

### DV-010 — Развёртывание и управление ресурсами
- **Title:** Автономная сборка и файловая структура
- **Viewpoint:** Развёртывания и ресурсов (`Deployment & Resources`)
- **Representation:** Сборка через CMake 3.16+ (MinGW-w64 с флагом `-static` или MSVC) порождает самодостаточный исполняемый файл `HomeLedger.exe` (~2 МБ), не требующий инсталлятора и внешних библиотек (REQ-INST-001). Рабочие файлы размещаются в каталоге `--data` (по умолчанию `./data`), снимки — в `data/backup/snapshots/` с ротацией не более 10 каталогов.
- **More Information:** Реализует `REQ-INST-001`, `REQ-PORT-001`, `REQ-REL-002..003`.

---

## 4. Решения {#4-decisions}

| ID | Заголовок | Статус | Связанные представления |
|---|---|---|---|
| [ADR-0001](../adr/0001-layered-architecture-ledger-core.md) | Разделение на слои с независимой библиотекой ядра `ledger_core` | accepted | DV-002, DV-003, DV-005 |
| [ADR-0002](../adr/0002-integer-kopeck-money.md) | Представление денежных сумм в целых копейках (`long long`) | accepted | DV-004, DV-006 |
| [ADR-0003](../adr/0003-csv-atomic-storage-and-backups.md) | Локальное CSV-хранилище с атомарной записью и именованными снимками | accepted | DV-004, DV-010 |
| [ADR-0004](../adr/0004-deterministic-advice-and-health-index.md) | Детерминированные правила советов и композитный индекс финансового здоровья | accepted | DV-006 |
| [ADR-0005](../adr/0005-dual-export-csv-and-offline-html-dashboard.md) | Двойной экспорт отчётов — табличный CSV и автономный HTML-дашборд с SVG | accepted | DV-006 |
| [ADR-0006](../adr/0006-plantuml-dynamic-behavior-modeling.md) | Механизмы построения, формализм и трассировка динамических UML-диаграмм | accepted | DV-001, DV-007, DV-008, DV-009 |

---

## 5. Приложения {#5-appendixes}

- Глоссарий проекта (табличный): [docs/uml/glossary.md](../uml/glossary.md)
- Дополнительные спецификации (FURPS+): [docs/uml/supplementary-spec.md](../uml/supplementary-spec.md)
- Спецификации актёров и прецедентов: [docs/uml/usecases.md](../uml/usecases.md)
- Исходные тексты UML 2.5 в нотации PlantUML: [docs/uml/puml/](../uml/puml/)
- Иллюстрации архитектуры (ЛР № 4) и UML (ЛР № 5): [docs/sdd/images/](images/), [docs/uml/images/](../uml/images/)
