$WshShell = New-Object -ComObject WScript.Shell
$Shortcut = $WshShell.CreateShortcut("C:\Users\User\Desktop\Float.lnk")
$Shortcut.TargetPath = "C:\Users\User\.gemini\antigravity\scratch\UltraBrowser\Float.exe"
$Shortcut.WorkingDirectory = "C:\Users\User\.gemini\antigravity\scratch\UltraBrowser"
$Shortcut.IconLocation = "C:\Users\User\.gemini\antigravity\scratch\UltraBrowser\app.ico"
$Shortcut.Save()

if (Test-Path "C:\Users\User\Desktop\UltraBrowser.lnk") {
    Remove-Item "C:\Users\User\Desktop\UltraBrowser.lnk" -Force
}
Write-Output "Shortcut created successfully"
