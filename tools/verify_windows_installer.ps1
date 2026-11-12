# Run on a disposable Windows worker: this installs and uninstalls application shortcuts.
param(
    [Parameter(Mandatory = $true)]
    [string]$InstallerPath
)

$ErrorActionPreference = 'Stop'
$installer = (Resolve-Path $InstallerPath).Path
$scripts = @(Get-ChildItem (Split-Path $installer) -Filter '*.nsi' -Recurse -File)
if ($scripts.Count -ne 1) { throw 'Expected exactly one generated NSIS script.' }
$script = Get-Content -LiteralPath $scripts[0].FullName -Raw -Encoding UTF8
if ([regex]::Matches($script, '(?m)^\s*Page custom InstallOptionsPage SaveShortcutOptions').Count -ne 1 -or
    $script -match '!insertmacro MUI_PAGE_STARTMENU' -or
    [regex]::Matches($script, '!insertmacro MUI_LANGUAGE').Count -ne 1 -or
    $script -notmatch '!insertmacro MUI_LANGUAGE "SimpChinese"' -or
    $script -notmatch '创建桌面快捷方式' -or $script -notmatch '创建开始菜单快捷方式') {
    throw 'Generated installer must have one Chinese shortcut page and no Start Menu folder page.'
}
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

function Invoke-CheckedProcess([string]$Path, [string]$Arguments, [int]$ExpectedExitCode = 0) {
    $process = Start-Process -FilePath $Path -ArgumentList $Arguments -PassThru
    if (-not $process.WaitForExit(90000)) {
        $process.Kill($true)
        throw "Process timed out: $Path"
    }
    if ($process.ExitCode -ne $ExpectedExitCode) {
        throw "Process failed ($($process.ExitCode)): $Path"
    }
}

function Invoke-Uninstall([string]$Uninstaller, [int]$ExpectedExitCode = 0) {
    $copy = Join-Path ([System.IO.Path]::GetTempPath()) ('OpenKeys-uninstall-' + [guid]::NewGuid().ToString('N') + '.exe')
    try {
        Copy-Item -LiteralPath $Uninstaller -Destination $copy
        # _?= disables NSIS's asynchronous launcher; run a copy outside the target directory.
        Invoke-CheckedProcess $copy "/S _?=$installRoot" $ExpectedExitCode
    } finally {
        Remove-Item -LiteralPath $copy -Force -ErrorAction SilentlyContinue
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

$outsideFile = $installRoot + '-outside.txt'
Set-Content -LiteralPath $outsideFile -Value 'must survive uninstall' -Encoding UTF8
$cases = @(
    @{ Options = ''; Desktop = 1; Menu = 1 },
    @{ Options = '/DESKTOP=1 /STARTMENU=1'; Desktop = 1; Menu = 1 },
    @{ Options = '/DESKTOP=0 /STARTMENU=1'; Desktop = 0; Menu = 1 },
    @{ Options = '/DESKTOP=1 /STARTMENU=0'; Desktop = 1; Menu = 0 },
    @{ Options = '/DESKTOP=0 /STARTMENU=0'; Desktop = 0; Menu = 0 }
)
foreach ($case in $cases) {
    $originalPath = $env:PATH
    $installedLinks = @()
    $uninstaller = Join-Path $installRoot 'Uninstall.exe'
    try {
        # NSIS requires /D to be last; the remainder is the directory, without quotes.
        Invoke-CheckedProcess $installer "/S $($case.Options) /D=$installRoot"
        $executable = Join-Path $installRoot 'openkeys.exe'
        if (-not (Test-Path $executable -PathType Leaf)) { throw 'Installed application is missing.' }
        $payload = @(Get-ChildItem -LiteralPath $installRoot -Recurse -File | Select-Object -ExpandProperty FullName)
        if (-not ($payload | Where-Object { $_ -like '*.dll' }) -or
            -not (Test-Path (Join-Path $installRoot 'docs')) -or
            -not (Test-Path (Join-Path $installRoot 'licenses'))) {
            throw 'Expected installed DLLs, documentation and licenses for cleanup coverage.'
        }
        $extraDirectory = Join-Path $installRoot '用户添加\嵌套目录'
        New-Item -ItemType Directory -Path $extraDirectory -Force | Out-Null
        $extraFile = Join-Path $extraDirectory '测试曲谱.txt'
        Set-Content -LiteralPath $extraFile -Value 'must be removed' -Encoding UTF8
        $desktop = @($desktopLinks | Where-Object { Test-Path $_ })
        $startMenu = @(Find-StartMenuLinks)
        if ($desktop.Count -ne $case.Desktop) { throw "Unexpected desktop shortcut count: $($desktop.Count)." }
        if ($startMenu.Count -ne $case.Menu) { throw "Unexpected Start Menu shortcut count: $($startMenu.Count)." }
        $installedLinks = @($desktop) + @($startMenu)
        foreach ($link in $installedLinks) { Assert-Shortcut $link $executable }
        $env:PATH = "$env:SystemRoot/System32;$env:SystemRoot"
        Invoke-CheckedProcess $executable '--smoke-test'
        foreach ($link in $installedLinks) { Invoke-CheckedProcess $link '--smoke-test' }
        if ($case.Options -eq '') {
            # Exercise uninstall-before-install with the old uninstaller running outside its directory.
            Invoke-CheckedProcess $installer "/S /D=$installRoot"
            if (Test-Path -LiteralPath $extraFile) { throw 'Upgrade did not clean the old installation directory.' }
            Invoke-CheckedProcess $executable '--smoke-test'
            New-Item -ItemType Directory -Path $extraDirectory -Force | Out-Null
            Set-Content -LiteralPath $extraFile -Value 'must be removed' -Encoding UTF8
        }
    } finally {
        $env:PATH = $originalPath
        if (Test-Path $uninstaller) {
            if ($case.Options -eq '') {
                # A sharing violation must fail visibly and leave a working retry entry point.
                $lock = [System.IO.File]::Open($extraFile, 'Open', 'Read', 'Read')
                try { Invoke-Uninstall $uninstaller 1 } finally { $lock.Dispose() }
                if (-not (Test-Path -LiteralPath $extraFile) -or -not (Test-Path -LiteralPath $uninstaller)) {
                    throw 'Failed uninstall must retain locked files and a retry uninstaller.'
                }
            }
            Invoke-Uninstall $uninstaller
        }
        $remainingLinks = @($installedLinks | Where-Object { Test-Path -LiteralPath $_ })
        $remainingFiles = @($payload | Where-Object { Test-Path -LiteralPath $_ })
        if ((Test-Path -LiteralPath $installRoot) -or $remainingLinks -or $remainingFiles) {
            $remaining = @(Get-ChildItem -LiteralPath $installRoot -Force -Recurse -ErrorAction SilentlyContinue |
                Select-Object -ExpandProperty FullName)
            throw "Uninstall left installation contents or shortcuts: $($remaining + $remainingLinks -join ', ')"
        }
        if (-not (Test-Path -LiteralPath $outsideFile) -or
            (Get-Content -LiteralPath $outsideFile -Raw).Trim() -ne 'must survive uninstall') {
            throw 'Uninstall modified data outside the installation directory.'
        }
    }

Write-Host "Verified shortcut options: $($case.Options)"
}
Remove-Item -LiteralPath $outsideFile
Write-Host 'Installer verified: Unicode/spaced path, defaults and all shortcut combinations, startup, locked-file failure and complete installation-directory removal.'
