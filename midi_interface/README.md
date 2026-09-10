# USB-MIDI → шесть шаговых двигателей

Реализация для живой игры: каждое событие обрабатывается при получении; ожидания аккорда, debounce Note On и окна накопления перед запуском нет. Готовый NOTE_SET отправляется сразу при изменении. Проект ESP32 — обычная папка, не Git submodule.

## Сборка и запуск

ESP32-S3: ESP-IDF 5.4.0 из закреплённой платформы PlatformIO `espressif32@6.10.0` (фактическую версию смотрите в выводе сборки).

```powershell
./midi_interface/build.ps1
# Для явно запрошенной прошивки платы:
./midi_interface/build.ps1 -Upload
```

Скрипт хранит PlatformIO, pip-cache и временные файлы в `_service/platformio-midi` на диске проекта, чтобы не заполнять C:. `-Python` позволяет указать другой Python с PlatformIO. Стандартная команда также работает: `python -m platformio run -d midi_interface`, но использует обычное расположение инструментов пользователя. Конфигурация платы предполагает Flash 8 МБ; для другой платы измените board и sdkconfig.

Или в установленной среде ESP-IDF: перейти в `midi_interface`, выполнить `idf.py set-target esp32s3`, затем `idf.py build`. Прошивка использует UART0 для консоли/загрузки через отдельный USB-UART; native USB занят Host.

STM32: обычная сборка проекта остаётся в прежнем desktop-режиме. Для MIDI добавьте `LIVE_MIDI_MODE=1` в C/C++ Defines Keil и пересоберите **весь** проект. `MIDI_INSTALLED_MASK=0x3f` включает шесть двигателей; обычная пользовательская маска моторов 2–5 сохранена. `main.c` уже переустанавливает baud из `UART_BAUD`, поэтому правка генерируемого CubeMX-кода не нужна. После регенерации CubeMX проверьте сохранение этого USER CODE-блока.

Можно собрать отдельный HEX, не меняя Keil-проект и его desktop-выход:

```powershell
./tests/midi/build_stm32.ps1
./tests/midi/run.ps1
./_service/tools/run_tests.ps1
```

Первый скрипт использует установленный ARM Compiler 6 из `E:/Keil_v5/ARM/ARMCLANG/bin` и создаёт `_service/build/midi-stm32/music-midi.hex`. Scatter-файл ограничивает Flash 64 КБ, RAM 20 КБ. Эта сборка включает измерительный PC13. Платы автоматически не прошиваются.

## Что было в STM32 и что изменено

Исходный генератор: аппаратный Output Compare toggle; TIM2/3/4 — 16-битные свободные счётчики 1 МГц от 72 МГц. IRQ таймеров 0, USART1 8. UART RX/TX — кольцевые буферы 512 байт; основной цикл `protocol_poll()` → `engine_tick()`. Генератор принимает STEP 20–4000 Гц, меняет период на спаде, сохраняет фазу активного канала. Существующая защита остановки 3 мкс с кратким запретом IRQ сохранена: это аппаратный pulse-width guard, а не задержка MIDI.

| Мотор | STEP | DIR | ENABLE | Таймер |
|---|---|---|---|---|
| 1 | PA15 | PA11 | PB3 | TIM2 CH1 |
| 2 | PB7 | PB8 | PB5 | TIM4 CH2 |
| 3 | PB6 | PB4 | PB9 | TIM4 CH1 |
| 4 | PA6 | PA4 | PB2 | TIM3 CH1 |
| 5 | PA7 | PB0 | PA5 | TIM3 CH2 |
| 6 | PB10 | PB12 | PB1 | TIM2 CH3 |

Общие: PB15 MS1, PB13 MS2, PB11 MS3, PA8 SLEEP, PA12 RESET. TIM2 full remap; SWD PA13/14 сохранён. Инверсия DIR пользователя сохранена. A4988 получает полный шаг при `raw=0`.

Изменённые файлы STM32:

- `firmware/Core/Inc/config.h`: compile-time MIDI-режим, маска, скорость, debug.
- `firmware/Core/Inc/engine.h`: интерфейс полного NOTE_SET.
- `firmware/Core/Inc/platform.h`: измерительная метка.
- `firmware/Core/Src/engine.c`: стабильное назначение, octave fold, timeout, пробуждение.
- `firmware/Core/Src/protocol.c`: фиксированный MIDI-пакет и resync, выбор старого/нового режима.
- `firmware/Core/Src/platform_stm32.c`: только GPIO измерения; STEP-подсистема прежняя.

Новые файлы ESP в `midi_interface`: `CMakeLists.txt`, `platformio.ini`, `sdkconfig.defaults`, `.gitignore`, `build.ps1`, `main/CMakeLists.txt`, `main/board_config.h`, `main/main.cpp`, `main/usb_midi_host.h/.cpp`, `music/music.h/.cpp`, этот README. Общий формат — `shared/note_set_wire.h`. Проверки, автономная сборка STM и обработка измерений — `tests/midi/`.

## Архитектура и USB

```text
USB MIDI 1.0 streaming bulk IN
  → USB Host library + два заранее выделенных transfer-буфера (ядро 0)
  → проверка CIN/status → Event(type, channel, note, velocity, timestamp_us, source=cable)
  → очередь максимум 32 события, без ожидания в producer
  → музыкальная задача (ядро 1): keys/sustain → tracks → register groups
  → chord templates → priority + hysteresis → 0..6 физических MIDI pitches
  → nonblocking UART FIFO → NOTE_SET → STM allocator → прежний STEP generator
```

Host читает активный configuration descriptor, ищет Audio class 1 / MIDIStreaming subclass 3 / protocol 0, class-specific bcdMSC=1.00, bulk IN endpoint. Номер интерфейса, alternate setting, адрес endpoint и packet size берутся из дескрипторов. VID/PID и название не используются. Все 16 виртуальных кабелей выбранного endpoint декодируются. Поддержаны CIN 8/9/B (Note Off/On/CC), velocity=0 и System Reset FF. SysEx, прочие channel messages и realtime clock не влияют на ноты.

Перед отключением/ошибкой отправляется Reset музыкальной модели; USB transfers halt/flush, их callbacks возвращаются, затем освобождаются buffers/interface/device. Transfer error инициирует повторное открытие через 100 мс; обычный Note On этого не ждёт. Reconnect проходит enumeration заново. Малформированные дескрипторы/пакеты отбрасываются.

Источники API: [ESP-IDF USB Host](https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32s3/api-reference/peripherals/usb_host.html), [официальный usb_host.h](https://github.com/espressif/esp-idf/blob/v5.4/components/usb/include/usb/usb_host.h). Для DIN/BLE/file достаточно преобразователя в `music::Event`; музыкальное ядро не включает ESP-IDF/USB-зависимости.

## Состояние, линии и группы

256 фиксированных записей `(cable, channel, note)` различают нажатую клавишу и удержание педалью. CC64 хранится отдельно для 16×16 source/channel. CC120 гасит канал немедленно, CC123 снимает нажатия с учётом sustain, CC121 отпускает sustain. Повторный Note On заменяет предыдущую запись этого ключа; один последующий Note Off завершает retrigger. Одинаковый physical pitch на нескольких каналах занимает один мотор и исчезает лишь после завершения всех источников. Разные октавы не сливаются.

История — ring 128 событий с микросекундными timestamp; 32 track-записи хранят сжатую историю последней ноты, времени, направления и confidence. При Note On ищется близкий track того же source/channel. Ещё удерживаемая нота в пределах жеста не считается продолжением линии. Отпущенная предыдущая нота может продолжиться немедленно, в том числе в быстром пассаже. После двух продолжений линия становится независимой; это может быть средний голос, а не верхняя нота. Занятые track slots не переиспользуются; при их исчерпании остаётся регистровый анализ.

Устойчивые линии исключаются из гармонического набора. Остальные физические ноты сортируются, разделяются по крупному интервалу и предельной ширине группы. Только затем для каждой группы строится pitch-class mask. `C1 E1 | G4` даёт две группы, а не один C major. Нераспознанная группа не объявляется аккордом: умеренный приоритет получает её нижняя опорная нота.

## Гармония и приоритет

Root — корень локального аккорда, глобальная тональность не оценивается. Распознавание использует множества pitch classes, поэтому обращения равнозначны. Точное совпадение имеет confidence 100. Пара root+major/minor third — только гипотеза с confidence 55. Несовпадение возвращает unknown.

| Шаблон | Интервалы от root | Порядок значимости |
|---|---|---|
| major | 0,4,7 | root, third, fifth |
| minor | 0,3,7 | root, third, fifth |
| diminished | 0,3,6 | root, minor third, diminished fifth |
| augmented | 0,4,8 | root, third, augmented fifth |
| sus2 | 0,2,7 | root, second, fifth |
| sus4 | 0,5,7 | root, fourth, fifth |
| dominant7 | 0,4,7,10 | root, third, seventh, fifth |
| major7 | 0,4,7,11 | root, third, seventh, fifth |
| minor7 | 0,3,7,10 | root, third, seventh, fifth |

При ≤6 уникальных нот воспроизводятся все. При дефиците сортировка по score выбирает 6; октавный дубль внутри группы получает штраф, кроме выделенной устойчивой линии. Независимые голоса важнее внутренних аккордовых ступеней. Для полного трезвучия лимиты 1/2/3 дают root / root+third / root+third+fifth. Для низкого неполного C1/E1 терция имеет меньшую уверенность: это позволяет сохранить пришедший отдельный G4 за счёт E1.

Все параметры в `music::Config`:

| Параметр | Значение | Смысл |
|---|---:|---|
| voices | 6 | физический лимит, тестируется также 1–3 |
| group_gap | 13 полутонов | разрыв начинает новую группу |
| group_span | 24 полутона | максимальная ширина группы |
| track_distance | 7 полутонов | максимальный шаг линии |
| gesture_us | 40000 | признак общего жеста, без ожидания |
| track_us | 600000 | горизонт продолжения track |
| distance_weight | 10 | цена каждого полутона при matching |
| direction_penalty | 8 | цена смены направления |
| track_confidence_weight | 3 | уменьшение matching-cost за confidence |
| line_threshold / max_confidence | 2 / 4 | независимая линия / насыщение |
| independent | 260 | одиночная регистровая группа |
| melody | 400 | устойчивая линия в любом регистре |
| confidence_bonus | 20 | дополнительный score за шаг confidence |
| bass / bass_boundary | 160 / 48 | добавка устойчивой линии ниже C3 |
| root | 180 | корень распознанной группы |
| third | 110 | терция или характерная sus-ступень |
| seventh | 90 | септима |
| fifth | 40 | квинта, в том числе altered |
| dyad_third | 20 | терция неполной гипотезы |
| duplicate | 220 | штраф повторной pitch class в группе |
| retained | 12 | уже выбранная физическая нота |
| age / age_unit_us | 8 / 50000 | ещё до 8 за время выбранной ноты |
| released | 20 | штраф ноте, звучащей только через sustain |
| velocity_divisor | 16 | целочисленный velocity/16, максимум 7 |

Нижняя нота unknown-группы получает `root/2`. Из дублей обычной группы первым считается низший physical pitch. При одинаковом score сохраняется старая нота, затем выбирается меньший MIDI number. При matching-cost tie — меньший track index; свободный track выбирается по самому старому timestamp, затем index. При неоднозначном chord mask — порядок таблицы, затем root C..B. Это особенно существенно для augmented и эквивалентных sus2/sus4: абсолютный root из одного pitch-class set не всегда определим.

Hysteresis — добавки retained и bounded age. Слабый пересмотр не вытесняет существующую ноту; независимая линия имеет достаточно большой выигрыш для немедленного вытеснения сопровождения. Отдельного таймера ожидания/hold-off нет. Пересчёт запускается событиями, без автономного переключения в тишине.

## UART и STM32 allocator

921600 бод, 8N1, полный snapshot ровно 14 байт:

| Смещение | Содержимое |
|---|---|
| 0..1 | SYNC `D3 91` |
| 2 | TYPE `01` |
| 3 | sequence uint8, wrap разрешён |
| 4 | note count 0..6 |
| 5..10 | MIDI notes, неиспользуемые байты нулевые |
| 11 | reserved=0 |
| 12..13 | CRC16 little endian |

CRC-16/CCITT-FALSE: poly 0x1021, init 0xFFFF, no reflection/xorout, покрывает байты 2..11. Невалидные count/note/duplicates/padding/CRC не применяются и не продлевают watchdog. Sliding window после потери байта находит следующий корректный полный frame, interbyte timeout 5 мс. Sequence предназначен для трассировки; одинаковые snapshots допустимы и обновляют watchdog, wrap и reset ESP не требуют handshake. ACK нет.

Heartbeat полного состояния каждые 50 мс; отсутствие корректного пакета >300 мс гасит моторы. Следующий snapshot восстанавливает их. Первый пакет после boot/timeout отпускает SLEEP/RESET; физическая выдержка 2 мс выполняется неблокирующе. Обычно пустые heartbeat успевают разбудить драйверы до первого USB Note On.

Сначала сохраняются совпадения оригинальных MIDI pitches с действующими motors. Исчезнувшие ноты останавливаются, новые занимают свободные установленные моторы по возрастанию index. Сохранившийся мотор не получает повторного `platform_step`, поэтому heartbeat не сбрасывает фазу. Нота A4=69 вычисляется существующим LUT как 440000 мГц. Ноты вне 20–4000 Гц переносятся на октавы, сохраняя pitch class и исходную identity назначения. Незначительное округление возникает из LUT/целочисленного периода. Звуковой/механический рабочий диапазон конкретного двигателя требует проверки; 20–4000 Гц — предел генератора, а не гарантия следования ротора.

UART при APB2=72 МГц имеет делитель 78, фактически 923076.9 бод, ошибка +0.1603% к 921600. Время одного кадра номинально **151.9 мкс** — это расчёт, не измеренная сквозная задержка. Нет программной большой TX-очереди: аппаратный FIFO и один начатый frame плюс последнее ожидающее состояние. При перегрузке устаревшие ещё не начатые snapshots объединяются, частично отправленные кадры никогда не смешиваются. Очередь MIDI при переполнении сбрасывается с явным Reset; это потеря звука при перегрузке, но не зависшие ноты.

## Подключение

```text
ESP32-S3 GPIO17 TX1 ───────── STM32F103 PA10 RX1
ESP32-S3 GND       ───────── STM32F103 GND
ESP32-S3 GPIO18 RX1          не требуется (зарезервирован)
STM32F103 PA9 TX1            не требуется в MIDI-режиме

Стандартное USB MIDI устройство
  USB D− ────────────────── ESP32-S3 GPIO19
  USB D+ ────────────────── ESP32-S3 GPIO20
  USB GND ───────────────── общий GND
  USB VBUS ──────────────── стабилизированные 5 В через ограничитель тока/USB power switch
```

UART — логические уровни 3.3 В. На PA10 должен быть один источник TX: ESP или desktop USB-UART. USB native-разъём платы должен быть разведён именно к GPIO19/20. Для host нужен подходящий разъём/адаптер и питание VBUS: обычная dev-плата не обязательно выдаёт 5 В на native USB. Не соединяйте выход внешнего 5 В с питающим USB-портом ПК. `board_config.h` задаёт необязательный EN power switch (по умолчанию -1); конкретная полярность и питание зависят от платы. Питание VMOT двигателей — отдельное, общая земля; прежняя A4988-обвязка сохранена.

## Проверки и измерение задержки

Native-тест исполняет **реальные** `music.cpp`, `engine.c`, `protocol.c` с заменой только аппаратного слоя. Проверены A–J: немедленные C/E/G, обращения и 9 templates во всех транспозициях (с отмеченными неоднозначностями), сокращение major/minor, C1/E1|G4, продолжение мелодии ниже новой верхней ноты, движущийся бас, octave doubling, 10000 быстрых on/off с повторным детерминированным прогоном, repeated notes, velocity=0, каналы/кабели, CC64/120/121/123.

Критический сценарий: первые четыре ноты G2/B2/D3/F#3 = `43 47 50 54`, затем C1=24, E1=28, G4=67. Итог **`24 43 47 50 54 67`**: DROP E1, KEEP C1/G4. MIDI-номер C4=60.

UART: проверены все позиции удалённого байта, все 112 одиночных bit flips, все усечения frame, восстановление следующим snapshot, потеря пакета, повторный seq, timeout, reset ESP/STM. Disconnect/reconnect проверен на уровне musical Reset → пустой NOTE_SET → новый набор; **физическое USB-отключение на плате пока не проверялось**. Дескрипторы проверяются с нестандартными interface/alternate/endpoint и malformed входами.

Существующая suite: **102 passed**, dependency check без конфликтов. Native MIDI suite: **PASS**. Host throughput выводится тестом отдельно и не оценивает ESP/USB/motor latency. Состояние аппаратных min/avg/max/jitter: **не измерены, нет подключённого измерительного стенда**.

Сборки 10.09.2026: STM32 ARM Compiler 6 — успешно, ROM 8296 байт, RW+ZI 3752 байта; одно предупреждение о deprecated legacy assembler исходного startup. ESP32-S3 ESP-IDF 5.4.0 — успешно, исходная сборка около 282 КБ Flash и 24 КБ статической RAM (стеки задач/heap учитываются дополнительно). Готовые файлы: `_service/build/midi-stm32/music-midi.hex` и `midi_interface/.pio/build/midi_interface/firmware.bin`; для ESP используйте PlatformIO upload, который также пишет bootloader/partitions по правильным адресам.

Для стенда включите `MIDI_DEBUG_ENABLED=1` на ESP (GPIO4) и `MIDI_DEBUG_GPIO=1` на STM (PC13). ESP переключает pin после декодирования каждого Note On; STM — один раз перед первым изменением motors в изменившемся snapshot. Heartbeat без изменений метки не даёт. Используйте общий logic analyser: каналы ESP GPIO4, STM PC13, UART TX, STEP интересующего двигателя. PC13 может быть подключён к светодиоду Blue Pill; измерять сигнал щупом, не судить по свечению.

Сначала измеряйте отдельные Note On с паузой/Note Off после прогрева, затем нагрузку шести моторов и быстрые пассажи. Сопоставляйте метки с UART sequence/notes; произвольно спаривать фронты при аккордах/voice stealing нельзя. Задержка до первого STEP дополнительно зависит от полупериода ноты; GPIO STM измеряет применение команды. USB scheduling до callback также не входит в интервал после GPIO ESP.

Экспортируйте сопоставленные пары в CSV с колонками `received_us,applied_us` из **одной временной базы**:

```powershell
python tests/midi/latency_stats.py measurements.csv
```

Скрипт вычислит samples/min/avg/max, peak-to-peak jitter=max−min и стандартное отклонение. В debugger ESP доступны `music_processing` и `receive_to_processed`: count, total_us, min_us, max_us; avg=total/count, jitter=max−min. Это локальные времена вычисления/очереди, а не сквозные. `midi_queue_overflows`, `note_set_coalesced`, `engine.overflow_count` помогают выявить перегрузку. В критическом пути нет printf.

## Известные ограничения

- USB MIDI **1.0**, первый подходящий streaming bulk IN активной конфигурации одного устройства. UMP/MIDI 2.0, несколько одновременно используемых interfaces/devices и vendor-specific USB не реализованы; standard MIDI 1.0 не привязан к Casio.
- Voice tracking — эвристика, без гарантии восстановления авторских партий; разделение пересекающихся/синхронных линий и неоднозначных аккордов может ошибаться. Конфигурацию можно подстроить по реальной игре.
- 256 source/channel/note записей, 32 tracks, 128 событий истории. Переполнение нот очищает модель и увеличивает счётчик; heap на событие отсутствует.
- Pitch bend, aftertouch, динамическая громкость двигателей и тембр программ не реализованы. Velocity влияет только на выбор нот при дефиците.
- ESP пока не получает физическую маску/диапазон STM через обратный канал. Для полноценной работы нужны шесть установленных моторов; при меньшем количестве согласуйте `Config.voices` и `MIDI_INSTALLED_MASK`.
- Потери и перегрузка восстанавливают актуальное состояние, но не воспроизводят потерянные короткие события задним числом. Watchdog обнаруживает отсутствие snapshots, не неисправность USB-инструмента, который остаётся подключённым и молчит без Note Off.
- Не измерены USB compatibility, акустический результат и аппаратная задержка; успешные unit tests/компиляция этого не заменяют.
