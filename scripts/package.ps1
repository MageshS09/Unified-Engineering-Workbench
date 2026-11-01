<#
  Packaging only: builds Release, runs the tests, COPIES files into dist/<Id>_Workbench and zips them.
  It implements no application feature.

  Run (no admin needed):
    powershell -ExecutionPolicy Bypass -File scripts/package.ps1
    powershell -ExecutionPolicy Bypass -File scripts/package.ps1 -CandidateId ABC123

  The toolchain folder (MSYS2 UCRT64 bin: g++, cmake, ninja) is taken from -ToolchainBin, then from the
  WB_TOOLCHAIN_BIN environment variable, then from PATH. No machine path is stored in this repository.
#>
param(
    [string]$CandidateId = 'CandidateID',
    [string]$ToolchainBin = $env:WB_TOOLCHAIN_BIN,
    [switch]$SkipBuild
)

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if ($ToolchainBin) { $env:PATH = "$ToolchainBin;$env:PATH" }

function Invoke-Step([string]$What, [scriptblock]$Run) {
    Write-Host "==> $What"
    & $Run
    if ($LASTEXITCODE -ne 0) { throw "$What failed (exit code $LASTEXITCODE)" }
}

if (-not $SkipBuild) {
    Invoke-Step 'Configure (Release, Ninja)' { cmake -S $root -B (Join-Path $root 'build') -G Ninja -DCMAKE_BUILD_TYPE=Release }
    Invoke-Step 'Build' { cmake --build (Join-Path $root 'build') }
    Invoke-Step 'Run tests' { ctest --test-dir (Join-Path $root 'build') --output-on-failure }
}

$name = "${CandidateId}_Workbench"
$stage = Join-Path $root "dist/$name"
$zip = Join-Path $root "dist/$name.zip"
if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
if (Test-Path $zip) { Remove-Item -Force $zip }
New-Item -ItemType Directory -Path $stage | Out-Null

Write-Host "==> Copy files into $stage"
Copy-Item (Join-Path $root 'build/Launch.exe') $stage
Copy-Item (Join-Path $root 'build/workbench_cli.exe') $stage
foreach ($f in 'README.md', 'CMakeLists.txt', 'CLAUDE.md', '.gitignore') {
    if (Test-Path (Join-Path $root $f)) { Copy-Item (Join-Path $root $f) $stage }
}
# Folders: app (web UI), data, results, docs, src, tests, third_party, scripts, .vscode.
$skipDirs = @('build', 'dist', 'logs', '.git', 'node_modules')
foreach ($d in 'app', 'data', 'results', 'docs', 'src', 'tests', 'third_party', 'scripts', '.vscode') {
    $from = Join-Path $root $d
    if (-not (Test-Path $from)) { continue }
    robocopy $from (Join-Path $stage $d) /E /XD $skipDirs /XF '*.tmp' '*.zip' /NFL /NDL /NJH /NJS /NP | Out-Null
    if ($LASTEXITCODE -ge 8) { throw "robocopy failed for $d (exit code $LASTEXITCODE)" }
}
$global:LASTEXITCODE = 0
# In the ZIP, Launch.exe is at the folder root, so the editor task uses a root-relative path.
Copy-Item (Join-Path $root 'scripts/tasks.packaged.json') (Join-Path $stage '.vscode/tasks.json') -Force

# Version-control evidence (only if git is available and this is a repository).
if ((Get-Command git -ErrorAction SilentlyContinue) -and (Test-Path (Join-Path $root '.git'))) {
    Push-Location $root
    git bundle create (Join-Path $stage 'repo.bundle') --all 2>$null
    git log --date=short --pretty=format:'%h %ad %s' | Set-Content -Encoding utf8 (Join-Path $stage 'git_log.txt')
    Pop-Location
}

Write-Host "==> Create $zip"
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
# Entries are written one by one so their names use '/' (ZipFile.CreateFromDirectory on Windows
# PowerShell 5.1 writes '\', which some unzip tools reject).
$archive = [System.IO.Compression.ZipFile]::Open($zip, [System.IO.Compression.ZipArchiveMode]::Create)
try {
    $stageFull = (Resolve-Path $stage).Path.TrimEnd('\')
    Get-ChildItem $stage -Recurse -File -Force | ForEach-Object {
        $relative = $_.FullName.Substring($stageFull.Length + 1).Replace('\', '/')
        [void][System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile(
            $archive, $_.FullName, "$name/$relative", [System.IO.Compression.CompressionLevel]::Optimal)
    }
} finally {
    $archive.Dispose()
}

$mb = [math]::Round((Get-Item $zip).Length / 1MB, 1)
Write-Host "Done: $zip ($mb MB)"
