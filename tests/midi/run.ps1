param([string]$Clang = 'C:\Program Files\clang+llvm-20.1.7-x86_64-pc-windows-msvc\bin\clang.exe')
$ErrorActionPreference='Stop'
$root=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$out=Join-Path $root '_service/build/midi'
New-Item -ItemType Directory -Force -Path $out | Out-Null
Push-Location $root
try {
    foreach($name in @('engine','protocol')) {
        & $Clang -c -O2 -g -std=c99 -Wall -Wextra -Werror -DLIVE_MIDI_MODE=1 -I firmware/Core/Inc "firmware/Core/Src/$name.c" -o "$out/$name.obj"
        if($LASTEXITCODE) {throw "C build failed: $name"}
    }
    & $Clang -c -O2 -std=c99 -Wall -Wextra -Werror -DLIVE_MIDI_MODE=1 -I firmware/Core/Inc tests/midi/host_platform.c -o "$out/platform.obj"
    if($LASTEXITCODE) {throw 'Platform build failed'}
    & $Clang -O2 -g -std=c++17 -Wall -Wextra -Werror -DLIVE_MIDI_MODE=1 -I firmware/Core/Inc -I midi_interface/music -I shared tests/midi/test_midi.cpp midi_interface/music/music.cpp "$out/engine.obj" "$out/protocol.obj" "$out/platform.obj" -o "$out/test_midi.exe"
    if($LASTEXITCODE) {throw 'C++ build failed'}
    & "$out/test_midi.exe"
    if($LASTEXITCODE) {throw 'MIDI tests failed'}
} finally {Pop-Location}
