# Exercise the real NSIS finish page on a disposable Windows worker.
param(
    [Parameter(Mandatory = $true)]
    [string]$InstallerPath
)
$ErrorActionPreference = 'Stop'
$installer = (Resolve-Path $InstallerPath).Path
$installRoot = Join-Path $env:TEMP ('OpenKeys 完成页 ' + [guid]::NewGuid().ToString('N'))

Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;
public static class OpenKeysWizard {
    private delegate bool EnumProc(IntPtr window, IntPtr data);
    [DllImport("user32.dll")] private static extern bool EnumWindows(EnumProc callback, IntPtr data);
    [DllImport("user32.dll")] private static extern bool EnumChildWindows(IntPtr parent, EnumProc callback, IntPtr data);
    [DllImport("user32.dll")] private static extern uint GetWindowThreadProcessId(IntPtr window, out uint pid);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] private static extern int GetWindowText(IntPtr window, StringBuilder text, int count);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr window);
    [DllImport("user32.dll")] public static extern bool IsWindowEnabled(IntPtr window);
    [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr window, int id);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern IntPtr SendMessage(IntPtr window, uint message, IntPtr wparam, IntPtr lparam);
    public static string Text(IntPtr window) {
        var text = new StringBuilder(1024);
        GetWindowText(window, text, text.Capacity);
        return text.ToString();
    }
    public static IntPtr Window(int process) {
        IntPtr result = IntPtr.Zero;
        EnumWindows((window, data) => {
            uint pid;
            GetWindowThreadProcessId(window, out pid);
            if (pid == process && IsWindowVisible(window) && Text(window).Contains("OpenKeys")) {
                result = window;
                return false;
            }
            return true;
        }, IntPtr.Zero);
        return result;
    }
    public static IntPtr Child(IntPtr parent, string text) {
        IntPtr result = IntPtr.Zero;
        EnumChildWindows(parent, (window, data) => {
            if (IsWindowVisible(window) && Text(window) == text) {
                result = window;
                return false;
            }
            return true;
        }, IntPtr.Zero);
        return result;
    }
    public static void Click(IntPtr button) {
        SendMessage(button, 0x00f5, IntPtr.Zero, IntPtr.Zero); // BM_CLICK
    }
    public static int Checked(IntPtr button) {
        return SendMessage(button, 0x00f0, IntPtr.Zero, IntPtr.Zero).ToInt32(); // BM_GETCHECK
    }
}
'@

function Find-InstalledProcess {
    @(Get-Process -Name openkeys -ErrorAction SilentlyContinue |
        Where-Object { $_.Path -eq (Join-Path $installRoot 'openkeys.exe') })
}

foreach ($language in @(
    @{ Id = 1033; Label = 'Run OpenKeys Piano now' },
    @{ Id = 2052; Label = '立即运行 OpenKeys Piano' }
)) {
    foreach ($launch in @($true, $false)) {
        $setup = $null
        try {
            # /D must be last and unquoted, even when it contains spaces.
            $setup = Start-Process $installer -ArgumentList "/LANG=$($language.Id) /DESKTOP=0 /STARTMENU=0 /D=$installRoot" -PassThru
            $deadline = [DateTime]::UtcNow.AddSeconds(90)
            $finished = $false
            while ([DateTime]::UtcNow -lt $deadline) {
                $setup.Refresh()
                if ($setup.HasExited) { throw 'Installer exited before the finish page.' }
                $window = [OpenKeysWizard]::Window($setup.Id)
                if ($window -ne [IntPtr]::Zero) {
                    $run = [OpenKeysWizard]::Child($window, $language.Label)
                    $next = [OpenKeysWizard]::GetDlgItem($window, 1)
                    if ($run -ne [IntPtr]::Zero) {
                        if ([OpenKeysWizard]::Checked($run) -ne 1) { throw 'Run-now checkbox must default to checked.' }
                        if (-not $launch) {
                            [OpenKeysWizard]::Click($run)
                            if ([OpenKeysWizard]::Checked($run) -ne 0) { throw 'Could not uncheck run-now.' }
                        }
                        [OpenKeysWizard]::Click($next)
                        $finished = $true
                        break
                    }
                    if ($next -ne [IntPtr]::Zero -and [OpenKeysWizard]::IsWindowEnabled($next)) {
                        [OpenKeysWizard]::Click($next)
                    }
                }
                Start-Sleep -Milliseconds 300
            }
            if (-not $finished) { throw 'Timed out reaching the installer finish page.' }
            if (-not $setup.WaitForExit(30000) -or $setup.ExitCode -ne 0) { throw 'Interactive installation did not finish successfully.' }
            $deadline = [DateTime]::UtcNow.AddSeconds($(if ($launch) { 30 } else { 3 }))
            $started = @()
            do {
                $started = @(Find-InstalledProcess)
                if ($started.Count) { break }
                Start-Sleep -Milliseconds 100
            } while ([DateTime]::UtcNow -lt $deadline)
            if ($launch -and $started.Count -ne 1) { throw 'Checked finish page did not launch the installed application.' }
            if (-not $launch -and $started.Count) { throw 'Unchecked finish page launched the application.' }
            if ($launch) {
                if (-not $started[0].WaitForInputIdle(30000)) { throw 'Launched application did not become ready.' }
                $started[0].Refresh()
                if ($started[0].HasExited -or $started[0].MainWindowHandle -eq [IntPtr]::Zero) {
                    throw 'Launched application has no main window.'
                }
            }
            Write-Host "Verified finish page: language $($language.Id), launch=$launch"
        } finally {
            foreach ($process in @(Find-InstalledProcess)) {
                $process.Kill($true)
                $process.WaitForExit()
            }
            if ($setup -and -not $setup.HasExited) {
                $setup.Kill($true)
                $setup.WaitForExit()
            }
            $uninstaller = Join-Path $installRoot 'Uninstall.exe'
            if (Test-Path -LiteralPath $uninstaller) {
                $copy = $installRoot + '-uninstall.exe'
                try {
                    Copy-Item -LiteralPath $uninstaller -Destination $copy
                    $remove = Start-Process $copy -ArgumentList "/S _?=$installRoot" -PassThru
                    if (-not $remove.WaitForExit(30000) -or $remove.ExitCode -ne 0) {
                        throw 'Finish-page test uninstall failed.'
                    }
                } finally {
                    Remove-Item -LiteralPath $copy -Force -ErrorAction SilentlyContinue
                }
            }
            if (Test-Path -LiteralPath $installRoot) { throw 'Finish-page test left an installation directory.' }
        }
    }
}
