param([switch]$Verbose, [switch]$Clean)

$ErrorActionPreference = "Stop"
Write-Host "=== Shader Compiler ===" -ForegroundColor Cyan

$glslc = $null
if ($env:VULKAN_SDK) { $glslc = Join-Path $env:VULKAN_SDK "Bin\glslc.exe" }
if (-not $glslc -or -not (Test-Path $glslc)) {
    $glslc = Get-Command glslc -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Source
}
if (-not $glslc) {
    Write-Host "ERROR: glslc not found! Install Vulkan SDK." -ForegroundColor Red
    exit 1
}

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$shaderDir = Join-Path (Split-Path -Parent $scriptDir) "shaders"
$outputDir = Join-Path $shaderDir "bin"

if ($Clean -and (Test-Path $outputDir)) { Remove-Item -Path $outputDir -Recurse -Force }
if (-not (Test-Path $outputDir)) { New-Item -ItemType Directory -Path $outputDir | Out-Null }

$extensions = @("*.vert", "*.frag", "*.comp", "*.geom")
$compiled = 0

foreach ($ext in $extensions) {
    Get-ChildItem -Path $shaderDir -Filter $ext -File | ForEach-Object {
        Write-Host "  Compiling: $($_.Name)" -NoNewline
        $out = Join-Path $outputDir "$($_.Name).spv"
        & $glslc $_.FullName -o $out 2>&1
        if ($LASTEXITCODE -eq 0) {
            Write-Host " [OK]" -ForegroundColor Green
            $compiled++
        } else {
            Write-Host " [FAILED]" -ForegroundColor Red
        }
    }
}

Write-Host "`nCompiled $compiled shader(s)" -ForegroundColor Cyan
