# Shared native-window capture. Dot-source from Windows automation scripts.
function Initialize-Numpad9WindowCapture {
    if ('Numpad9WindowCapture' -as [type]) { return }
Add-Type -AssemblyName System.Drawing
$drawingReferences = @([Drawing.Bitmap].Assembly.Location, [Drawing.Size].Assembly.Location)
# PowerShell/.NET 9+ forwards drawing interfaces to these assemblies.
$drawingReferences += @(Get-ChildItem (Join-Path (Split-Path ([Drawing.Bitmap].Assembly.Location) -Parent) 'System.Private.Windows.*.dll') | ForEach-Object { $_.FullName })
Add-Type -ReferencedAssemblies $drawingReferences @'
using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;
public static class Numpad9WindowCapture {
  [DllImport("kernel32.dll")] private static extern void Sleep(uint milliseconds);
  [StructLayout(LayoutKind.Sequential)] private struct Rect { public int Left, Top, Right, Bottom; }
  [DllImport("user32.dll", SetLastError=true)] private static extern IntPtr SetThreadDpiAwarenessContext(IntPtr context);
  [DllImport("user32.dll", SetLastError=true)] private static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int width, int height, uint flags);
  [DllImport("user32.dll", SetLastError=true)] private static extern bool GetWindowRect(IntPtr h, out Rect rect);
  [DllImport("user32.dll", SetLastError=true)] private static extern bool PrintWindow(IntPtr h, IntPtr dc, uint flags);
  [DllImport("user32.dll")] private static extern bool RedrawWindow(IntPtr h, IntPtr rect, IntPtr region, uint flags);
  [DllImport("user32.dll")] private static extern IntPtr SendMessage(IntPtr h, uint message, IntPtr wp, IntPtr lp);
  private delegate bool EnumWindowsCallback(IntPtr window, IntPtr data);
  [DllImport("user32.dll")] private static extern bool EnumWindows(EnumWindowsCallback callback, IntPtr data);
  [DllImport("user32.dll")] private static extern bool IsWindowVisible(IntPtr window);
  [DllImport("user32.dll")] private static extern uint GetWindowThreadProcessId(IntPtr window, out uint processId);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] private static extern int GetClassName(IntPtr window, System.Text.StringBuilder text, int length);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] private static extern int GetWindowText(IntPtr window, System.Text.StringBuilder text, int length);
  public static string ClassName(IntPtr window) {
    System.Text.StringBuilder text = new System.Text.StringBuilder(256);
    GetClassName(window, text, text.Capacity); return text.ToString();
  }
  public static string Caption(IntPtr window) {
    System.Text.StringBuilder text = new System.Text.StringBuilder(1024);
    GetWindowText(window, text, text.Capacity); return text.ToString();
  }
  public static void AssertMainWindow(IntPtr window, int processId, string expectedClass) {
    uint owner; GetWindowThreadProcessId(window, out owner);
    if (owner != (uint)processId || !IsWindowVisible(window) || ClassName(window) != expectedClass)
      throw new InvalidOperationException("The expected visible native window does not belong to the launched process.");
  }
  public static void AssertNoDialogs(int processId) {
    string dialog = null;
    EnumWindows((window, data) => {
      uint owner; GetWindowThreadProcessId(window, out owner);
      if (owner == (uint)processId && IsWindowVisible(window) && ClassName(window) == "#32770")
        dialog = Caption(window);
      return true;
    }, IntPtr.Zero);
    if (dialog != null) throw new InvalidOperationException("Unexpected startup dialog: " + dialog);
  }
  public static int[] Capture(IntPtr window, string path, bool toggleLanguage) {
    // Keep resize, measurement and rendering on this one DPI-aware thread.
    IntPtr previous = SetThreadDpiAwarenessContext(new IntPtr(-4)); // PER_MONITOR_AWARE_V2
    if (previous == IntPtr.Zero) throw new InvalidOperationException("Window API failed with Win32 error " + Marshal.GetLastWin32Error());
    try {
      if (!SetWindowPos(window, IntPtr.Zero, 0, 0, 1366, 900, 0x0040))
throw new InvalidOperationException("Window API failed with Win32 error " + Marshal.GetLastWin32Error());
      if (toggleLanguage) SendMessage(window, 0x0111, new IntPtr(106), IntPtr.Zero); // Actual app language control.
      RedrawWindow(window, IntPtr.Zero, IntPtr.Zero, 0x0581);
      Sleep(500);
      Rect rect;
      if (!GetWindowRect(window, out rect)) throw new InvalidOperationException("Window API failed with Win32 error " + Marshal.GetLastWin32Error());
      int width = rect.Right - rect.Left, height = rect.Bottom - rect.Top;
      if (width != 1366 || height != 900)
throw new InvalidOperationException("Requested physical window 1366x900, observed " + width + "x" + height +
  " at rectangle (" + rect.Left + "," + rect.Top + ")-(" + rect.Right + "," + rect.Bottom + ").");
      using (Bitmap bitmap = new Bitmap(width, height)) {
using (Graphics graphics = Graphics.FromImage(bitmap)) {
  IntPtr dc = graphics.GetHdc();
  try {
    if (!PrintWindow(window, dc, 2)) throw new InvalidOperationException("Window API failed with Win32 error " + Marshal.GetLastWin32Error());
  } finally { graphics.ReleaseHdc(dc); }
}
int first = bitmap.GetPixel(width / 2, height / 2).ToArgb();
bool different = false;
for (int x = 25; x < width; x += 35)
  for (int y = 25; y < height; y += 25)
    if (bitmap.GetPixel(x, y).ToArgb() != first) different = true;
if (!different) throw new InvalidOperationException("Captured window appears blank.");
bitmap.Save(path, ImageFormat.Png);
      }
      return new int[] { width, height };
    } finally { SetThreadDpiAwarenessContext(previous); }
  }
}
'@
}

function Save-Numpad9WindowScreenshot {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][IntPtr]$Window,
        [Parameter(Mandatory)][string]$Path,
        [switch]$ToggleLanguage
    )
    Initialize-Numpad9WindowCapture
    $fullPath = [IO.Path]::GetFullPath($Path)
    New-Item -ItemType Directory -Force -Path (Split-Path $fullPath -Parent) | Out-Null
    $dimensions = [Numpad9WindowCapture]::Capture($Window, $fullPath, [bool]$ToggleLanguage)
    [pscustomobject]@{ path = $fullPath; width = $dimensions[0]; height = $dimensions[1]; sha256 = (Get-FileHash -LiteralPath $fullPath -Algorithm SHA256).Hash }
}
