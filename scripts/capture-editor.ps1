# Opens a plug-in in the SDK's editorhost, captures only that window with
# PrintWindow (nothing else on the screen), saves a PNG and closes the host.
#
# With -ClientWidth and -ClientHeight the host window is first resized so that
# its content area has that size in pixels, the way a user would drag it. The
# script does not apply the plug-in's size constraint, so any shape can be tried.
#
# -Click x,y clicks the plug-in window at that point of its content area
# (repeatable, after any resize). -Hold x,y presses the mouse there, captures
# while it is held, then releases it.
param(
    [Parameter(Mandatory)] [string] $EditorHost,
    [Parameter(Mandatory)] [string] $Plugin,
    [Parameter(Mandatory)] [string] $Output,
    [int] $ClientWidth = 0,
    [int] $ClientHeight = 0,
    [string[]] $Click = @(),
    [string] $Hold = ""
)

Add-Type -AssemblyName System.Drawing
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class Win {
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT rect);
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr hWnd, out RECT rect);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr hWnd, IntPtr hdc, uint flags);
    [DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr hWnd, IntPtr after,
        int x, int y, int cx, int cy, uint flags);
    [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }
    [DllImport("user32.dll")] public static extern IntPtr ChildWindowFromPointEx(IntPtr parent,
        POINT point, uint flags);
    [DllImport("user32.dll")] public static extern bool MapWindowPoints(IntPtr from, IntPtr to,
        ref POINT point, uint count);
    [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr hWnd, uint msg,
        IntPtr wParam, IntPtr lParam);
    // Sends a mouse message to the deepest child window under a point of `top`'s client area.
    public static void Mouse(IntPtr top, int x, int y, uint msg, int keys) {
        IntPtr target = top;
        POINT p = new POINT { X = x, Y = y };
        while (true) {
            IntPtr child = ChildWindowFromPointEx(target, p, 1); // CWP_SKIPINVISIBLE
            if (child == IntPtr.Zero || child == target) break;
            MapWindowPoints(target, child, ref p, 1);
            target = child;
        }
        SendMessage(target, msg, (IntPtr)keys, (IntPtr)((p.Y << 16) | (p.X & 0xFFFF)));
    }
}
'@

$process = Start-Process -FilePath $EditorHost -ArgumentList "`"$Plugin`"" -PassThru
try {
    $handle = [IntPtr]::Zero
    foreach ($i in 1..50) {
        Start-Sleep -Milliseconds 200
        $process.Refresh()
        if ($process.HasExited) { throw "editorhost exited with code $($process.ExitCode)" }
        if ($process.MainWindowHandle -ne [IntPtr]::Zero) { $handle = $process.MainWindowHandle; break }
    }
    if ($handle -eq [IntPtr]::Zero) { throw "editorhost showed no window" }
    Start-Sleep -Milliseconds 1000   # let the editor draw

    $rect = New-Object Win+RECT
    $client = New-Object Win+RECT
    if ($ClientWidth -gt 0 -and $ClientHeight -gt 0) {
        [void][Win]::GetWindowRect($handle, [ref]$rect)
        [void][Win]::GetClientRect($handle, [ref]$client)
        $frameWidth = ($rect.Right - $rect.Left) - $client.Right
        $frameHeight = ($rect.Bottom - $rect.Top) - $client.Bottom
        # SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE
        [void][Win]::SetWindowPos($handle, [IntPtr]::Zero, 0, 0,
            $ClientWidth + $frameWidth, $ClientHeight + $frameHeight, 0x0016)
        Start-Sleep -Milliseconds 800
    }

    foreach ($point in $Click) {
        $x, $y = $point.Split(',') | ForEach-Object { [int]$_ }
        [Win]::Mouse($handle, $x, $y, 0x0201, 1)   # WM_LBUTTONDOWN, MK_LBUTTON
        Start-Sleep -Milliseconds 60
        [Win]::Mouse($handle, $x, $y, 0x0202, 0)   # WM_LBUTTONUP
        Start-Sleep -Milliseconds 600
    }
    if ($Hold) {
        $holdX, $holdY = $Hold.Split(',') | ForEach-Object { [int]$_ }
        [Win]::Mouse($handle, $holdX, $holdY, 0x0201, 1)
        Start-Sleep -Milliseconds 400
    }

    [void][Win]::GetWindowRect($handle, [ref]$rect)
    [void][Win]::GetClientRect($handle, [ref]$client)
    $width = $rect.Right - $rect.Left
    $height = $rect.Bottom - $rect.Top
    $bitmap = New-Object System.Drawing.Bitmap $width, $height
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $hdc = $graphics.GetHdc()
    $ok = [Win]::PrintWindow($handle, $hdc, 2)   # PW_RENDERFULLCONTENT
    $graphics.ReleaseHdc($hdc)
    $graphics.Dispose()
    $bitmap.Save($Output, [System.Drawing.Imaging.ImageFormat]::Png)
    $bitmap.Dispose()
    if ($Hold) { [Win]::Mouse($handle, $holdX, $holdY, 0x0202, 0) }
    "window ${width}x${height}, content $($client.Right)x$($client.Bottom), PrintWindow=$ok, dpi=$([Win]::GetDpiForWindow($handle))"
}
finally {
    if (-not $process.HasExited) {
        [void]$process.CloseMainWindow()
        if (-not $process.WaitForExit(3000)) { $process.Kill() }
    }
}
