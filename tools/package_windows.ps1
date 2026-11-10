$ErrorActionPreference = "Stop"
$project = Split-Path $PSScriptRoot -Parent
$staging = Join-Path ([System.IO.Path]::GetTempPath()) ('openkeys-package-' + [guid]::NewGuid().ToString('N'))
Push-Location $project
try {
    if (-not $env:VCPKG_ROOT) { throw "Set VCPKG_ROOT to the vcpkg checkout first." }
    cmake --preset windows-release
    if ($LASTEXITCODE) { throw "CMake configuration failed." }
    cmake --build --preset windows-release --parallel 4
    if ($LASTEXITCODE) { throw "Build failed." }
    ctest --preset windows-release
    if ($LASTEXITCODE) { throw "Tests failed." }
    cmake --install build-windows-release --config Release --prefix $staging
    if ($LASTEXITCODE) { throw "Staging failed." }
    $banks = Get-ChildItem $staging -Recurse -File | Where-Object { $_.Extension -in '.sf2', '.sf3' }
    if ($banks) { throw "The application package must not contain soundfonts." }
    $process = Start-Process -FilePath (Join-Path $staging 'openkeys.exe') -ArgumentList '--smoke-test' -PassThru
    if (-not $process.WaitForExit(30000)) {
        $process.Kill($true)
        throw "Staged application timed out after 30 seconds."
    }
    if ($process.ExitCode) { throw "Staged application did not start: $($process.ExitCode)" }
    cpack --config build-windows-release/CPackConfig.cmake -C Release -B dist
    if ($LASTEXITCODE) { throw "Packaging failed. Check that NSIS is installed." }
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    Get-ChildItem dist/OpenKeysPiano-*.zip | ForEach-Object {
        $archive = [System.IO.Compression.ZipFile]::OpenRead($_.FullName)
        try {
            foreach ($entry in $archive.Entries) {
                if ([System.IO.Path]::GetExtension($entry.FullName) -in '.sf2', '.sf3') {
                    throw "The ZIP contains a soundfont: $($entry.FullName)"
                }
            }
        } finally { $archive.Dispose() }
    }
    Write-Host "Application ZIP and installer (without soundfonts): $project/dist"
} finally {
    Pop-Location
    if (Test-Path $staging) { Remove-Item $staging -Recurse -Force }
}
