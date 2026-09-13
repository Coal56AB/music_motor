# Проверка MIDI и распределения

Новый аппаратный тракт проверяется непосредственно с `firmware/Core/Src/engine.c`:

```powershell
cl /nologo /utf-8 /std:c11 /Ifirmware/Core/Inc desktop/tests/native_stm32_music_test.c /Fobuild/midi-tests/native_stm32_music_test.obj /Febuild/midi-tests/native_stm32_music_test.exe
./build/midi-tests/native_stm32_music_test.exe
$env:MUSIC_ENGINE_TEST_EXE=(Resolve-Path build/midi-tests/native_stm32_music_test.exe).Path
$env:STM32_MUSIC_TEST_EXE=$env:MUSIC_ENGINE_TEST_EXE
python -m unittest discover -s desktop/tests -v
```

Запускать из Developer PowerShell Visual Studio. Тест включает старые проверки ручного управления, выбор шести голосов, sustain, восстановление при перемотке, удержание индикации на 400 мс и погасание на 600 мс, границы пакетов, тайм-аут, повтор MIDI-пакета после потерянного ACK и приоритет ПК. Python дополнительно прогоняет длинную мелодию через реальное C-ядро с многократным пополнением очереди. `test_music_parity.py` сравнивает выбор STM32 с предварительным просмотром на тестовых композициях.

Из корня проекта (Python с зависимостями `desktop/requirements.txt`):

```powershell
python -m unittest discover -s desktop/tests -v
```

Qt-тесты работают без подключения устройств, через offscreen platform. Они
проверяют сохранение распределения при ручных правках, явный пересчёт кнопкой,
Undo, конфликты и серые несыгранные участки.

Для проверки C++-ядра и совпадения музыкального выбора с Python соберите
`native_music_test.cpp` вместе с `music.cpp` и `live_controller.cpp` любым
компилятором C++14. Например, в Developer PowerShell Visual Studio:

```powershell
New-Item -ItemType Directory -Force build/midi-tests
cl /nologo /EHsc /std:c++14 /Iesp32-diplsay-midi/DisplaySrc/esp32/music /Iesp32-diplsay-midi/DisplaySrc/esp32 desktop/tests/native_music_test.cpp esp32-diplsay-midi/DisplaySrc/esp32/music/music.cpp esp32-diplsay-midi/DisplaySrc/esp32/live_controller.cpp /Fobuild/midi-tests/ /Febuild/midi-tests/native_music_test.exe
./build/midi-tests/native_music_test.exe
$env:MUSIC_ENGINE_TEST_EXE=(Resolve-Path build/midi-tests/native_music_test.exe).Path
python -m unittest discover -s desktop/tests -v
```

Без `MUSIC_ENGINE_TEST_EXE` только проверки сравнения с C++ пропускаются.
Обычная сборка ESP32: `pio run -d esp32-diplsay-midi/Firmware/ESP32 -e display`.
Ни один из этих шагов не прошивает устройство и не запускает двигатели.

Проверки запуска и экрана (Developer PowerShell Visual Studio, из корня):

`native_display_test.c` сравнивает 100 000 обновлений перенесённого на STM32 `display_overview.h` с исторической функцией из коммита ESP `00e979e` (`legacy_overview_reference.h`). Проверяются состояния всех шести индикаторов и внутренних таймеров, включая отсутствие будущей ноты, смену нот, паузу, сон, потерю связи, живой режим и переполнение часов. Отдельно проверены граница 1999/2000 мс и отсутствие таймеров погасания на ESP: экран меняется только после STATE v2. `native_stm32_music_test.c` проверяет выдачу STATE v2 настоящим протоколом STM, разделение фактической и отображаемой ноты/частоты и остановленный STEP во время удержания. Проверка startup принимает STATE v2 и отклоняет некорректные отображаемые флаги.

```powershell
cl /nologo /utf-8 /std:c11 /Iesp32-diplsay-midi/Firmware/Api desktop/tests/native_display_test.c /Fobuild/midi-tests/ /Febuild/midi-tests/native_display_test.exe
cl /nologo /EHsc /std:c++14 /Iesp32-diplsay-midi/DisplaySrc/esp32 desktop/tests/native_startup_test.cpp esp32-diplsay-midi/DisplaySrc/esp32/controller_link.cpp /Fobuild/midi-tests/ /Febuild/midi-tests/native_startup_test.exe
./build/midi-tests/native_display_test.exe
./build/midi-tests/native_startup_test.exe
```

Они проверяют корректный ответ STM32, неверный CRC, тайм-аут и запоздалый ответ,
переполнение часов, ввод нот/частот, шаг кнопок, высоты тестовых столбцов и
перелистывание пустых/редко заполненных слотов. Рендеры экрана сохраняются в
`build/midi-tests/*.ppm`.

Проверка ручного управления STM32 без подключения платы:

```powershell
cl /nologo /utf-8 /std:c11 /Ifirmware/Core/Inc desktop/tests/native_manual_test.c firmware/Core/Src/engine.c /Fobuild/midi-tests/ /Febuild/midi-tests/native_manual_test.exe
./build/midi-tests/native_manual_test.exe
```

Проверяется работа дольше двухсекундного watchdog при исправных heartbeat экрана,
остановка при потере связи, повреждённом CRC и отзыве управления. Heartbeat экрана
не должен продлевать работу моторов, которыми владеет другой источник.

В живом тракте время группировки не добавлено: граница группы — уже полученный
USB transfer. Педальный штраф: 20 баллов плюс 80/с после отпускания, максимум
добавки 240. Удерживаемая клавиша не стареет. Шаблоны аккордов и веса линий
сохранены. Реальную задержку USB → STEP необходимо измерять на плате.

Studio сохраняет физические интервалы и контроллеры, а звучащие интервалы
вычисляет перед распределением. MIDI-экспорт исходника содержит все ноты и CC;
для сохранения порядка одновременных событий экспортируется один трек на MIDI
порт. Редакторские партии и закрепления без потерь сохраняются в `.motor.json`.
Экспорт для моторов содержит только сыгранные сегменты.

Старые проекты без контроллеров остаются читаемыми, но потерянную при старом
импорте педаль восстановить нельзя — нужно заново открыть исходный MIDI.
Ранее выбранные стратегии Studio сохраняются в пользовательских настройках;
новый режим — «Аккорды, мелодия и Sustain».

Действующие ограничения: CC66/SysEx/Pitch Bend не интерпретируются; CC124–127
выполняют освобождение нот, но не переключают маршрутизацию Omni/Mono/Poly.
Аппаратный retrigger одной высоты и регулировка громкости не добавлены.
Симулятор экрана `DisplaySrc/Host/controller_simulator.py` остаётся демонстрацией
интерфейса, не эталоном интеллектуального распределения; эталон проверяется
нативными тестами выше.
