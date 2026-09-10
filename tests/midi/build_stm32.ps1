param([string]$Tools = 'E:\Keil_v5\ARM\ARMCLANG\bin')
$ErrorActionPreference='Stop'
$root=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$out=Join-Path $root '_service/build/midi-stm32'
New-Item -ItemType Directory -Force -Path $out | Out-Null
Push-Location $root
try {
    $includes=@('-Ifirmware/Core/Inc','-Ifirmware/Drivers/STM32F1xx_HAL_Driver/Inc','-Ifirmware/Drivers/CMSIS/Device/ST/STM32F1xx/Include','-Ifirmware/Drivers/CMSIS/Include')
    $files=@(Get-ChildItem firmware/Core/Src -Filter '*.c') + @(Get-ChildItem firmware/Drivers/STM32F1xx_HAL_Driver/Src -Filter '*.c' | Where-Object Name -NotLike '*template*')
    $objects=@()
    foreach($file in $files) {
        $obj=Join-Path $out ($file.BaseName+'.o')
        & "$Tools/armclang.exe" --target=arm-arm-none-eabi -mcpu=cortex-m3 -mthumb -std=c99 -Oz -g -DUSE_HAL_DRIVER -DSTM32F103xB -DLIVE_MIDI_MODE=1 -DMIDI_DEBUG_GPIO=1 @includes -c $file.FullName -o $obj
        if($LASTEXITCODE) {throw "ARM build failed: $file"}
        $objects+=$obj
    }
    & "$Tools/armasm.exe" --cpu Cortex-M3 --apcs=interwork -o "$out/startup.o" firmware/MDK-ARM/startup_stm32f103xb.s
    if($LASTEXITCODE) {throw 'Startup assembly failed'}
    & "$Tools/armlink.exe" @objects "$out/startup.o" --scatter tests/midi/stm32.sct --entry Reset_Handler --map --info sizes --list "$out/music-midi.map" -o "$out/music-midi.axf"
    if($LASTEXITCODE) {throw 'ARM link failed'}
    & "$Tools/fromelf.exe" --i32combined --output "$out/music-midi.hex" "$out/music-midi.axf"
    if($LASTEXITCODE) {throw 'HEX generation failed'}
    Write-Output "STM32 MIDI image: $out/music-midi.hex"
} finally {Pop-Location}
