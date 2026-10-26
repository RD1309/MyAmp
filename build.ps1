param(
    [string]$ZigPath = '',
    [string]$OutputPath = ''
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$sourcePath = Join-Path $projectRoot 'src\myamp.c'
$versionLine = Select-String -Path $sourcePath -Pattern '^#define APP_VERSION L"([^"]+)"' | Select-Object -First 1
if (-not $versionLine) { throw 'Could not read APP_VERSION from src\myamp.c.' }
$filenameVersion = $versionLine.Matches[0].Groups[1].Value.Replace('.', '_')

if ([string]::IsNullOrWhiteSpace($ZigPath)) {
    $zigCommand = Get-Command zig -ErrorAction SilentlyContinue
    if ($zigCommand) {
        $ZigPath = $zigCommand.Source
    } else {
        $defaultZig = Join-Path $env:TEMP 'myamp-build-tools\zig\zig-x86_64-windows-0.17.0\zig.exe'
        if (Test-Path -LiteralPath $defaultZig) { $ZigPath = $defaultZig }
    }
}

if ([string]::IsNullOrWhiteSpace($ZigPath) -or -not (Test-Path -LiteralPath $ZigPath)) {
    throw 'Zig was not found. Install Zig or pass -ZigPath C:\path\to\zig.exe.'
}

if ([string]::IsNullOrWhiteSpace($OutputPath)) {
    $outputPath = Join-Path $projectRoot "MyAmp$filenameVersion.exe"
} elseif ([System.IO.Path]::IsPathRooted($OutputPath)) {
    $outputPath = $OutputPath
} else {
    $outputPath = Join-Path $projectRoot $OutputPath
}
$resourcePath = Join-Path $projectRoot 'build\myamp.res'
$zigCachePath = Join-Path $projectRoot 'build\zig-cache'
New-Item -ItemType Directory -Path $zigCachePath -Force | Out-Null
$env:ZIG_GLOBAL_CACHE_DIR = $zigCachePath

Push-Location $projectRoot
try {
    & $ZigPath rc /fo $resourcePath 'src\myamp.rc'
    if ($LASTEXITCODE -ne 0) { throw "Zig resource compiler failed with exit code $LASTEXITCODE." }
    & $ZigPath cc -target x86-windows-gnu -std=c11 -O2 -s -municode `
        '-Wl,--subsystem,windows' '-Wl,--gc-sections' -Ivendor $sourcePath $resourcePath -o $outputPath `
        -luser32 -lgdi32 -lshell32 -lcomctl32 -lole32 -luuid -lwinmm -lavrt -static
    if ($LASTEXITCODE -ne 0) { throw "Zig failed with exit code $LASTEXITCODE." }
} finally {
    Pop-Location
}

Write-Host "Built $outputPath"
