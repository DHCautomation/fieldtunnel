Remove-Item env:MSYSTEM -ErrorAction SilentlyContinue
Remove-Item env:MINGW_PREFIX -ErrorAction SilentlyContinue
Remove-Item env:MSYSTEM_PREFIX -ErrorAction SilentlyContinue
Remove-Item env:MSYSTEM_CHOST -ErrorAction SilentlyContinue
Remove-Item env:MSYSTEM_CARCH -ErrorAction SilentlyContinue
Remove-Item env:MINGW_CHOST -ErrorAction SilentlyContinue
Remove-Item env:MINGW_PACKAGE_PREFIX -ErrorAction SilentlyContinue

$env:IDF_PATH = "C:\Users\admin\esp\v5.5.1\esp-idf"
$env:IDF_TOOLS_PATH = "C:\Users\admin\.espressif"
$env:IDF_PYTHON_ENV_PATH = "C:\Users\admin\.espressif\python_env\idf5.5_py3.11_env"

& "$env:IDF_PATH\export.ps1" 2>$null
Set-Location "C:\dev\fieldtunnel\firmware\proto-atom"
# Full clean needed — partition table changed for OTA
idf.py fullclean
idf.py set-target esp32
idf.py build
idf.py -p COM4 -b 115200 flash
