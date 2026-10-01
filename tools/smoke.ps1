# Launch the real window, capture screenshots, simulate a key press (dev check).
param(
    [string]$Exe = ".\build\wheel.exe",
    [string]$OutDir = ".\build"
)
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class WinApi {
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT r);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern void keybd_event(byte bVk, byte bScan, uint dwFlags, UIntPtr dwExtraInfo);
}
"@

function Grab([IntPtr]$h, [string]$path) {
    $r = New-Object WinApi+RECT
    [void][WinApi]::GetWindowRect($h, [ref]$r)
    $w = $r.Right - $r.Left
    $hh = $r.Bottom - $r.Top
    if ($w -le 0 -or $hh -le 0) { return "no window rect" }
    $bmp = New-Object System.Drawing.Bitmap $w, $hh
    $gfx = [System.Drawing.Graphics]::FromImage($bmp)
    $gfx.CopyFromScreen($r.Left, $r.Top, 0, 0, (New-Object System.Drawing.Size $w, $hh))
    $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
    $gfx.Dispose()
    $bmp.Dispose()
    return "saved $path ($w x $hh)"
}

$p = Start-Process -FilePath $Exe -PassThru
Start-Sleep -Milliseconds 2600
$p.Refresh()
$h = $p.MainWindowHandle
if ($h -eq [IntPtr]::Zero) { "no window created"; $p | Stop-Process -Force; exit 1 }
[void][WinApi]::SetForegroundWindow($h)
Start-Sleep -Milliseconds 500
Grab $h (Join-Path $OutDir "live-idle.png")

[WinApi]::keybd_event(0x20, 0, 0, [UIntPtr]::Zero)
Start-Sleep -Milliseconds 80
[WinApi]::keybd_event(0x20, 0, 2, [UIntPtr]::Zero)
Start-Sleep -Milliseconds 1000
Grab $h (Join-Path $OutDir "live-spin.png")
Start-Sleep -Milliseconds 4300
Grab $h (Join-Path $OutDir "live-result.png")

[void]$p.CloseMainWindow()
Start-Sleep -Milliseconds 900
if (!$p.HasExited) { $p | Stop-Process -Force }
"done"
