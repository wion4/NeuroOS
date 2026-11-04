# Продолжение разработки

Пользователь 2026-11-04 разрешил работать автономно до доступного лимита токенов.
Goal активен; завершение отдельного теста/релиза не означает завершения ОС.
После проверенного результата выбирать следующий ограниченный шаг и продолжать.
Не изображать работу в фоне, если агент не работает; перед handoff сохранять
незавершённое и конкретное продолжение. Не требовать повторного разрешения
на уже согласованные обычные изменения/тесты/публикацию исходников.

## Приоритет

ИИ интегрируется последним. Сейчас — ОС и универсальные средства управления,
проверяемые обычными программами без модели/весов. Архитектура выбрана: собственная
микроядерная ОС x86-64, не дистрибутив. .nos — ELF64, local ABI ident[7]=0x4e,
ident[8]=1. Прототип AI v0.0.17–18 сохранён optional и заморожен; не развивать
модели/чат/inference до готовности основания. Требования — system-control.md.

## Текущий checkpoint — v0.0.21

Приватный пользовательский loader читает согласованный executable до 1 МиБ;
START возвращает ID сразу, LOADING=4, GUI принимает ввод, Stop работает до
Started. Kernel syscall22 проверяет/копирует образ и заменяет task без наследования
loader role/heap/прав. ABORT snapshot обязателен; file grant конечного приложения
независим. Loading wall deadline 6000 ticks, у программы свежий CPU budget1000.
Save/Files во время read snapshot могут получить busy; текст Notes сохраняется.
CLI/совместимый syscall13 остаются синхронными. См. loader.md.

Полный check-release --desktop PASS 24 этапа без AI; FS2 job controller PASS.
Настоящий 1 МиБ, Stop loading, owner exit flags0, exact PMM, loader #PF,
естественный deadline, FS mid-PIO #PF и чужой in-flight FS request проверены.
Последний тест воспроизводил ложный отказ общей idle-проверки: исправлено,
Started/exit42 происходит до завершения чужого STAT. Missing -2/чужой TX -16
сохраняют snapshot настоящего owner. На копии 25 файлов старый compiler/SDK
собрали C с puts, новый executable вернул37; оригиналы не менялись.

ISO 0.0.21 4,114,432 байт без AI загружен в VMware. Девять user LOAD payloads,
process.o/journal.o совпали с проверенными, весь flat disk и 25 файлов прежние.
SHA 460d70eba035dd11e5e0fb4d3f20b66f9162a124886451510808a33c76a8d299.
Backup work/backups/before-0.0.21-20261104-134713; manifest/report
build/vmware-0.0.21-*.json; compatibility build/vmware-compat-0.0.21/result.json.

## Предыдущий checkpoint — v0.0.20

Модель не используется. Programs → Events: числовой lifecycle native jobs,
owner/job/sequence/ticks, результат, наблюдённые peak pages/CPU ticks. Kernel
RAM-кольцо на 64 записи, syscall 21 только trusted controller, независимые
cursor/lost, overflow без wraparound; никаких указателей/текста/IO в журнале.
История переживает перезапуск desktop, не reboot. Полный постоянный аудит,
boot ID и scoped grants ещё не реализованы. См. events.md.

Полный check-release --desktop: PASS всех 21 этапов без AI. Дополнительно PASS
job_control с FS2. Реальный ring3 проверил права/буферы/версию/cursor, exit42,
load error -8, cancel CPU, rollback1, heap peak >400, fault14, CPU limit1000
и wrap кольца. GUI проверил наблюдение при busyFS/CPU, Stop, missed, restart
controller, owner exit, точные файлы и PMM; FS recovery видна после mid-PIO #PF.
Host journal проверен у UINT64_MAX. Рабочий ISO 0.0.20 4,087,808 байт загружен
в VMware без AI. Все 25 файлов и весь flat disk прежние; 8 user LOAD payloads,
process.o/journal.o совпали с тестовыми. Backup — work/backups/
before-0.0.20-20261104-130711; локальные manifest/result — build/vmware-0.0.20-*.json.

## Основа, реализованная в v0.0.19

v0.0.19 — обычная фоновая .nos, без AI в рабочем образе. GUI сохраняет заметки
во время CPU-bound задачи. Programs: Run/Stop, аргументы, вывод до 2047 байт,
exit/fault/CPU limit/cancel. Запуск и права зафиксированы до завершения; это
не интерфейс динамического отзыва части прав. Source C -> Build C работает.
Русский ввод/UTF-8/clipboard/undo/redo/maximize сохранены. Окна инструментов
пока в одном процессе. Фаза загрузки/проверки ELF ещё приостанавливает desktop.

Контракт — jobs.md. Вызов 20 START/POLL/CANCEL/REAP доверенного desktop;
один outstanding job до REAP. Непрозрачный ID монотонный и не переиспользуется.
Slot 4 — обычный ребёнок, IPC 0 — общие FS requests, ответы desktop/ребёнку —
1/4. IDs привязываются к ответам, не только к слотам. Kernel broker — сессия 0,
которая сохраняется между временными fsclient.nos RPC-помощниками; GUI и ребёнок
имеют разные положительные сессии. Новому помощнику не выдаётся новый владелец
одной kernel-транзакции. Sleep(19) обычных приложений не сбрасывает CPU ticks.

FS3 (и legacy FS2) понимают kernel-authenticated CLIENT_CLOSED(13). При завершении
ребёнка queued requests удаляются до нового выбора задачи; подтверждение закрытия
обрабатывает ядро. Backend откатывает только неподтверждённый draft этого владельца.
Одновременно одна явная транзакция; чужие операции получают busy без потери текста.
Выполняющаяся IO может завершиться до отмены; committed не откатывается. Никакого
автоматического replay неудачной записи. Сбой FS восстанавливает новый сервис
с committed roots и сохраняет GUI. Exit родителя отменяет ребёнка; активный FS
контекст при таком exit отбрасывается с последующим mount через supervisor.

Остаются ограничения: file grants ко всему FS, нет per-object/revoke, несколько
jobs/threads/stdin ещё не поддержаны, общего app manager/window compositor нет.
FS3: 128 файлов по 1 МиБ, ASCII имена до 15 байт, каталогов нет. User heap 2 МиБ,
RO mappings больших файлов/compute buffers впереди. C/ASM есть, C++ ещё нет.
Диагностическая консоль и ранние PC-драйверы пока в ядре. Сети/полного GPU нет.

## Проверки и рабочая VM

Полный check-release --desktop из 18 этапов прошёл без AI. После добавлен и
отдельно пройден native_recovery; теперь --desktop содержит 19 этапов.
На итоговом коде пройдены native_background/recovery после фиксации launch settings,
supervisor, SDK, guest FS3 и job_control с legacy FS2. Прямой ring3 controller
проверяет stale IDs, неверные буферы, busy/reap, два файловых клиента, подделку
CLIENT_CLOSED, private draft и rollback. Native UI проверяет ввод/save во время
spin, Stop работающего/спящего ребёнка, busy и сохранность заметки, exit родителя
и точный PMM. Recovery вводит настоящий #PF после 100 PIO words через loopback
GDB stub своего QEMU; GUI/данные сохранены, write не повторяется. См. validation.md.

На копии реальных 25 файлов старый установленный compiler.nos через новый GUI
скомпилировал C; результат вернул 37. На реальном диске прежние файлы не заменялись.
Рабочий ISO 0.0.19 (4,061,184 байт) загружен в VMware без AI. Все 25 файлов и
весь data-flat.vmdk побайтово прежние; LOAD восьми программ и process.o совпали
с тестовыми. Локальный отчёт build/vmware-0.0.19-validation.json, manifest backup
build/vmware-0.0.19-backup.json. Backup в work/backups/before-0.0.19-20261104-115750
от workspace new-chat. Предыдущий ISO 0.0.18 сохранён вместе с копией диска.

## Следующий ограниченный шаг — файловые права по именам и операциям

Текущее NOS_LAUNCH_FILES выдаёт весь FS. Ограниченный private loader уже
проверяет имя/операции в ядре. Обобщить policy для обычного native job: новый
append-only START op с bounded списком до четырёх ASCII имён и явными правами
read/write/create/delete. Прежний START/file flag оставить совместимым.
Не выдавать обычной программе controller/loader/ATA/display или sender identity.

Сначала отделить policy в проверяемый C модуль; затем привязать её snapshot
к task/session при exec. Проверять каждый запрос до постановки в FS очередь,
отклонять LIST и закрытие чужого клиента. В scoped режиме не разрешать операции
над невыданными именами; read snapshot BEGIN/ABORT и mutating COMMIT должны
соответствовать правам. Проверить попытки обхода через payload, переполнение,
неверный grant/имя/op, чтение/запись чужого файла, создание/удаление и rollback.
Это права по текущим именам, пока не stable object handles/rename/revocation.
Частичный динамический отзыв не имитировать; Stop по-прежнему закрывает весь job.

Проверить настоящим ring3 controller/обычным C без модели, сохранить broad grant
для компилятора/SDK, UI default-off и защиту данных. Затем необходимы scoped UI,
общий manager/несколько задач, отдельный отзыв, большие files/mappings,
collector/подписки и семантические API GUI. Пользователь уже разрешил продолжать;
не завершать larger task только из-за публикации очередного checkpoint.

## Публикация и данные

Публичный репозиторий https://github.com/wion4/NeuroOS; публикация исходников
разрешена. Оригинальный код пока без лицензии по выбору владельца; vendor licenses
сохранены. Веса/диски/logs/живой VMX не публиковать. Публичный бинарный ISO не
выкладывался. В Git обезличенный vmx.template и setup-vm.py. Реальная VM —
vmware/NeuroOS.vmx, образ — build/NeuroOS.iso, данные — vmware/data-flat.vmdk.

Не удалять и не перезаписывать реальный VMDK, не трогать физические диски.
Перед обновлениями VM — выключение и SHA-проверенная копия. Все тесты используют
свои явно названные образы в build/. Прежние backup сохранять. GitHub credentials
не выводить/не коммитить. Архив legacy Claude — исторические данные; нужные
инструкции уже прочитаны, не грузить архив целиком и не печатать секреты.
