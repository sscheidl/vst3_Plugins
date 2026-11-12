# Opens a plug-in in the SDK's editorhost, captures only that window with
# PrintWindow (nothing else on the screen), saves a PNG and closes the host.
param(
    [Parameter(Mandatory)] [string] $EditorHost,
    [Parameter(Mandatory)] [string] $Plugin,
    [Parameter(Mandatory)] [string] $Output
)

Add-Type -AssemblyName System.Drawing
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class Win {
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT rect);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr hWnd, IntPtr hdc, uint flags);
    [DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr hWnd);
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
    Start-Sleep -Milliseconds 1200   # let the editor draw

    $rect = New-Object Win+RECT
    [void][Win]::GetWindowRect($handle, [ref]$rect)
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
    "captured ${width}x${height}, PrintWindow=$ok, dpi=$([Win]::GetDpiForWindow($handle)), title='$($process.MainWindowTitle)'"
}
finally {
    if (-not $process.HasExited) {
        [void]$process.CloseMainWindow()
        if (-not $process.WaitForExit(3000)) { $process.Kill() }
    }
}
