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
idf.py build
idf.py -p COM4 -b 115200 flash

# Auto-update releases
$ErrorActionPreference = 'SilentlyContinue'
$ver = (Select-String -Path "main\gateway.h" -Pattern 'FW_VERSION\s+"([^"]+)"').Matches[0].Groups[1].Value
if ($ver) {
    $bin = "build\fieldtunnel-proto.bin"
    $dest = "..\..\releases\fieldtunnel-$ver.bin"
    Copy-Item $bin $dest -Force
    $json = "{`n  `"version`": `"$ver`",`n  `"releaseDate`": `"$(Get-Date -Format 'yyyy-MM-dd')`",`n  `"notes`": `"Latest release`",`n  `"minVersion`": `"0.2.0`",`n  `"url`": `"https://fieldtunnel.com/releases/fieldtunnel-$ver.bin`"`n}"
    $json | Out-File "..\..\releases\latest.json" -Encoding UTF8 -NoNewline
    Write-Host "Auto-released v$ver to releases/"
}
