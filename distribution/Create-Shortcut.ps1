param([Parameter(Mandatory=$true)][string]$Root,
      [string]$ShortcutPath)
$ErrorActionPreference='Stop'
$rootPath=[IO.Path]::GetFullPath($Root)
if(-not $ShortcutPath){$ShortcutPath=Join-Path $rootPath 'Project Tandem.lnk'}
$launcher=Join-Path $rootPath 'Launch-Tandem.cmd'
$icon=Join-Path $rootPath 'source\distribution\assets\project-tandem.ico'
if(-not (Test-Path -LiteralPath $launcher -PathType Leaf) -or -not (Test-Path -LiteralPath $icon -PathType Leaf)){throw 'The Tandem launcher or community icon is missing.'}
$shell=New-Object -ComObject WScript.Shell
$shortcut=$shell.CreateShortcut([IO.Path]::GetFullPath($ShortcutPath))
$shortcut.TargetPath=$launcher
$shortcut.WorkingDirectory=$rootPath
$shortcut.IconLocation=$icon+',0'
$shortcut.Description='Project Tandem - community artwork by POOTERMAN (deviantart.com/pooterman)'
$shortcut.Save()
