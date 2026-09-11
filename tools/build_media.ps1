param([string[]]$CmakeArguments = @())
$ErrorActionPreference='Stop'
$project='D:\Projects\Wall-E\WALL-E Reborn\media_s3_ai_smart'
$build='D:\Projects\Wall-E\WALL-E Reborn\media_s3_ai_smart\build'
$env:IDF_PATH='D:/esp/v5.5.1/esp-idf'
$env:IDF_PYTHON_ENV_PATH='C:/Espressif/tools/python/v5.5.1/venv'
$env:IDF_PYTHON_CHECK_CONSTRAINTS='no'
$env:IDF_TOOLS_PATH='C:/Espressif/tools'
$env:ESP_ROM_ELF_DIR='C:/Espressif/tools/esp-rom-elfs'
$env:PATH='C:/Espressif/tools/python/v5.5.1/venv/Scripts;C:/Espressif/tools/cmake/3.30.2/bin;C:/Espressif/tools/ninja/1.12.1;C:/Espressif/tools/xtensa-esp-elf/esp-14.2.0_20241119/xtensa-esp-elf/bin;C:/Program Files/Git/usr/bin;C:/Program Files/Git/mingw64/bin;'+$env:PATH
$cmake='C:/Espressif/tools/cmake/3.30.2/bin/cmake.exe'
& $cmake -G Ninja -DPYTHON_DEPS_CHECKED=1 -DPYTHON='C:\Espressif\tools\python\v5.5.1\venv\Scripts\python.exe' -DESP_PLATFORM=1 -DCCACHE_ENABLE=0 -S $project -B $build @CmakeArguments
if($LASTEXITCODE -ne 0) { throw "Media CMake configure failed: $LASTEXITCODE" }
& $cmake --build $build
if($LASTEXITCODE -ne 0) { throw "Media build failed: $LASTEXITCODE" }
