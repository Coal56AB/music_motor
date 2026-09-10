param(
    [string]$Python = '',
    [switch]$Upload
)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$storage=Join-Path $root '_service/platformio-midi'
$paths=@{
    PLATFORMIO_CORE_DIR=$storage
    TEMP=(Join-Path $storage 'tmp')
    TMP=(Join-Path $storage 'tmp')
    PIP_CACHE_DIR=(Join-Path $storage 'pip-cache')
    IDF_TOOLS_PATH=(Join-Path $storage 'idf-tools')
}
$saved=@{}
try {
    foreach($key in $paths.Keys) {
        $saved[$key]=[Environment]::GetEnvironmentVariable($key,'Process')
        New-Item -ItemType Directory -Force -Path $paths[$key] | Out-Null
        [Environment]::SetEnvironmentVariable($key,$paths[$key],'Process')
    }
    if(-not $Python) {
        $venv=Join-Path $storage 'core'
        $Python=Join-Path $venv 'Scripts/python.exe'
        if(-not (Test-Path -LiteralPath $Python)) {
            $bootstrap="$env:USERPROFILE\.platformio\penv\Scripts\python.exe"
            if(-not (Test-Path -LiteralPath $bootstrap)) {$bootstrap='python'}
            & $bootstrap -m venv $venv
            if($LASTEXITCODE) {throw 'Python environment creation failed'}
        }
        & $Python -c 'import importlib.util, sys; sys.exit(0 if importlib.util.find_spec("platformio") else 1)'
        if($LASTEXITCODE) {
            & $Python -m pip install --disable-pip-version-check 'platformio==6.1.18'
            if($LASTEXITCODE) {throw 'PlatformIO environment installation failed'}
        }
    }
    $pioArgs=@('-m','platformio','run','-d',$PSScriptRoot)
    if($Upload) {$pioArgs+=@('-t','upload')}
    & $Python @pioArgs
    if($LASTEXITCODE) {throw 'midi_interface build failed'}
} finally {
    foreach($key in $saved.Keys) {[Environment]::SetEnvironmentVariable($key,$saved[$key],'Process')}
}
