# End-to-end check of the inline editors (name / weight) in the real window.
param(
    [string]$Exe = ".\build\wheel.exe",
    [string]$OutDir = ".\build"
)
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class W2 {
    [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
    [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr hWnd, ref POINT p);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT r);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern void mouse_event(uint f, uint dx, uint dy, uint d, UIntPtr e);
    [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte sc, uint f, UIntPtr e);
}
"@

$script:hwnd = [IntPtr]::Zero

function ClientToScreenPt([int]$x, [int]$y) {
    $pt = New-Object W2+POINT
    $pt.X = $x; $pt.Y = $y
    [void][W2]::ClientToScreen($script:hwnd, [ref]$pt)
    return $pt
}
function Click([int]$x, [int]$y) {
    $pt = ClientToScreenPt $x $y
    [void][W2]::SetCursorPos($pt.X, $pt.Y)
    Start-Sleep -Milliseconds 120
    [W2]::mouse_event(0x0002, 0, 0, 0, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 60
    [W2]::mouse_event(0x0004, 0, 0, 0, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 200
}
function Key([int]$vk) {
    [W2]::keybd_event([byte]$vk, 0, 0, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 40
    [W2]::keybd_event([byte]$vk, 0, 2, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 60
}
function SelAll() {
    [W2]::keybd_event(0x11, 0, 0, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 40
    Key 0x41
    [W2]::keybd_event(0x11, 0, 2, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 80
}
function Grab([string]$path) {
    $r = New-Object W2+RECT
    [void][W2]::GetWindowRect($script:hwnd, [ref]$r)
    $w = $r.Right - $r.Left; $hh = $r.Bottom - $r.Top
    $bmp = New-Object System.Drawing.Bitmap $w, $hh
    $gfx = [System.Drawing.Graphics]::FromImage($bmp)
    $gfx.CopyFromScreen($r.Left, $r.Top, 0, 0, (New-Object System.Drawing.Size $w, $hh))
    $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
    $gfx.Dispose(); $bmp.Dispose()
}

$p = Start-Process -FilePath $Exe -PassThru
Start-Sleep -Milliseconds 2600
$p.Refresh()
$script:hwnd = $p.MainWindowHandle
if ($script:hwnd -eq [IntPtr]::Zero) { "no window"; $p | Stop-Process -Force; exit 1 }
[void][W2]::SetForegroundWindow($script:hwnd)
Start-Sleep -Milliseconds 400

# Row 0 name cell -> replace with "Pizza"
Click 964 140
Start-Sleep -Milliseconds 300
Grab (Join-Path $OutDir "edit-open.png")
SelAll
foreach ($vk in 0x50, 0x49, 0x5A, 0x5A, 0x41) { Key $vk }   # P I Z Z A
Key 0x0D
Start-Sleep -Milliseconds 500
Grab (Join-Path $OutDir "edit-name.png")

# Row 1 weight cell -> set to 5
Click 1093 190
Start-Sleep -Milliseconds 300
SelAll
Key 0x35
Key 0x0D
Start-Sleep -Milliseconds 700
Grab (Join-Path $OutDir "edit-weight.png")

[void]$p.CloseMainWindow()
Start-Sleep -Milliseconds 800
if (!$p.HasExited) { $p | Stop-Process -Force }
"done"
