param([Parameter(Mandatory=$true)][string]$Evidence)
$ErrorActionPreference='Stop'
$source=Split-Path -Parent $PSScriptRoot
. (Join-Path $source 'distribution/Installer-Core.ps1')
Add-Type -AssemblyName System.IO.Compression.FileSystem
[IO.Directory]::CreateDirectory($Evidence)|Out-Null
$script:checks=0
function Require([bool]$Value,[string]$Message){$script:checks++;if(-not $Value){throw $Message}}
function Reject([scriptblock]$Action,[string]$Message){$failed=$false;try{& $Action|Out-Null}catch{$failed=$true};Require $failed $Message}
function Text([string]$Path,[string]$Value){[IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($Path))|Out-Null;[IO.File]::WriteAllText($Path,$Value)}
$bootstrap=Join-Path $Evidence 'download/ProjectTandem.exe';Text $bootstrap 'bootstrap fixture'
Reject {Initialize-TandemInstall ([IO.Path]::GetPathRoot($Evidence)) $bootstrap} 'drive root rejected'
$occupied=Join-Path $Evidence 'occupied';Text (Join-Path $occupied 'personal.txt') 'preserve'
Reject {Initialize-TandemInstall $occupied $bootstrap} 'nonempty folder rejected'
Require ([IO.File]::ReadAllText((Join-Path $occupied 'personal.txt')) -eq 'preserve') 'nonempty folder untouched'
$root=Join-Path $Evidence "fresh spaces % and ' quote"
$lock=Initialize-TandemInstall $root $bootstrap
try{Reject {Initialize-TandemInstall $root $bootstrap} 'duplicate install blocked'}finally{$lock.Dispose()}
$lock=Initialize-TandemInstall $root $bootstrap;$lock.Dispose()
Require ((Read-TandemJson (Join-Path $root '.tandem-install.json')).phase -eq 'Downloading') 'interrupted installation can resume'
$link=Join-Path $Evidence 'linked';New-Item -ItemType Junction -Path $link -Target $occupied|Out-Null
Reject {Initialize-TandemInstall $link $bootstrap} 'junction installation rejected'
$retained=Join-Path $Evidence 'retained saves'
$card=Join-Path $retained 'local-data/game/saves/memcard_a.raw';Text $card 'existing progress fixture'
$controls=Join-Path $retained 'local-data/game/saves/controllers.cfg';Text $controls 'existing controls fixture'
Write-TandemJson (Join-Path $retained 'local-data/settings.json') @{player_name='Returning player'}
$retainedHashes=@{};foreach($file in @(Get-ChildItem -LiteralPath (Join-Path $retained 'local-data') -Recurse -File)){$retainedHashes[$file.FullName]=Get-TandemHash $file.FullName}
Require (Test-TandemPathEqual (Get-TandemInstallSaveFolder $retained) (Split-Path -Parent $card)) 'retained default save path recognized'
$lock=Initialize-TandemInstall $retained $bootstrap;$lock.Dispose()
Require ((Read-TandemJson (Join-Path $retained '.tandem-install.json')).phase -eq 'Downloading') 'save-only old installation admitted'
$remainingGame=Join-Path $Evidence 'working game';Text (Join-Path $remainingGame 'local-data/game/saves/memcard_a.raw') 'progress'
Text (Join-Path $remainingGame 'Tandem-Launcher.ps1') 'working launcher'
Reject {Initialize-TandemInstall $remainingGame $bootstrap} 'working installation must use normal launcher'
Require (-not (Test-Path -LiteralPath (Join-Path $remainingGame '.tandem-install.json'))) 'rejected working installation not claimed'
$linkedData=Join-Path $Evidence 'linked retained';[IO.Directory]::CreateDirectory($linkedData)|Out-Null
New-Item -ItemType Junction -Path (Join-Path $linkedData 'local-data') -Target (Join-Path $retained 'local-data')|Out-Null
Reject {Initialize-TandemInstall $linkedData $bootstrap} 'linked retained data rejected'
$linkedSaves=Join-Path $Evidence 'linked saves';[IO.Directory]::CreateDirectory((Join-Path $linkedSaves 'local-data/game'))|Out-Null
New-Item -ItemType Junction -Path (Join-Path $linkedSaves 'local-data/game/saves') -Target (Split-Path -Parent $card)|Out-Null
Reject {Initialize-TandemInstall $linkedSaves $bootstrap} 'linked save folder rejected'
$pending=Join-Path $Evidence 'retained unfinished update';Text (Join-Path $pending 'local-data/update-transaction.json') '{}'
Reject {Initialize-TandemInstall $pending $bootstrap} 'unfinished update requires recovery before reinstall'
Require (-not (Test-Path -LiteralPath (Join-Path $pending '.tandem-install.json'))) 'unfinished update not claimed'
$custom=Join-Path $Evidence 'retained custom';$customData=Join-Path $Evidence 'external data'
Write-TandemJson (Join-Path $custom 'paths.json') @{data=$customData}
$null=Initialize-TandemDataRoot $custom;Text (Join-Path $customData 'game/saves/memcard_a.raw') 'custom progress'
$ownerHash=Get-TandemHash (Join-Path $customData '.tandem-owner.json')
$lock=Initialize-TandemInstall $custom $bootstrap;$lock.Dispose()
Require (Test-TandemPathEqual (Get-TandemInstallSaveFolder $custom) (Join-Path $customData 'game/saves')) 'retained custom save path recognized'
Require (Test-TandemPathEqual (Initialize-TandemDataRoot $custom) $customData) 'original external data owner retained'
Require ((Get-TandemHash (Join-Path $customData '.tandem-owner.json')) -eq $ownerHash) 'external ownership bytes unchanged'
$otherOwner=Join-Path $Evidence 'wrong custom owner';Write-TandemJson (Join-Path $otherOwner 'paths.json') @{data=$customData}
Reject {Initialize-TandemDataRoot $otherOwner} 'existing external data is never silently adopted by another installation'
function Release([int]$Beta,[bool]$Draft=$false){return [pscustomobject]@{tag_name=('v0.1.0-beta.'+$Beta);name='Fixture';draft=$Draft;published_at='2026-11-03';prerelease=$true;assets=@(
    [pscustomobject]@{name=('Project-Tandem-Windows-Public-Beta-'+$Beta+'.zip');state='uploaded';size=100;browser_download_url=('https://github.com/badcompany94-rcmp/project-tandem/releases/download/v0.1.0-beta.'+$Beta+'/full.zip')})}}
function Get-TandemReleaseJson([string]$Url){return @((Release 9),(Release 11 $true),(Release 10))}
Require ((Get-TandemInstallRelease).Tag -eq 'v0.1.0-beta.10') 'fresh installs choose highest published numeric release'
function Get-TandemReleaseJson([string]$Url){return @()}
Reject {Get-TandemInstallRelease} 'no usable release explained'
$stage=Join-Path $Evidence 'package'
foreach($file in @('Setup-Tandem.ps1','Launch-Tandem.ps1','Tandem-Launcher.ps1','source/tools/build_identity.py','tools/fixture.txt')){Text (Join-Path $stage $file) ('fixture '+$file)}
$files=@(Get-ChildItem -LiteralPath $stage -Recurse -File|ForEach-Object {@{path=$_.FullName.Substring($stage.Length+1).Replace('\','/');sha256=(Get-TandemHash $_.FullName)}})
$manifest=@{public_beta=$true;includes_game_data=$false;release='Public Beta 13';release_tag='v0.1.0-beta.13';compatibility='0123456789abcdef';files=$files}
Write-TandemJson (Join-Path $stage 'package-manifest.json') $manifest
$script:archive=Join-Path $Evidence 'package.zip';[IO.Compression.ZipFile]::CreateFromDirectory($stage,$script:archive)
function Save-TandemAsset($Asset,[string]$Folder,[scriptblock]$Progress){return $script:archive}
$candidate=[pscustomobject]@{Tag='v0.1.0-beta.13';Full=[pscustomobject]@{name='full.zip'}}
# Inject a copy failure after a real first file, then retry into the owned folder.
$script:originalCopy=(Get-Command Copy-TandemAtomic).ScriptBlock;$script:copied=0
function Copy-TandemAtomic([string]$From,[string]$To){if(++$script:copied -eq 2){throw 'simulated interruption'};& $script:originalCopy $From $To}
Reject {Install-TandemPackage $root $candidate $null} 'interrupted first installation is reported'
Set-Item -Path Function:Copy-TandemAtomic -Value $script:originalCopy
Install-TandemPackage $root $candidate $null|Out-Null
Require ((Read-TandemJson (Join-Path $root '.tandem-install.json')).phase -eq 'FilesReady') 'retry reaches disc selection'
foreach($file in $files){Require ((Get-TandemHash (Join-Path $root $file.path)) -eq $file.sha256) ('installed exact bytes '+$file.path)}
# An interrupted reinstall and its retry must preserve every retained byte.
$script:copied=0
function Copy-TandemAtomic([string]$From,[string]$To){if(++$script:copied -eq 2){throw 'simulated reinstall interruption'};& $script:originalCopy $From $To}
Reject {Install-TandemPackage $retained $candidate $null} 'interrupted reinstall reported'
Set-Item -Path Function:Copy-TandemAtomic -Value $script:originalCopy
foreach($path in $retainedHashes.Keys){Require ((Get-TandemHash $path) -eq $retainedHashes[$path]) 'interrupted reinstall preserves retained data'}
Install-TandemPackage $retained $candidate $null|Out-Null
foreach($path in $retainedHashes.Keys){Require ((Get-TandemHash $path) -eq $retainedHashes[$path]) 'completed reinstall preserves retained data'}
Require ((Get-TandemSettings $retained).player_name -eq 'Returning player') 'existing player preferences reused'
Require (Test-TandemPathEqual (Get-TandemInstallSaveFolder $retained) (Split-Path -Parent $card)) 'reinstalled game resolves original save folder'
$registration=Join-Path $Evidence 'registration/installed-root.txt'
Reject {Complete-TandemInstall $root $bootstrap $registration} 'cannot register an unbuilt installation'
Require (-not (Test-Path -LiteralPath $registration)) 'incomplete build does not replace remembered installation'
Write-TandemJson (Join-Path $root 'local-data/build-portable/identity/build-identity.json') @{id='0123456789abcdef'}
Text (Join-Path $root 'local-data/build-portable/mkdd.exe') 'game fixture'
Write-TandemJson (Join-Path $root 'local-data/settings.json') @{player_name='Test'}
Complete-TandemInstall $root $bootstrap $registration
Require ((Get-TandemHash (Join-Path $root 'ProjectTandem.exe')) -eq (Get-TandemHash $bootstrap)) 'standalone EXE retained as installed launcher'
Require (Test-TandemPathEqual ([IO.File]::ReadAllText($registration)) $root) 'original download remembers installed location'
Require ((Read-TandemJson (Join-Path $root '.tandem-install.json')).phase -eq 'Ready') 'completed installation recorded'
$bootstrapType=Add-Type -Path (Join-Path $source 'distribution/launcher/ProjectTandem.cs') -ReferencedAssemblies 'System.Windows.Forms' -PassThru
$resolve=$bootstrapType.GetMethod('ResolveRoot',([Reflection.BindingFlags]::Static -bor [Reflection.BindingFlags]::NonPublic))
Require ($resolve.Invoke($null,[object[]]@([string](Split-Path -Parent $bootstrap),[string]$registration)) -eq $root) 'downloaded EXE finds completed installation on reopening'
Require ($resolve.Invoke($null,[object[]]@([string]$root,'missing-registration')) -eq $root) 'installed EXE always prefers its own installation'
Text $registration (Join-Path $Evidence 'removed-folder')
Require ($resolve.Invoke($null,[object[]]@([string](Split-Path -Parent $bootstrap),[string]$registration)) -eq (Split-Path -Parent $bootstrap)) 'removed installation returns to installer'
$desktop=Join-Path $Evidence 'desktop';[IO.Directory]::CreateDirectory($desktop)|Out-Null
$shell=New-Object -ComObject WScript.Shell;$other=$shell.CreateShortcut((Join-Path $desktop 'Project Tandem.lnk'));$other.TargetPath=$bootstrap;$other.Save()
$originalShortcut=Get-TandemHash (Join-Path $desktop 'Project Tandem.lnk')
$shortcut=New-TandemDesktopShortcut $root $desktop
Require ((Split-Path -Leaf $shortcut) -eq 'Project Tandem (2).lnk') 'another installation shortcut not overwritten'
Require ((Get-TandemHash (Join-Path $desktop 'Project Tandem.lnk')) -eq $originalShortcut) 'previous shortcut exact bytes preserved'
Require (Test-TandemPathEqual ($shell.CreateShortcut($shortcut).TargetPath) (Join-Path $root 'ProjectTandem.exe')) 'desktop shortcut targets launcher'
Require ((New-TandemDesktopShortcut $root $desktop) -eq $shortcut) 'shortcut creation is idempotent'
$steam=Get-TandemSteamShortcutInfo $root
Require ($steam.Uri -eq 'steam://open/addnonsteamgame') 'Steam opens its own supported add-game route'
Require (Test-TandemPathEqual $steam.Launcher (Join-Path $root 'ProjectTandem.exe')) 'Steam path refers to installed launcher'
Require ($steam.Instructions -match 'Add Selected Programs') 'Steam confirmation explained'
$progress=ConvertTo-TandemInstallProgress '4/5 Build game 85% 391 / 460 build steps'
Require ($progress.Stage -eq '4/5 Build game' -and $progress.Percent -eq 85 -and $progress.Detail -eq '391 / 460 build steps') 'actual build progress percentage parsed'
$progress=ConvertTo-TandemInstallProgress '2/5 Cache your game data 0% 0 / 1392 MB'
Require ($progress.Stage -eq '2/5 Cache your game data' -and $progress.Percent -eq 0) 'zero percent cache progress preserved'
$progress=ConvertTo-TandemInstallProgress '3/5 Configure  Working; details saved in setup.log'
Require ($progress.Percent -eq -1 -and $progress.Stage -eq '3/5 Configure') 'unknown-duration phase remains indeterminate'
Require ($null -eq (ConvertTo-TandemInstallProgress 'plain setup log')) 'non-progress output ignored'
# Exercise the real worker/admission path and kill-on-close containment without
# launching a game or touching the user's installed build.
$workerRoot=Join-Path $Evidence 'worker';$guard=Initialize-TandemInstall $workerRoot $bootstrap;$guard.Dispose()
$image=Join-Path $Evidence 'fixture.iso';Text $image 'not a real image'
$compilerSource=Join-Path $Evidence 'sleeper.cs';Text $compilerSource 'class Sleeper {static void Main(){System.Threading.Thread.Sleep(60000);}}'
$compiler=Join-Path $Evidence 'sleeper.exe'
& (Join-Path $env:SystemRoot 'Microsoft.NET/Framework64/v4.0.30319/csc.exe') /nologo /target:winexe ("/out:"+$compiler) $compilerSource
Require ($LASTEXITCODE -eq 0) 'owned compiler fixture builds'
Text (Join-Path $workerRoot 'Setup-Tandem.ps1') @'
param([string]$GameImage)
$compiler=Join-Path (Split-Path -Parent $PSScriptRoot) 'sleeper.exe'
$child=Start-Process -FilePath $compiler -WindowStyle Hidden -PassThru
[IO.File]::WriteAllText((Join-Path $PSScriptRoot 'compiler-id.txt'),[string]$child.Id)
$child.WaitForExit()
'@
Initialize-TandemBuildJob
$container=New-Object TandemBuildJob;$worker=$null;$descendant=$null
try{
    $result=Join-Path $workerRoot 'result.json';$admission=Join-Path $workerRoot 'admitted.json'
    $arguments=@('-NoProfile','-ExecutionPolicy','Bypass','-File',(Join-Path $source 'distribution/Installer-Worker.ps1'),'-Root',$workerRoot,
        '-Action','Build','-Result',$result,'-ProgressFile',(Join-Path $workerRoot 'progress.json'),'-Bootstrap',$bootstrap,'-GameImage',$image,'-AdmissionFile',$admission)
    $worker=Start-Process -FilePath (Join-Path $env:SystemRoot 'System32/WindowsPowerShell/v1.0/powershell.exe') -ArgumentList (Join-TandemArguments $arguments) -WindowStyle Hidden -PassThru
    $container.Attach($worker)
    Write-TandemJson $admission @{id=$worker.Id;started=$worker.StartTime.ToUniversalTime().ToString('o')}
    $until=[DateTime]::UtcNow.AddSeconds(15);$childFile=Join-Path $workerRoot 'compiler-id.txt'
    while(-not (Test-Path -LiteralPath $childFile) -and [DateTime]::UtcNow -lt $until){Start-Sleep -Milliseconds 50}
    Require (Test-Path -LiteralPath $childFile) 'admitted real worker starts compiler fixture'
    $descendant=Get-Process -Id ([int][IO.File]::ReadAllText($childFile))
    $container.Dispose()
    Require ($worker.WaitForExit(10000)) 'installer interruption stops worker'
    Require ($descendant.WaitForExit(10000)) 'installer interruption stops compiler descendants'
    $guard=Initialize-TandemInstall $workerRoot $bootstrap;$guard.Dispose()
    Require $true 'interrupted worker releases install lock for retry'
}finally{
    $container.Dispose()
    if($worker -and -not $worker.HasExited){$worker.Kill();$worker.WaitForExit()}
    if($descendant -and -not $descendant.HasExited){$descendant.Kill();$descendant.WaitForExit()}
}
Write-TandemJson (Join-Path $Evidence 'result.json') @{passed=$true;checks=$script:checks}
Write-Output "PASS $script:checks first-install, retry, isolation, registration and shortcut checks"
