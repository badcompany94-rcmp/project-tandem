param([Parameter(Mandatory=$true)][string]$Evidence)
$ErrorActionPreference='Stop'
$source=Split-Path -Parent $PSScriptRoot
. (Join-Path $source 'distribution/Tandem-Paths.ps1')
. (Join-Path $source 'distribution/Updater-Core.ps1')
$checks=0
function Require([bool]$Value,[string]$Why){$script:checks++;if(-not $Value){throw $Why}}
function Reject([scriptblock]$Action,[string]$Why){$failed=$false;try{& $Action|Out-Null}catch{$failed=$true};Require $failed $Why}
function Text([string]$File,[string]$Value){New-Item -ItemType Directory -Force -Path (Split-Path -Parent $File)|Out-Null;[IO.File]::WriteAllText($File,$Value)}
$root=Join-Path $Evidence 'install with spaces'
New-Item -ItemType Directory -Force -Path $root|Out-Null
Require (Test-TandemPathEqual (Get-TandemDataRoot $root) (Join-Path $root 'local-data')) 'default data stays portable'
New-Item -ItemType Directory -Path (Join-Path $root 'paths.json')|Out-Null
Reject {Get-TandemDataRoot $root} 'paths.json directory must not silently select default saves'
Remove-Item -LiteralPath (Join-Path $root 'paths.json')
Text (Join-Path $root 'paths.json') '{"data":"user data"}'
$data=Get-TandemDataRoot $root
Require (Test-TandemPathEqual $data (Join-Path $root 'user data')) 'relative path resolves against installation'
Require (Test-TandemPathEqual (Get-TandemBuildRoot $root) (Join-Path $root 'local-data/build-portable')) 'build stays inside installation'
Text (Join-Path $root 'portable.txt') ''
Text (Join-Path $root 'paths.json') 'not JSON'
Require (Test-TandemPathEqual (Get-TandemDataRoot $root) (Join-Path $root 'local-data')) 'portable sentinel wins over malformed paths'
Remove-Item -LiteralPath (Join-Path $root 'portable.txt')
Reject {Get-TandemDataRoot $root} 'malformed JSON fails without silent fallback'
foreach($value in @($root,[IO.Path]::GetPathRoot($root),(Split-Path -Parent $root),'source/data','tools/data','docs/data','local-data/build-portable','%TANDEM_UNDEFINED_PATH%/data')){
    Text (Join-Path $root 'paths.json') (@{data=$value}|ConvertTo-Json)
    Reject {Get-TandemDataRoot $root} 'unsafe/reserved/undefined path rejected'
}
$env:TANDEM_DATA_PATH_TEST=Join-Path $Evidence 'absolute data'
Text (Join-Path $root 'paths.json') '{"data":"%TANDEM_DATA_PATH_TEST%"}'
Require (Test-TandemPathEqual (Get-TandemDataRoot $root) $env:TANDEM_DATA_PATH_TEST) 'environment and absolute path supported'
$data=Initialize-TandemDataRoot $root
Require (Test-Path -LiteralPath (Join-Path $data '.tandem-owner.json')) 'custom data owner recorded'
$second=Join-Path $Evidence 'second install'
Text (Join-Path $second 'paths.json') (@{data=$data}|ConvertTo-Json)
Reject {Initialize-TandemDataRoot $second} 'independent installs cannot silently share user data'
Text (Join-Path $data 'settings.json') '{"player_name":"Preserve me"}'
Text (Join-Path $root 'local-data/game/saves/marker.txt') 'legacy progress'
Require (Test-Path -LiteralPath (Join-Path $root 'local-data/game/saves/marker.txt')) 'opt-in does not remove old saves'
Require ((Get-TandemSettings $root).player_name -eq 'Preserve me') 'settings read from custom folder'
Require (Test-TandemPathEqual (Get-TandemPrivateStatePath $root 'settings.json' $data) (Join-Path $data 'settings.json')) 'updater explicitly bounds custom settings backup'
Require (Test-TandemPathEqual (Get-TandemPrivateStatePath $root 'update-pending.json' $data) (Join-Path $root 'local-data/update-pending.json')) 'update marker stays in control root'
Reject {Get-TandemPrivateStatePath $root 'settings.json' (Join-Path $Evidence 'forged')} 'journal cannot redirect private restore elsewhere'
Reject {Get-TandemPrivateStatePath $root '../save.raw' $data} 'private restore accepts only named state files'
Reject {Assert-TandemSafePath $root (Join-Path $data 'settings.json')} 'update package path guard still rejects external writes'
Reject {Get-TandemPackagePath 'paths.json'} 'package cannot replace data-location preferences'
Reject {Get-TandemPackagePath 'portable.txt'} 'package cannot replace portable sentinel'
$link=Join-Path $root 'linked-data';New-Item -ItemType Junction -Path $link -Target $data|Out-Null
Text (Join-Path $root 'paths.json') '{"data":"linked-data"}'
Reject {Get-TandemDataRoot $root} 'linked custom folders rejected'
Text (Join-Path $root 'paths.json') '{"data":"blocked-file"}';Text (Join-Path $root 'blocked-file') 'not a directory'
Reject {Initialize-TandemDataRoot $root} 'unwritable destination reports an error without fallback'
Text (Join-Path $root 'paths.json') '{"data":"relative data"}'
$data=Initialize-TandemDataRoot $root
Text (Join-Path $data 'game/saves/marker.txt') 'custom progress'
$moved=Join-Path $Evidence 'renamed installation'
$resolvedSource=[IO.Path]::GetFullPath($root);$resolvedTarget=[IO.Path]::GetFullPath($moved)
if(-not $resolvedSource.StartsWith([IO.Path]::GetFullPath($Evidence)+'\') -or -not $resolvedTarget.StartsWith([IO.Path]::GetFullPath($Evidence)+'\')){throw 'Unsafe fixture move'}
Move-Item -LiteralPath $resolvedSource -Destination $resolvedTarget
$after=Initialize-TandemDataRoot $moved
Require ((Get-Content -LiteralPath (Join-Path $after 'game/saves/marker.txt') -Raw).Trim() -eq 'custom progress') 'relative data follows moved installation'
Require (Test-TandemPathEqual (Get-TandemBuildRoot $moved) (Join-Path $moved 'local-data/build-portable')) 'build resolver follows move separately'
@{passed=$true;checks=$checks;data=$after}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $Evidence 'result.json') -Encoding UTF8
"PASS $checks portable/default/custom/environment/ownership/move and updater-boundary checks"
