# Simula uma combinação de teclas global (Ctrl + Alt + <tecla>) via keybd_event
param([string]$Key = 'R')
Add-Type -Namespace W -Name K -MemberDefinition '[DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, System.UIntPtr extra);'
$vk = [byte][char]$Key.ToUpper()
$CTRL = 0x11; $ALT = 0x12; $UP = 2
[W.K]::keybd_event($CTRL, 0, 0, [UIntPtr]::Zero)
[W.K]::keybd_event($ALT, 0, 0, [UIntPtr]::Zero)
[W.K]::keybd_event($vk, 0, 0, [UIntPtr]::Zero)
Start-Sleep -Milliseconds 120
[W.K]::keybd_event($vk, 0, $UP, [UIntPtr]::Zero)
[W.K]::keybd_event($ALT, 0, $UP, [UIntPtr]::Zero)
[W.K]::keybd_event($CTRL, 0, $UP, [UIntPtr]::Zero)
