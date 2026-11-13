# True source recompilation of Condemned 2 (Windows x64 Release).
# Run in a PRIVATE GitHub Actions repository containing the user's CI input ZIP.
# The output intentionally NEVER contains the proprietary XEX or XEXP.
[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$SourceRoot,
    [Parameter(Mandatory=$true)][string]$InputZip,
    [Parameter(Mandatory=$true)][string]$OutputDir
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

$SourceRoot = [IO.Path]::GetFullPath($SourceRoot)
$InputZip = [IO.Path]::GetFullPath($InputZip)
$OutputDir = [IO.Path]::GetFullPath($OutputDir)
$expectedZip = '53b86df7ac64a57982fd2de68bab106d69c0f2cc3382fd9b40ac9fc0f1c6c0ae'
$expectedXex = '554b2ae2c3abfe6f33d6b7c7616e89c725c8008cff675f481a6df918110310fd'
$expectedXexp = 'fef2cc32be1fe738b0fee1dea7a579cf1f0515745a3466bda593dbad733ae4ef'
$officialSha = 'd013f0d3cae6d02898a93e2ba3bb0f75c66e67fbbc3347750ae201036b3ac296'

function Assert-Hash([string]$Path, [string]$Expected) {
    if (!(Test-Path -LiteralPath $Path -PathType Leaf)) { throw "Missing required file: $Path" }
    $actual = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actual -ne $Expected) { throw "SHA256 mismatch: $Path" }
}

function Exec([string]$Program, [string[]]$Arguments) {
    Write-Host "Running $Program"
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { throw "Tool exited with code $LASTEXITCODE : $Program" }
}

Write-Host '=== Upstream psxrestore 2026-11-13 input regression checks ==='
Exec 'python' @((Join-Path $SourceRoot 'ci/test_upstream_input_contract.py'))

Write-Host '=== Private inputs integrity check ==='
Assert-Hash $InputZip $expectedZip
New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null
$tempInput = Join-Path $env:RUNNER_TEMP 'cnd2-source-input'
if (Test-Path $tempInput) { Remove-Item $tempInput -Recurse -Force }
New-Item -ItemType Directory -Path $tempInput -Force | Out-Null
Expand-Archive -LiteralPath $InputZip -DestinationPath $tempInput
$inputXex = Join-Path $tempInput 'default.xex'
$inputXexp = Join-Path $tempInput 'default.xexp'
Assert-Hash $inputXex $expectedXex
Assert-Hash $inputXexp $expectedXexp

# 'Assets' (UI art) is already present in the source tree; Windows resolves
# manifest 'assets/default.xex' to the same case-insensitive directory.
$assetDir = Join-Path $SourceRoot 'assets'
New-Item -ItemType Directory -Path $assetDir -Force | Out-Null
Copy-Item -LiteralPath $inputXex -Destination (Join-Path $assetDir 'default.xex') -Force
Copy-Item -LiteralPath $inputXexp -Destination (Join-Path $assetDir 'default.xexp') -Force
if (!(Test-Path (Join-Path $SourceRoot 'thirdparty/rexglue-sdk/CMakeLists.txt'))) {
    throw 'ReXGlue SDK submodule is missing. Checkout must use submodules: recursive.'
}
if (!(Test-Path (Join-Path $SourceRoot 'condemned2recomp_manifest.toml'))) {
    throw 'The public source checkout was not found.'
}

Write-Host '=== Verify Win64 clang toolchain ==='
# Visual Studio developer setup prepends its own LLVM 19 to PATH. Use the
# runner-wide LLVM 20 explicitly, regardless of the MSVC toolchain PATH.
$clangC = 'C:/Program Files/LLVM/bin/clang.exe'
$clangCxx = 'C:/Program Files/LLVM/bin/clang++.exe'
if (!(Test-Path -LiteralPath $clangC -PathType Leaf)) { throw "Missing pinned Clang C compiler: $clangC" }
if (!(Test-Path -LiteralPath $clangCxx -PathType Leaf)) { throw "Missing pinned Clang C++ compiler: $clangCxx" }
$versionLines = @(& $clangCxx --version)
$clangVersion = $versionLines[0]
Write-Host $clangVersion
if ($clangVersion -match 'version\s+(\d+)\.') {
    $clangMajor = [int]$Matches[1]
} else {
    throw "Cannot establish the installed Clang version: $clangVersion"
}
if ($clangMajor -lt 20) { throw 'Requires LLVM/Clang 20 or later' }

$buildDir = Join-Path $SourceRoot 'out/build/win-amd64-release'
$sdkDir = Join-Path $SourceRoot 'thirdparty/rexglue-sdk'
$oldLocation = Get-Location
try {
    Set-Location $SourceRoot
    $configuration = @(
        '--preset', 'win-amd64-release',
        "-DREXSDK_DIR=$sdkDir",
        '-DREXGLUE_RECOMP_DEBUG_INFO=none',
        "-DCMAKE_C_COMPILER=$clangC",
        "-DCMAKE_CXX_COMPILER=$clangCxx"
    )
    # Configure SDK, host project, codegen target.
    Exec 'cmake' $configuration
    # Actual recompilation from default.xex + default.xexp + Title Update TOMLs.
    Exec 'cmake' @('--build','--preset','win-amd64-release','--target','condemned2recomp_codegen','--parallel','2')
    if (!(Test-Path 'generated/default/sources.cmake')) {
        throw 'Codegen completed without generated/default/sources.cmake'
    }
    # CMake loads the newly generated sources.cmake and DLL targets.
    Exec 'cmake' $configuration
    Exec 'cmake' @('--build','--preset','win-amd64-release','--target','condemned2recomp','--parallel','2')
}
finally {
    Set-Location $oldLocation
}

Write-Host '=== Package newly COMPILED EXE (never patch an official EXE) ==='
$newExe = Join-Path $buildDir 'condemned2recomp.exe'
if (!(Test-Path $newExe)) { throw 'Real source-build condemned2recomp.exe missing' }
$publicRelease = Join-Path $env:RUNNER_TEMP 'cnd2-public-060.zip'
$releaseUrl = 'https://github.com/psxrestore/Condemned2Recomp/releases/download/0.6.0/condemned2recompV0.6.0_windows.zip'
Invoke-WebRequest -Uri $releaseUrl -OutFile $publicRelease
Assert-Hash $publicRelease $officialSha

$stage = Join-Path $env:RUNNER_TEMP 'cnd2-true-release'
if (Test-Path $stage) { Remove-Item $stage -Force -Recurse }
Expand-Archive -LiteralPath $publicRelease -DestinationPath $stage
Copy-Item -LiteralPath $newExe -Destination (Join-Path $stage 'condemned2recomp.exe') -Force
if ((Get-Item $newExe).Length -lt 100000) { throw 'Compiled executable is unexpectedly small' }

# Linker-produced runtime binaries take precedence over official 0.6.0 DLLs.
# A compilation succeeds only if its corresponding current SDK runtime exists.
foreach($dll in @('rexruntime.dll', 'rexgpu-xenos.dll')) {
    $candidates = @(Get-ChildItem -LiteralPath $buildDir -Recurse -File -Filter $dll -ErrorAction SilentlyContinue)
    if ($candidates.Count -eq 0) { throw "Newly compiled $dll was not found under build output" }
    $found = $candidates | Sort-Object LastWriteTimeUtc -Descending | Select-Object -First 1
    Copy-Item -LiteralPath $found.FullName -Destination (Join-Path $stage $dll) -Force
    Write-Host "Bundled freshly compiled $dll"
}

# Never ship the user's game files in public artifacts.
$prohibited = @(Get-ChildItem $stage -Recurse -File | Where-Object {
    $_.Extension -in @('.xex','.xexp','.iso') -or $_.Name -eq 'Condemned2_Private_CI_Inputs.zip'
})
if ($prohibited.Count -gt 0) { throw 'Package would contain proprietary game inputs' }

$revision = (& git -C $SourceRoot rev-parse --short=12 HEAD | Select-Object -First 1)
$buildInfo = @"
Condemned 2 Recomp Windows x64 REAL SOURCE BUILD
Public source: AD010/Condemned2Recomp
Branch: fix/input-weapon-pickup-qte-060
Commit: $revision
Base upstream: psxrestore/Condemned2Recomp 6411f7e541a613579e27fc127e2b9d7656123a58 (2026-11-13).
Mouse sensitivity midhook 0x82306EF4/f1: exact upstream 6411f7e.
Native input: upstream 9df903a plus AD010 fixes for complete E/R KeyUp,
forensic wheel release, quick-tool close, LT+RT grab, and RMB CancelTask.
Single-player spectator HUD protection: AD010 integration.
Windows compilation, private inputs, runtime DLLs, and ZIP: AD010 CI.
Compiler: $clangVersion
Generated from user-provided original XEX and Title Update XEXP.
Packaged with public Condemned2Recomp 0.6.0 support files.
Game input files are not included.
Experimental build. User verified health-pack pickup in build 27/29
but NOT melee weapon pickup, all context prompts, or the doll-factory
finisher QTE. Changes made after build 29 are not yet game-tested.
Windows loader smoke test proves process loading, not gameplay correctness.
"@
Set-Content -LiteralPath (Join-Path $stage 'TRUE_SOURCE_BUILD.txt') -Value $buildInfo -Encoding utf8
$zipOut = Join-Path $OutputDir 'Condemned2Recomp-RealSource-InputFix-Windows-x64.zip'
if (Test-Path $zipOut) { Remove-Item $zipOut -Force }
Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $zipOut -CompressionLevel Optimal
Add-Type -AssemblyName System.IO.Compression.FileSystem
$check = [IO.Compression.ZipFile]::OpenRead($zipOut)
try {
    $exeEntries = @($check.Entries | Where-Object { $_.FullName -eq 'condemned2recomp.exe' })
    if ($exeEntries.Count -ne 1) {
        throw 'Compiled EXE not present exactly once in ZIP'
    }
    $privateEntries = @($check.Entries | Where-Object {
        $_.FullName -match '(?i)\.(xex|xexp|iso)$'
    })
    if ($privateEntries.Count -ne 0) {
        throw 'Private game inputs detected in artifact'
    }
}
finally {
    $check.Dispose()
}
Write-Host '=== SUCCESS: true-source Windows x64 build archived ==='
Write-Host ((Get-FileHash $zipOut -Algorithm SHA256).Hash)
Write-Host $zipOut
