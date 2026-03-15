# Build PeerDen installer with Inno Setup
# Run from project root: .\installer\build-installer.ps1

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$projectRoot = Split-Path -Parent $scriptDir

# Find ISCC.exe (Inno Setup compiler)
$isccPaths = @(
    "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe",
    "C:\Program Files (x86)\Inno Setup 6\ISCC.exe",
    "C:\Program Files\Inno Setup 6\ISCC.exe"
)
$iscc = $null
foreach ($p in $isccPaths) {
    if (Test-Path $p) { $iscc = $p; break }
}
if (-not $iscc) {
    Write-Error "Inno Setup not found. Install from https://jrsoftware.org/isinfo.php"
    exit 1
}

$issFile = Join-Path $scriptDir "peerdden.iss"
if (-not (Test-Path $issFile)) {
    Write-Error "Not found: $issFile"
    exit 1
}

$exePath = Join-Path $projectRoot "build\Release\peerdden.exe"
$iconPath = Join-Path $projectRoot "build\peerdden.ico"
if (-not (Test-Path $exePath)) {
    $buildDir = Join-Path $projectRoot "build"
    if (-not (Test-Path (Join-Path $buildDir "CMakeCache.txt"))) {
        Write-Error "Build not configured. Run first: cmake -B build -G `"Visual Studio 17 2022`" -A x64 -DOPENSSL_ROOT_DIR=`"C:\Program Files\OpenSSL-Win64`""
        exit 1
    }
    Write-Host "peerdden.exe not found. Building..." -ForegroundColor Yellow
    Push-Location $projectRoot
    cmake --build build --config Release
    Pop-Location
    if (-not (Test-Path $exePath)) {
        Write-Error "Build failed. Fix errors above, then run this script again."
        exit 1
    }
}
if (-not (Test-Path $iconPath)) {
    Write-Error "Installer icon not found: $iconPath. Run a full build first (cmake --build build --config Release)."
    exit 1
}

# Run from project root so paths in .iss resolve correctly
Push-Location $projectRoot
try {
    & $iscc $issFile
    if ($LASTEXITCODE -eq 0) {
        $outDir = Join-Path $scriptDir "output"
        Write-Host "Installer built: $outDir\*.exe" -ForegroundColor Green
    }
} finally {
    Pop-Location
}
