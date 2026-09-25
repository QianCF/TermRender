<#
    TermRender - Windows build (MinGW-w64 g++)

        .\build.ps1              # release build
        .\build.ps1 -Debug       # debug build (no optimisation, symbols)

    Produces out\termrender.exe
#>
param(
    [switch]$Debug
)

$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
if (-not $root) { $root = (Get-Location).Path }
$src = Join-Path $root "src"
$deps = Join-Path $root "deps"
$out = Join-Path $root "out"

New-Item -ItemType Directory -Force -Path $out | Out-Null

$cxx = "g++"
if (-not (Get-Command $cxx -ErrorAction SilentlyContinue)) {
    Write-Error "g++ not found in PATH. Install MinGW-w64 and try again."
}

$files = Get-ChildItem -Path $src -Filter *.cpp | ForEach-Object { $_.FullName }

$common = @("-std=c++17", "-Wall", "-Wextra", "-Wno-unused-parameter",
            "-I$src", "-I$deps")
$releaseFlags = @("-O2", "-DNDEBUG")
$debugFlags = @("-O0", "-g")

$flags = if ($Debug) { $debugFlags } else { $releaseFlags }
$exe = Join-Path $out "termrender.exe"

Write-Host "==> compiling TermRender (MinGW g++)"
& $cxx @common @flags @files "-o" $exe "-static" "-static-libgcc" "-static-libstdc++" "-pthread"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "==> built: $exe"
