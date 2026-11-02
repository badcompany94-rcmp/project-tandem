param([Parameter(Mandatory=$true)][string]$Evidence)
$ErrorActionPreference='Stop'
$source=Split-Path -Parent $PSScriptRoot
. (Join-Path $source 'distribution/Launcher-Core.ps1')
. (Join-Path $source 'distribution/Tandem-Paths.ps1')
. (Join-Path $source 'distribution/Updater-Core.ps1')
Add-Type -AssemblyName System.IO.Compression.FileSystem
[IO.Directory]::CreateDirectory($Evidence)|Out-Null
$script:checks=0
function Require([bool]$Value,[string]$Message){$script:checks++;if(-not $Value){throw ('FAIL '+$Message)}}
function Reject([scriptblock]$Action,[string]$Message){$rejected=$false;try{& $Action|Out-Null}catch{$rejected=$true};Require $rejected $Message}
function Text([string]$Path,[string]$Value){[IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($Path))|Out-Null;[IO.File]::WriteAllText($Path,$Value)}
function Fixture([string]$Name) {
    $root=Join-Path $Evidence ($Name+" spaces % and ' quote.pc");[IO.Directory]::CreateDirectory($root)|Out-Null
    $stage=Join-Path $root 'local-data/test-stage';[IO.Directory]::CreateDirectory($stage)|Out-Null
    foreach($name in @('Launch-Tandem.ps1','source/tools/build_identity.py','docs/obsolete.txt')){Text (Join-Path $root $name) 'old'}
    Text (Join-Path $root 'ProjectTandem.exe') 'old launcher';Text (Join-Path $root 'Project Tandem.lnk') 'old shortcut'
    Text (Join-Path $root 'local-data/game/saves/personal.sav') 'my save'
    Text (Join-Path $root 'local-data/game/saves/controllers.cfg') 'my bindings'
    Text (Join-Path $root 'custom-user-file.txt') 'keep me'
    Text (Join-Path $root 'local-data/build-portable/mkdd.exe') 'old native game'
    Text (Join-Path $root 'local-data/build-portable/CMakeCache.txt') ('CMAKE_HOME_DIRECTORY:INTERNAL='+$root+'/source'+"`nCMAKE_CACHEFILE_DIR:INTERNAL="+$root+'/local-data/build-portable')
    Write-TandemJson (Join-Path $root 'local-data/build-portable/identity/build-identity.json') @{id='1111111111111111'}
    Write-TandemJson (Join-Path $root 'local-data/settings.json') @{player_name='JOHNNY';custom='preserve';game_image='old'}
    Text (Join-Path $root 'local-data/game-image.iso') 'private fixture disc'
    Write-TandemJson (Join-Path $root 'local-data/game-image.json') @{format=1;file='game-image.iso';bytes=20}
    $oldFiles=@('Launch-Tandem.ps1','source/tools/build_identity.py','docs/obsolete.txt')|ForEach-Object {@{path=$_;sha256=(Get-TandemHash (Join-Path $root $_))}}
    Write-TandemJson (Join-Path $root 'package-manifest.json') @{release='Public Beta 7';compatibility='1111111111111111';files=@($oldFiles)}
    Text (Join-Path $stage 'Launch-Tandem.ps1') 'new launch'
    Text (Join-Path $stage 'source/tools/build_identity.py') 'new identity source'
    Text (Join-Path $stage 'docs/new.txt') 'new doc'
    Text (Join-Path $stage 'Build-Launcher.ps1') "param([string]`$Root); [IO.File]::WriteAllText((Join-Path `$Root 'ProjectTandem.exe'),'new launcher')"
    Text (Join-Path $stage 'Create-Shortcut.ps1') "param([string]`$Root); [IO.File]::WriteAllText((Join-Path `$Root 'Project Tandem.lnk'),'new shortcut')"
    Text (Join-Path $stage 'Setup-Tandem.ps1') @'
param([string]$GameImage)
if(Test-Path -LiteralPath (Join-Path $PSScriptRoot 'pause-build.flag')){Start-Sleep -Seconds 120}
if(Test-Path -LiteralPath (Join-Path $PSScriptRoot 'fail-build.flag')){throw 'simulated compiler failure'}
$build=Join-Path $PSScriptRoot 'local-data/build-portable'
[IO.Directory]::CreateDirectory((Join-Path $build 'identity'))|Out-Null
[IO.File]::WriteAllText((Join-Path $build 'mkdd.exe'),'new native game')
@{id='2222222222222222'}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $build 'identity/build-identity.json')
'@
    $files=Get-ChildItem -LiteralPath $stage -File -Recurse|ForEach-Object {@{path=$_.FullName.Substring($stage.Length+1).Replace('\','/');sha256=(Get-TandemHash $_.FullName)}}
    $manifest=[pscustomobject]@{release='Public Beta 8';public_beta=$true;includes_game_data=$false;compatibility='2222222222222222';files=@($files)}
    Write-TandemJson (Join-Path $stage 'package-manifest.json') $manifest
    $record=[pscustomobject]@{Format=1;Id=[guid]::NewGuid().ToString('N');Root=$root;Tag='v0.1.0-beta.8';Phase='Starting';OwnerId=0;OwnerStarted=''}
    Set-TandemTransaction $root $record 'Starting'
    return [pscustomobject]@{Root=$root;Stage=$stage;Manifest=$manifest;Record=$record}
}
function IsRestored($f) {
    Require (([IO.File]::ReadAllText((Join-Path $f.Root 'local-data/build-portable/mkdd.exe'))) -eq 'old native game') 'previous executable restored'
    Require (([IO.File]::ReadAllText((Join-Path $f.Root 'ProjectTandem.exe'))) -eq 'old launcher') 'launcher rollback'
    Require ((Get-TandemInstalledTag $f.Root) -eq 'v0.1.0-beta.7') 'manifest rollback'
    Require (([IO.File]::ReadAllText((Join-Path $f.Root 'docs/obsolete.txt'))) -eq 'old') 'removed file restored'
    Require (-not (Test-Path -LiteralPath (Join-Path $f.Root 'docs/new.txt'))) 'new file removed during rollback'
    Require (([IO.File]::ReadAllText((Join-Path $f.Root 'local-data/game/saves/personal.sav'))) -eq 'my save') 'save preserved'
    Require (([IO.File]::ReadAllText((Join-Path $f.Root 'custom-user-file.txt'))) -eq 'keep me') 'unmanaged file preserved'
    Require (-not (Get-TandemUpdateStatus $f.Root).Pending) 'recovery marker cleared'
}
Require ((Get-TandemVersion 'v0.1.0-beta.10') -gt (Get-TandemVersion 'v0.1.0-beta.9')) 'numeric beta ordering'
Require ((Get-TandemVersion 'v0.1.0') -gt (Get-TandemVersion 'v0.1.0-beta.99')) 'stable follows beta'
Require ($null -eq (Get-TandemVersion 'v0.1.0-untrusted')) 'unknown release labels ignored'
Require ((Get-TandemPackagePath 'licenses/component/LICENSE.txt') -eq 'licenses/component/LICENSE.txt') 'distributed license notices accepted'
foreach($path in @('../bad','C:/bad','local-data/settings.json','source/../bad','source/file:stream','source/CON.txt','source/name.','source/name ','source/mkdd.exe','source/a.dll')){Reject {Get-TandemPackagePath $path} ('reject '+$path)}
Reject {Assert-TandemUrl ([uri]'http://api.github.com/repos/badcompany94-rcmp/project-tandem/releases')} 'reject HTTP'
Reject {Assert-TandemUrl ([uri]'https://api.github.com/repos/other/project/releases')} 'reject other repo'
Reject {Assert-TandemUrl ([uri]'https://example.org/a') -Redirect} 'reject external redirect'
function Release([int]$Beta,[bool]$Draft=$false) {
    return [pscustomobject]@{tag_name=('v0.1.0-beta.'+$Beta);name=('Public Beta '+$Beta);draft=$Draft;prerelease=$true;published_at='2026-01-01';assets=@(
        [pscustomobject]@{name=('Project-Tandem-Windows-Public-Beta-'+$Beta+'.zip');state='uploaded';size=200;browser_download_url=('https://github.com/badcompany94-rcmp/project-tandem/releases/download/v0.1.0-beta.'+$Beta+'/full.zip')},
        [pscustomobject]@{name=('Project-Tandem-Public-Beta-'+$Beta+'-Cumulative-Update.zip');state='uploaded';size=100;browser_download_url=('https://github.com/badcompany94-rcmp/project-tandem/releases/download/v0.1.0-beta.'+$Beta+'/update.zip')})}
}
$fixture=Fixture 'release-check'
function Get-TandemReleaseJson([string]$Url){return @((Release 8),(Release 10),(Release 11 $true),(Release 9))}
$update=Get-TandemUpdate $fixture.Root
Require ($update.Available -and $update.Candidate.Tag -eq 'v0.1.0-beta.10') 'latest published public beta detected'
function Get-TandemReleaseJson([string]$Url){throw 'simulated offline'}
Reject {Get-TandemUpdate $fixture.Root} 'offline check reports failure'
Require ((Get-TandemInstalledTag $fixture.Root) -eq 'v0.1.0-beta.7') 'offline check does not mutate installation'
Restore-TandemUpdate $fixture.Root $fixture.Record $null
$success=Fixture 'success'
$saves=Get-TandemHash (Join-Path $success.Root 'local-data/game/saves/controllers.cfg')
Install-TandemUpdate $success.Root $success.Stage $success.Manifest $success.Record $null
Require ((Get-TandemInstalledTag $success.Root) -eq 'v0.1.0-beta.8') 'successful update installs new manifest'
Require ((Get-Content -LiteralPath (Join-Path $success.Root 'local-data/build-portable/identity/build-identity.json') -Raw|ConvertFrom-Json).id -eq '2222222222222222') 'new build identity verified'
Require ((Get-TandemHash (Join-Path $success.Root 'local-data/game/saves/controllers.cfg')) -eq $saves) 'controls exact bytes preserved'
Require (-not (Test-Path -LiteralPath (Join-Path $success.Root 'docs/obsolete.txt'))) 'obsolete managed file removed'
Require (Test-Path -LiteralPath (Join-Path $success.Root 'custom-user-file.txt')) 'custom file preserved'
Require (-not (Get-TandemUpdateStatus $success.Root).Pending) 'successful transaction committed'
# A launcher-only update does not throw away a matching game build.
$same=Fixture 'launcher-only';$same.Manifest.compatibility='1111111111111111';Write-TandemJson (Join-Path $same.Stage 'package-manifest.json') $same.Manifest
Text (Join-Path $same.Root 'fail-build.flag') 'must not rebuild'
Install-TandemUpdate $same.Root $same.Stage $same.Manifest $same.Record $null
Require (([IO.File]::ReadAllText((Join-Path $same.Root 'local-data/build-portable/mkdd.exe'))) -eq 'old native game') 'matching game build reused'
$failure=Fixture 'failed-build'
$settings=Get-TandemHash (Join-Path $failure.Root 'local-data/settings.json')
Text (Join-Path $failure.Root 'fail-build.flag') 'fail'
Reject {Install-TandemUpdate $failure.Root $failure.Stage $failure.Manifest $failure.Record $null} 'build failure is reported'
Restore-TandemUpdate $failure.Root $failure.Record $null;IsRestored $failure
Require ((Get-TandemHash (Join-Path $failure.Root 'local-data/settings.json')) -eq $settings) 'settings exact bytes restored'
$zip=Join-Path $Evidence 'fixture.zip';[IO.Compression.ZipFile]::CreateFromDirectory($success.Stage,$zip)
$candidate=[pscustomobject]@{Tag='v0.1.0-beta.8'}
$expanded=Expand-TandemUpdate $zip (Join-Path $Evidence 'expanded') $success.Root $candidate $null
Require ($expanded.compatibility -eq '2222222222222222') 'valid archive hashes checked and expanded'
Reject {Expand-TandemUpdate $zip (Join-Path $Evidence 'wrong-version') $success.Root ([pscustomobject]@{Tag='v0.1.0-beta.9'}) $null} 'archive version mismatch rejected'
function BadZip([string]$Name,[string]$Entry,[string]$Contents) {
    $p=Join-Path $Evidence $Name;$z=[IO.Compression.ZipFile]::Open($p,[IO.Compression.ZipArchiveMode]::Create)
    try {$e=$z.CreateEntry($Entry);$w=New-Object IO.StreamWriter($e.Open());try{$w.Write($Contents)}finally{$w.Dispose()}}finally{$z.Dispose()};return $p
}
foreach($entry in @('../outside.txt','local-data/settings.json','source/FILE:stream')) {
    $bad=BadZip ([guid]::NewGuid().ToString('N')+'.zip') $entry 'bad'
    Reject {Expand-TandemUpdate $bad (Join-Path $Evidence 'bad-stage') $success.Root $candidate $null} ('archive path rejected '+$entry)
}
$bad=Join-Path $Evidence 'corrupt.zip';[IO.File]::Copy($zip,$bad)
$z=[IO.Compression.ZipFile]::Open($bad,[IO.Compression.ZipArchiveMode]::Update)
try {$e=$z.Entries|Where-Object {$_.FullName.Replace('\','/') -eq 'docs/new.txt'}|Select-Object -First 1;$e.Delete();$e=$z.CreateEntry('docs/new.txt');$w=New-Object IO.StreamWriter($e.Open());$w.Write('modified');$w.Dispose()}finally{$z.Dispose()}
Reject {Expand-TandemUpdate $bad (Join-Path $Evidence 'corrupt-stage') $success.Root $candidate $null} 'tampered archive file rejected'
$partial=Fixture 'cumulative-tools'
$toolHash='ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad'
$partial.Manifest.files+=@([pscustomobject]@{path='tools/required.txt';sha256=$toolHash})
Write-TandemJson (Join-Path $partial.Stage 'package-manifest.json') $partial.Manifest
$partialZip=Join-Path $Evidence 'cumulative-tools.zip';[IO.Compression.ZipFile]::CreateFromDirectory($partial.Stage,$partialZip)
$missingTool=$false
try {Expand-TandemUpdate $partialZip (Join-Path $Evidence 'missing-tool-stage') $partial.Root $candidate $null|Out-Null}catch{$missingTool=$_.Exception.Message -match 'TANDEM_FULL_PACKAGE_REQUIRED'}
Require $missingTool 'missing tool requests full-package fallback'
Text (Join-Path $partial.Root 'tools/required.txt') 'abc'
Require ((Expand-TandemUpdate $partialZip (Join-Path $Evidence 'valid-tool-stage') $partial.Root $candidate $null).compatibility -eq '2222222222222222') 'cumulative update verifies retained tools'
$linkTarget=Join-Path $Evidence 'junction-target';[IO.Directory]::CreateDirectory($linkTarget)|Out-Null
$link=Join-Path $partial.Root 'source/link';New-Item -ItemType Junction -Path $link -Target $linkTarget|Out-Null
Reject {Assert-TandemSafePath $partial.Root (Join-Path $link 'outside.txt')} 'linked destination rejected'
$duplicate=Join-Path $Evidence 'duplicate.zip';$z=[IO.Compression.ZipFile]::Open($duplicate,[IO.Compression.ZipArchiveMode]::Create)
try {$null=$z.CreateEntry('docs/test.txt');$null=$z.CreateEntry('DOCS/TEST.txt')}finally{$z.Dispose()}
Reject {Expand-TandemUpdate $duplicate (Join-Path $Evidence 'duplicate-stage') $success.Root $candidate $null} 'case-alias duplicates rejected'
# A real hidden process named mkdd in this fixture must block installation.
$busy=Fixture 'running-game';$stub=Join-Path $Evidence 'process-fixture.cs'
Text $stub 'class Fixture { static void Main(){System.Threading.Thread.Sleep(60000);} }'
$stubExe=Join-Path $busy.Root 'local-data/build-portable/mkdd.exe'
& (Join-Path $env:SystemRoot 'Microsoft.NET/Framework64/v4.0.30319/csc.exe') /nologo /target:winexe ("/out:"+$stubExe) $stub
Require ($LASTEXITCODE -eq 0) 'hidden process fixture compiled'
$running=Start-Process -FilePath $stubExe -WindowStyle Hidden -PassThru
try {Require (Get-TandemLauncherState $busy.Root).Running 'running game detected';Reject {Install-TandemUpdate $busy.Root $busy.Stage $busy.Manifest $busy.Record $null} 'running game blocks update'}
finally {if(-not $running.HasExited){$running.Kill();$running.WaitForExit()};$running.Dispose()}
Restore-TandemUpdate $busy.Root $busy.Record $null
$lock=Get-TandemUpdateLock $success.Root
try {Reject {Get-TandemUpdateLock $success.Root} 'concurrent updater lock enforced'}finally{$lock.Dispose()}
# Exercise truncated/tampered download rejection and cancellation without a web server.
$script:responses=0
function Open-TandemResponse([string]$Url,[switch]$Asset) {
    $script:responses++
    $reply=[pscustomobject]@{Stream=(New-Object IO.MemoryStream(,[Text.Encoding]::UTF8.GetBytes('abc')))}
    $reply|Add-Member -MemberType ScriptMethod -Name GetResponseStream -Value {return $this.Stream}
    $reply|Add-Member -MemberType ScriptMethod -Name Dispose -Value {$this.Stream.Dispose()}
    return $reply
}
$asset=[pscustomobject]@{name='download.zip';size=3;digest=('sha256:'+('0'*64));browser_download_url='https://github.com/badcompany94-rcmp/project-tandem/releases/download/v0.1.0-beta.8/test.zip'}
Reject {Save-TandemAsset $asset $Evidence $null} 'SHA-256 mismatch rejects download'
Require ($script:responses -eq 2) 'failed download retries only once'
Require (-not (Test-Path -LiteralPath (Join-Path $Evidence 'download.zip.part'))) 'failed partial download removed'
$asset.digest='sha256:ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad';$script:responses=0
$download=Save-TandemAsset $asset $Evidence $null
Require ((Get-TandemHash $download) -eq $asset.digest.Substring(7)) 'valid downloaded hash accepted'
Save-TandemAsset $asset $Evidence $null|Out-Null
Require ($script:responses -eq 1) 'verified cached download reused'
$asset.name='cancelled.zip'
Reject {Save-TandemAsset $asset $Evidence {throw (New-Object OperationCanceledException('cancel'))}} 'cancel stops download'
Require (-not (Test-Path -LiteralPath (Join-Path $Evidence 'cancelled.zip'))) 'cancelled download never installed'
$asset.name='truncated.zip';$asset.size=9
Reject {Save-TandemAsset $asset $Evidence $null} 'truncated download rejected'
# Simulate actual process interruption during a build, then recover in a new process.
$crash=Fixture 'interrupted';Text (Join-Path $crash.Root 'pause-build.flag') 'pause'
$inputFile=Join-Path $Evidence 'crash-input.json';Write-TandemJson $inputFile $crash
$childFile=Join-Path $Evidence 'crash-worker.ps1'
Text $childFile @'
param([string]$Source,[string]$Fixture)
$ErrorActionPreference='Stop'
. (Join-Path $Source 'distribution/Updater-Core.ps1');. (Join-Path $Source 'distribution/Tandem-Paths.ps1');. (Join-Path $Source 'distribution/Launcher-Core.ps1')
$f=Get-Content -LiteralPath $Fixture -Raw|ConvertFrom-Json
Set-TandemTransaction $f.Root $f.Record 'Starting'
Install-TandemUpdate $f.Root $f.Stage $f.Manifest $f.Record $null
'@
$ps=Join-Path $env:SystemRoot 'System32/WindowsPowerShell/v1.0/powershell.exe'
$child=Start-Process -FilePath $ps -ArgumentList (Join-TandemArguments @('-NoProfile','-ExecutionPolicy','Bypass','-File',$childFile,'-Source',$source,'-Fixture',$inputFile)) -WindowStyle Hidden -PassThru
try {
    $deadline=[DateTime]::UtcNow.AddSeconds(25)
    do {Start-Sleep -Milliseconds 100;$active=Read-TandemJson (Join-Path $crash.Root 'local-data/update-transaction.json');if([DateTime]::UtcNow -gt $deadline -or $child.HasExited){throw 'interruption fixture did not reach build'}}while(-not $active.BuildOwnerId)
    $child.Kill();$child.WaitForExit();Require (Get-TandemUpdateStatus $crash.Root).Recovery 'abrupt exit detected'
    $descendant=Get-Process -Id $active.BuildOwnerId -ErrorAction SilentlyContinue
    if($descendant){Require ($descendant.WaitForExit(5000)) 'compiler process tree stops when updater dies'}else{Require $true 'compiler process tree already stopped'}
    & $ps -NoProfile -ExecutionPolicy Bypass -File (Join-Path $source 'distribution/Update-Tandem.ps1') -Root $crash.Root -Recover -Headless
    Require ($LASTEXITCODE -eq 0) 'independent recovery worker completes'
    IsRestored $crash
}finally {if(-not $child.HasExited){$child.Kill()};$child.Dispose()}
# Exercise the actual detached handoff and immutable runtime, without any UI.
# A deliberately nonexistent official release must fail and retain the install.
$handoff=Fixture 'handoff';Restore-TandemUpdate $handoff.Root $handoff.Record $null
$handoffFile=Join-Path $Evidence 'handoff-worker.ps1'
Text $handoffFile @'
param([string]$Source,[string]$Root)
$ErrorActionPreference='Stop'
. (Join-Path $Source 'distribution/Launcher-Core.ps1');. (Join-Path $Source 'distribution/Updater-Core.ps1')
New-TandemUpdateWorker -Root $Root -Tag 'v0.0.0' -Headless|Out-Null
'@
$caller=Start-Process -FilePath $ps -ArgumentList (Join-TandemArguments @('-NoProfile','-ExecutionPolicy','Bypass','-File',$handoffFile,'-Source',$source,'-Root',$handoff.Root)) -WindowStyle Hidden -PassThru
try {Require ($caller.WaitForExit(10000) -and $caller.ExitCode -eq 0) 'detached update handoff starts'}finally{$caller.Dispose()}
$deadline=[DateTime]::UtcNow.AddSeconds(35)
while((Get-TandemUpdateStatus $handoff.Root).Pending){if([DateTime]::UtcNow -gt $deadline){throw 'handoff worker did not finish'};Start-Sleep -Milliseconds 100}
Require (Test-Path -LiteralPath (Join-Path $handoff.Root 'local-data/updates/active/Update-Build.ps1')) 'immutable recovery runtime copied'
IsRestored $handoff
# Custom saves stay outside the installation, but journals/builds remain local.
foreach($fail in @($false,$true)) {
    $custom=Fixture ('external-data-'+$fail)
    $external=Join-Path $Evidence ('user data '+$fail)
    Text (Join-Path $external 'game/saves/progress.sav') 'external progress'
    foreach($name in @('settings.json','game-image.json','game-image.iso')) {
        Copy-Item -LiteralPath (Join-Path $custom.Root ('local-data/'+$name)) -Destination (Join-Path $external $name)
    }
    Write-TandemJson (Join-Path $custom.Root 'paths.json') @{data=$external}
    Initialize-TandemDataRoot $custom.Root|Out-Null
    $metadataHash=Get-TandemHash (Join-Path $external 'settings.json')
    if($fail) {
        Text (Join-Path $custom.Stage 'Setup-Tandem.ps1') @'
param([string]$GameImage)
. (Join-Path $PSScriptRoot 'Tandem-Paths.ps1')
[IO.File]::WriteAllText((Join-Path (Get-TandemDataRoot $PSScriptRoot) 'settings.json'),'changed during failing build')
throw 'custom-data simulated failure'
'@
        Copy-Item -LiteralPath (Join-Path $source 'distribution/Tandem-Paths.ps1') -Destination (Join-Path $custom.Root 'Tandem-Paths.ps1')
        Reject {Install-TandemUpdate $custom.Root $custom.Stage $custom.Manifest $custom.Record $null} 'external-data build failure reported'
        Restore-TandemUpdate $custom.Root $custom.Record $null
        IsRestored $custom
    }else{
        Install-TandemUpdate $custom.Root $custom.Stage $custom.Manifest $custom.Record $null
        Require ((Get-TandemInstalledTag $custom.Root) -eq 'v0.1.0-beta.8') 'external-data update committed'
    }
    Require ((Get-TandemHash (Join-Path $external 'settings.json')) -eq $metadataHash) 'external settings preserved/restored byte for byte'
    Require (([IO.File]::ReadAllText((Join-Path $external 'game/saves/progress.sav'))) -eq 'external progress') 'external save preserved'
    Require (-not (Test-Path -LiteralPath (Join-Path $external 'updates'))) 'update journals never redirected into user data'
}
$moved=Fixture 'moved-matching-build';$moved.Manifest.compatibility='1111111111111111'
Text (Join-Path $moved.Root 'local-data/build-portable/CMakeCache.txt') 'CMAKE_HOME_DIRECTORY:INTERNAL=Z:/old/source'
Text (Join-Path $moved.Root 'fail-build.flag') 'must not rebuild matching moved game'
Install-TandemUpdate $moved.Root $moved.Stage $moved.Manifest $moved.Record $null
Require (([IO.File]::ReadAllText((Join-Path $moved.Root 'local-data/build-portable/mkdd.exe'))) -eq 'old native game') 'moved matching game reused during update'
foreach($reserved in @('paths.json','portable.txt')){Reject {Get-TandemPackagePath $reserved} ('archive cannot redirect user data with '+$reserved)}
Write-TandemJson (Join-Path $Evidence 'result.json') @{passed=$true;checks=$script:checks;fixture_game_only=$true;real_game_launched=$false}
Write-Output "PASS $script:checks updater version, verification, install, preservation, rollback and interruption checks"
