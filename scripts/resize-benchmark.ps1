# Measures how long a plug-in window takes to follow a resize. Opens the plug-in
# in the SDK's editorhost and resizes the host window step by step, as a drag
# would, forcing a repaint after each step. Prints the time per step.
#
# What it measures: the host's resize, the plug-in's onSize() and one full
# repaint. What it does not: the host's size constraint during a real mouse
# drag (WM_SIZING), and anything a DAW adds on top.
param(
    [Parameter(Mandatory)] [string] $EditorHost,
    [Parameter(Mandatory)] [string] $Plugin,
    [int] $Steps = 120,
    [int] $FromWidth = 400,
    [int] $ToWidth = 900,
    [double] $AspectRatio = 0.413   # height / width of the content
)

Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class Win {
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT rect);
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr hWnd, out RECT rect);
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr hWnd, IntPtr after,
        int x, int y, int cx, int cy, uint flags);
    [DllImport("user32.dll")] public static extern bool RedrawWindow(IntPtr hWnd, IntPtr rect,
        IntPtr region, uint flags);
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
    Start-Sleep -Milliseconds 1000

    $rect = New-Object Win+RECT
    $client = New-Object Win+RECT
    [void][Win]::GetWindowRect($handle, [ref]$rect)
    [void][Win]::GetClientRect($handle, [ref]$client)
    $frameWidth = ($rect.Right - $rect.Left) - $client.Right
    $frameHeight = ($rect.Bottom - $rect.Top) - $client.Bottom

    # RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN
    $redraw = 0x0001 -bor 0x0100 -bor 0x0080
    $times = New-Object System.Collections.Generic.List[double]
    $watch = [System.Diagnostics.Stopwatch]::new()
    foreach ($pass in 0..1) {   # out and back
        foreach ($i in 0..($Steps - 1)) {
            $t = $i / ($Steps - 1)
            if ($pass -eq 1) { $t = 1 - $t }
            $width = [int][Math]::Round($FromWidth + ($ToWidth - $FromWidth) * $t)
            $height = [int][Math]::Round($width * $AspectRatio)
            $watch.Restart()
            # SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE
            [void][Win]::SetWindowPos($handle, [IntPtr]::Zero, 0, 0,
                $width + $frameWidth, $height + $frameHeight, 0x0016)
            [void][Win]::RedrawWindow($handle, [IntPtr]::Zero, [IntPtr]::Zero, $redraw)
            $watch.Stop()
            $times.Add($watch.Elapsed.TotalMilliseconds)
        }
    }
    $sorted = $times | Sort-Object
    $median = $sorted[[int]($sorted.Count / 2)]
    $p95 = $sorted[[int]($sorted.Count * 0.95)]
    "{0} steps: median {1:N1} ms, 95th percentile {2:N1} ms, max {3:N1} ms" -f `
        $times.Count, $median, $p95, ($sorted | Select-Object -Last 1)
}
finally {
    if (-not $process.HasExited) {
        [void]$process.CloseMainWindow()
        if (-not $process.WaitForExit(3000)) { $process.Kill() }
    }
}
