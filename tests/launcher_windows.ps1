param([Parameter(Mandatory=$true)][string]$Evidence)
$ErrorActionPreference='Stop'
$source=Split-Path -Parent $PSScriptRoot
. (Join-Path $source 'distribution/Launcher-Core.ps1')
$checks=0
function Require([bool]$Value,[string]$Message){$script:checks++;if(-not $Value){throw $Message}}
$root=Join-Path $Evidence "Tandem spaces % and ' quote.pc"
New-Item -ItemType Directory -Path $root -Force|Out-Null
Require (-not (Get-TandemLauncherState $root).PackageValid) 'missing package must be explained'
$manifest=Join-Path $root 'package-manifest.json'
'broken json'|Set-Content -LiteralPath $manifest
Require (-not (Get-TandemLauncherState $root).PackageValid) 'corrupt manifest handled'
@{release='Test';compatibility='1234567890abcdef';files=@(@{path='fixture';sha256='0'})}|ConvertTo-Json -Depth 5|Set-Content -LiteralPath $manifest
Require (Get-TandemLauncherState $root).PackageValid 'valid package recognized'
Require (-not (Get-TandemLauncherState $root).Ready) 'fresh install needs setup'
@'
param([string]$GameImage)
if(-not $GameImage -or -not (Test-Path -LiteralPath $GameImage)){throw 'fixture image missing'}
$build=Join-Path $PSScriptRoot 'local-data/build-portable'
New-Item -ItemType Directory -Path (Join-Path $build 'identity') -Force|Out-Null
@{id='1234567890abcdef'}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $build 'identity/build-identity.json')
'fixture'|Set-Content -LiteralPath (Join-Path $build 'mkdd.exe')
@{game_image=$GameImage}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $PSScriptRoot 'local-data/settings.json')
'fixture setup finished'
'@|Set-Content -LiteralPath (Join-Path $root 'Setup-Tandem.ps1')
function Wait-JobResult($Job) {
    if(-not $Job.Process.WaitForExit(20000)){throw 'launcher test worker timeout'}
    Require (Test-Path -LiteralPath $Job.Result) 'worker always produces a result'
    return Get-Content -LiteralPath $Job.Result -Raw|ConvertFrom-Json
}
$worker=Join-Path $source 'distribution/Launcher-Action.ps1'
$job=New-TandemLauncherJob $root 'Setup' '' $worker
$result=Wait-JobResult $job
Require (-not $result.succeeded -and $result.message -match 'fixture image missing') 'setup failure visible with useful error'
Require (Test-Path -LiteralPath (Join-Path $job.Folder 'stderr.log')) 'error log retained'
$image=Join-Path $root "disc's image %.iso";'fixture'|Set-Content -LiteralPath $image
$job=New-TandemLauncherJob $root 'Setup' $image $worker
Require (Wait-JobResult $job).succeeded 'quoted file paths survive background process'
Require (Get-TandemLauncherState $root).Ready 'successful setup becomes ready'
$pending=Join-Path $root 'local-data/update-pending.json';'{}'|Set-Content -LiteralPath $pending
Require (-not (Get-TandemLauncherState $root).Ready) 'pending update requires rebuild'
Remove-Item -LiteralPath $pending
"throw 'fixture game failed at startup'"|Set-Content -LiteralPath (Join-Path $root 'Launch-Tandem.ps1')
$job=New-TandemLauncherJob $root 'Play' '' $worker
$result=Wait-JobResult $job
Require (-not $result.succeeded -and $result.message -match 'fixture game failed') 'failed launch does not silently disappear'
@{passed=$true;checks=$checks;game_launched=$false;fixture=$root}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $Evidence 'result.json') -Encoding UTF8
Write-Output "PASS $checks launcher state, setup, quoting, rebuild and failure-reporting checks"
