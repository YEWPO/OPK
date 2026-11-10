# Run on a disposable Windows worker: this installs and uninstalls application shortcuts.
param(
    [Parameter(Mandatory = $true)]
    [string]$InstallerPath
)

$ErrorActionPreference = 'Stop'
$installer = (Resolve-Path $InstallerPath).Path
$installRoot = Join-Path ([System.IO.Path]::GetTempPath()) ('OpenKeys 安装 test ' + [guid]::NewGuid().ToString('N'))
$desktopRoots = @(
    [Environment]::GetFolderPath('DesktopDirectory'),
    [Environment]::GetFolderPath('CommonDesktopDirectory')
) | Where-Object { $_ } | Select-Object -Unique
$programRoots = @(
    [Environment]::GetFolderPath('Programs'),
    [Environment]::GetFolderPath('CommonPrograms')
) | Where-Object { $_ } | Select-Object -Unique
$desktopLinks = @($desktopRoots | ForEach-Object { Join-Path $_ 'OpenKeys Piano.lnk' })

function Find-StartMenuLinks {
    @($programRoots | ForEach-Object {
        if (Test-Path $_) {
            Get-ChildItem $_ -Filter 'OpenKeys Piano.lnk' -File -Recurse | Select-Object -ExpandProperty FullName
        }
    })
}

if (($desktopLinks | Where-Object { Test-Path $_ }) -or (Find-StartMenuLinks)) {
    throw 'Use a clean worker: an OpenKeys Piano shortcut already exists.'
}

function Invoke-CheckedProcess([string]$Path, [string]$Arguments) {
    $process = Start-Process -FilePath $Path -ArgumentList $Arguments -PassThru
    if (-not $process.WaitForExit(90000)) {
        $process.Kill($true)
        throw "Process timed out: $Path"
    }
    if ($process.ExitCode -ne 0) {
        throw "Process failed ($($process.ExitCode)): $Path"
    }
}

function Assert-Shortcut([string]$Path, [string]$Target) {
    $shell = New-Object -ComObject WScript.Shell
    try {
        $shortcut = $shell.CreateShortcut($Path)
        if ([System.IO.Path]::GetFullPath($shortcut.TargetPath) -ne [System.IO.Path]::GetFullPath($Target)) {
            throw "Invalid shortcut target: $Path -> $($shortcut.TargetPath)"
        }
        if (-not (Test-Path $shortcut.TargetPath -PathType Leaf)) {
            throw "Shortcut target is missing: $Path"
        }
    } finally {
        if ($shortcut) { [void][System.Runtime.InteropServices.Marshal]::ReleaseComObject($shortcut) }
        [void][System.Runtime.InteropServices.Marshal]::ReleaseComObject($shell)
    }
}

$originalPath = $env:PATH
$installedLinks = @()
$uninstaller = Join-Path $installRoot 'Uninstall.exe'
try {
    # NSIS requires /D to be last; the remainder is the directory, without quotes.
    Invoke-CheckedProcess $installer "/S /D=$installRoot"
    $executable = Join-Path $installRoot 'openkeys.exe'
    if (-not (Test-Path $executable -PathType Leaf)) { throw 'Installed application is missing.' }
    $desktop = @($desktopLinks | Where-Object { Test-Path $_ })
    $startMenu = @(Find-StartMenuLinks)
    if ($desktop.Count -ne 1) { throw "Expected one desktop shortcut, found $($desktop.Count)." }
    if ($startMenu.Count -ne 1) { throw "Expected one Start Menu shortcut, found $($startMenu.Count)." }
    $installedLinks = @($desktop) + @($startMenu)
    foreach ($link in $installedLinks) { Assert-Shortcut $link $executable }
    $env:PATH = "$env:SystemRoot/System32;$env:SystemRoot"
    Invoke-CheckedProcess $executable '--smoke-test'
} finally {
    $env:PATH = $originalPath
    if (Test-Path $uninstaller) {
        Invoke-CheckedProcess $uninstaller '/S'
        # NSIS launches its temporary uninstaller; the original process may exit first.
        for ($attempt = 0; $attempt -lt 120; ++$attempt) {
            $remainingLinks = @($installedLinks | Where-Object { Test-Path $_ })
            if (-not (Test-Path $uninstaller) -and -not $remainingLinks) { break }
            Start-Sleep -Milliseconds 500
        }
        if ((Test-Path $uninstaller) -or (Test-Path (Join-Path $installRoot 'openkeys.exe')) -or
            ($installedLinks | Where-Object { Test-Path $_ })) {
            throw 'Uninstall did not remove the application and its shortcuts.'
        }
    }
}
Write-Host 'Installer verified: Unicode/spaced path, default desktop/Start Menu links, startup and uninstall.'
