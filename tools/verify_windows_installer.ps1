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

# WScript.Shell reads TargetPath through an ANSI interface, losing Chinese characters
# on an English Windows worker. Read the installed link through IShellLinkW instead.
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
using System.Runtime.InteropServices.ComTypes;
using System.Text;

public static class OpenKeysShortcutReader
{
    [ComImport, Guid("00021401-0000-0000-C000-000000000046")]
    private class ShellLink { }

    // Only the first IShellLinkW method is needed; keep its native vtable position.
    [ComImport, Guid("000214F9-0000-0000-C000-000000000046")]
    [InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    private interface IShellLinkW
    {
        void GetPath([Out, MarshalAs(UnmanagedType.LPWStr)] StringBuilder path,
                     int capacity, IntPtr findData, uint flags);
    }

    public static string ReadTarget(string linkPath)
    {
        object link = new ShellLink();
        try
        {
            ((IPersistFile)link).Load(linkPath, 0);
            var target = new StringBuilder(260);
            ((IShellLinkW)link).GetPath(target, target.Capacity, IntPtr.Zero, 0);
            return target.ToString();
        }
        finally
        {
            Marshal.FinalReleaseComObject(link);
        }
    }
}
'@

function Assert-Shortcut([string]$Path, [string]$Target) {
    $actualTarget = [OpenKeysShortcutReader]::ReadTarget($Path)
    if ([System.IO.Path]::GetFullPath($actualTarget) -ne [System.IO.Path]::GetFullPath($Target)) {
        throw "Invalid shortcut target: $Path -> $actualTarget (expected $Target)"
    }
    if (-not (Test-Path -LiteralPath $actualTarget -PathType Leaf)) {
        throw "Shortcut target is missing: $Path -> $actualTarget"
    }
    Write-Host "Verified shortcut: $Path -> $actualTarget"
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
    foreach ($link in $installedLinks) { Invoke-CheckedProcess $link '--smoke-test' }
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
