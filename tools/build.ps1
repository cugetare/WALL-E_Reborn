param([string]$ProjectDirectory = (Split-Path $PSScriptRoot -Parent), [string[]]$IdfArguments = @('build'))
$ErrorActionPreference='Stop'
$env:IDF_PATH='D:/esp/v5.5.1/esp-idf'
$env:IDF_TOOLS_PATH='C:/Espressif'
$env:IDF_PYTHON_ENV_PATH='C:/Espressif/tools/python/v5.5.1/venv'
$env:IDF_PYTHON_CHECK_CONSTRAINTS='no'
$env:PATH='C:/Espressif/tools/python/v5.5.1/venv/Scripts;C:/Espressif/tools/cmake/3.30.2/bin;C:/Espressif/tools/ninja/1.12.1;C:/Espressif/tools/xtensa-esp-elf/esp-14.2.0_20241119/xtensa-esp-elf/bin;C:/Program Files/Git/mingw64/bin;'+$env:PATH
$env:CMAKE_MAKE_PROGRAM='C:/Espressif/tools/ninja/1.12.1/ninja.exe'
$toolchain='C:/Espressif/tools/xtensa-esp-elf/esp-14.2.0_20241119/xtensa-esp-elf/bin'
Push-Location -LiteralPath $ProjectDirectory
try {
    & 'C:/Espressif/tools/python/v5.5.1/venv/Scripts/python.exe' "$env:IDF_PATH/tools/idf.py" "-DCMAKE_MAKE_PROGRAM=$env:CMAKE_MAKE_PROGRAM" "-DCMAKE_C_COMPILER=$toolchain/xtensa-esp32s3-elf-gcc.exe" "-DCMAKE_CXX_COMPILER=$toolchain/xtensa-esp32s3-elf-g++.exe" "-DCMAKE_ASM_COMPILER=$toolchain/xtensa-esp32s3-elf-gcc.exe" @IdfArguments
    if($LASTEXITCODE -ne 0) { throw "ESP-IDF failed: $LASTEXITCODE" }
} finally { Pop-Location }
