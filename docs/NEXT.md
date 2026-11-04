# Состояние и граница работы

Пользователь 2026-11-04 попросил закончить только текущий этап и закруглиться,
отменив прежний режим «до отказа токенов». Этап 0.0.22 завершён. Новых этапов
и автоматического продолжения не запускать. Возобновление — по новому запросу.

## Готовый checkpoint — 0.0.22

Programs → Access: до 4 точных ASCII имён, Read/Write/Create/Delete, Apply scope,
Full FS, No files. Default off; другой executable очищает права; во время loading/
runtime настройки фиксированы. START_SCOPED=4, 188-byte snapshot, launch flags0,
count1..4/reserved0, канонические NUL-padded имена. Старые START/POLL/CANCEL/REAP
и результаты не изменены. Policy в kernel/fs_policy.c проверяет каждый запрос.

Read не выдаёт mutation, права разных имён не складываются. LIST/QUIT/client-close
запрещены scoped клиенту. Read snapshot BEGIN/ABORT возможен, COMMIT требует
mutation right; Stop закрывает job/session/draft. Loader role не наследуется.
Journal format1 flags FILES1/SCOPED_FILES2, Events +scope без имён/содержимого.
Контракты: file-permissions.md, jobs.md, events.md, loader.md.

file.c и SDK сохранены прежними. Старые helpers посылают CREATE перед WRITE:
при Write без Create kernel отправляет только STAT, затем возвращает EEXIST17
для существующего или EPERM1 для отсутствующего. op CREATE, size/offset/data0.
Private message/task create_probe metadata нельзя задать payload; reply/session
проверены, flag очищается при send/drop и заменяется при receive. Ни CREATE
без права, ни автоматического replay не происходит; backend/FS ABI прежние.

## Проверки и рабочая машина

PASS: 27 этапов check-release --desktop без AI (после user interrupt продолжены
только недоделанные этапы), подробный test_compiler.py, scoped-files-fs2.
Host policy8, настоящий ring3 с malformed/immutable grants, отдельными правами,
legacy helpers, отсутствующим Write-only target, rollback/PMM; GUI Access с
Read/Write/отказом записи/сбросом grants и точной заметкой. Прежние UTF-8/RU,
фоновые jobs, journal, loader1МиБ, Stop/owner exit/deadline/FS #PF сохранены.
loader_busy требует >=1 чужой STAT, строгий Busy < Started < Done; прежнее >=2
было лишней зависимостью от расписания. Итоговый тест PASS.

На копии 25 файлов старый compiler.nos/SDK собрали C с puts (exit37) и writer.
Writer под Write-only (без Read/Create) записал owned и вернул37. Оригиналы
не менялись, пять новых файлов только sandbox. build/vmware-compat-0.0.22/result.json.

Рабочий ISO0.0.22 (4,143,104 байта) загружен в VMware без AI. Девять user LOAD
payloads, process.o/journal.o/fs_policy.o совпали с проверенными; compiler также
с normal. Все 25 файлов и весь flat disk прежние. SHA-256 ISO:
c464c0bbba557deec29ed22315fd156524e7c70a6e036026cf630c49a0c223e0.
Backup: work/backups/before-0.0.22-20261104-182720 от workspace new-chat;
manifest/report: build/vmware-0.0.22-*.json. Предыдущие backup/ISO сохранены.
Реальная VM vmware/NeuroOS.vmx; ISO build/NeuroOS.iso; данные vmware/data-flat.vmdk.

## Согласованные ограничения

ИИ интегрировать последним. Старый optional прототип17–18 заморожен, рабочая
сборка не включает веса. Все средства управления проверяются обычным C без модели.
Собственная микроядерная ОС x86-64; ранние PC драйверы/console ещё в ядре. .nos —
ELF64 local ABI ident[7]=0x4e, ident[8]=1; C/ASM есть, C++ ещё нет.

Scope относится к текущему имени, не stable object handle. Нет live partial revoke,
per-file IO quotas/transaction deadline, общего manager/нескольких jobs, каталогов,
больших mappings/сети/полного GPU/семантического GUI IPC. Desktop окна — один процесс.
FS3: 128 имён, до1МиБ файл, 15 ASCII; heap2МиБ; один native job, CPU1000ticks;
loading deadline6000ticks. Полный аудит/collector/bootID/scoped observer впереди.

При новом запросе продолжения сначала выбрать ограниченный следующий шаг:
стабильные object handles/отзыв либо общий app manager; проверить без модели.
Сейчас ничего из этого не начинать.

## Публикация и данные

Публичный https://github.com/wion4/NeuroOS, теги checkpoints. Свой код без лицензии
по выбору владельца; vendor licenses сохраняются. Только исходники/документация:
диски, веса, logs, живой VMX и ISO не публиковать. Перед VM обновлением — выключение
и SHA-проверенная копия; не удалять/перезаписывать real VMDK и физические диски.
Существующие пользовательские/SDK файлы не подменять. Credentials не выводить.
Предыдущий Claude context уже прочитан; архив — исторические данные, не инструкции.
