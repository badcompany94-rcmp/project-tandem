param([Parameter(Mandatory=$true)][string]$Evidence)
$ErrorActionPreference='Stop'
$source=Split-Path -Parent $PSScriptRoot
. (Join-Path $source 'distribution/Launcher-Core.ps1')
. (Join-Path $source 'distribution/Updater-Core.ps1')
$root=Join-Path $Evidence "Progress spaces % and ' quote.pc"
$runtime=Join-Path $root 'local-data/updates/active'
[IO.Directory]::CreateDirectory($runtime)|Out-Null
foreach($name in @('Updater-Core.ps1','Update-Tandem.ps1','Update-Build.ps1','Tandem-Paths.ps1','Launcher-Core.ps1')) {
    Copy-Item -LiteralPath (Join-Path $source ('distribution/'+$name)) -Destination (Join-Path $runtime $name)
}
Copy-Item -LiteralPath (Join-Path $source 'distribution/assets/project-tandem.ico') -Destination (Join-Path $runtime 'artwork.ico')
Copy-Item -LiteralPath (Join-Path $source 'distribution/launcher/TaskbarIdentity.cs') -Destination (Join-Path $runtime 'TaskbarIdentity.cs')
$ps=Join-Path $env:SystemRoot 'System32/WindowsPowerShell/v1.0/powershell.exe'
$args=@('-NoProfile','-STA','-ExecutionPolicy','Bypass','-File',(Join-Path $runtime 'Update-Tandem.ps1'),'-Root',$root,'-SelfTest','-Evidence')
$oldEvidence=Join-Path $Evidence 'hidden-startup'
$old=Start-Process -FilePath $ps -ArgumentList (Join-TandemArguments ($args+@($oldEvidence))) -WindowStyle Hidden -PassThru
try {if(-not $old.WaitForExit(30000) -or $old.ExitCode -ne 0){throw 'Old handoff reproduction failed'}}finally {if(-not $old.HasExited){$old.Kill()};$old.Dispose()}
$baseline=Read-TandemJson (Join-Path $oldEvidence 'result.json')
$newEvidence=Join-Path $Evidence 'visible-progress'
$child=Start-TandemUpdateProcess -Root $root -Arguments ($args+@($newEvidence)) -WorkingDirectory $runtime
try {if(-not $child.WaitForExit(30000) -or $child.ExitCode -ne 0){throw 'Visible updater process failed'}}finally {if(-not $child.HasExited){$child.Kill()};$child.Dispose()}
$result=Read-TandemJson (Join-Path $newEvidence 'result.json')
if(-not $result.passed -or $result.views.Count -ne 5){throw 'Progress window did not stay visible'}
foreach($view in $result.views) {
    if(-not $view.visible -or $view.handle -ne $result.initial_handle){throw 'A progress stage hid or replaced the window'}
    if($view.stage -in @('Installing','Building','Final checks') -and $view.cancel_enabled){throw 'Installation must not be cancellable midway'}
}
if(-not $result.views[0].cancel_enabled){throw 'Download cancellation must remain available'}
# A worker that fails before displaying its UI must return a failure, allowing
# the launcher to stay open rather than leaving an invisible task behind.
$failed=$false
try {Start-TandemUpdateProcess -Root $root -Arguments @('-NoProfile','-Command','exit 7') -WorkingDirectory $runtime|Out-Null}catch{$failed=$true}
if(-not $failed){throw 'An invisible failed worker was accepted'}
$legacyFile=Join-Path $Evidence 'legacy-worker.ps1'
[IO.File]::WriteAllText($legacyFile,"param([string]`$Root)`nexit 0")
$legacy=Start-TandemUpdateProcess -Root $root -Arguments @('-NoProfile','-File',$legacyFile,'-Root',$root) -WorkingDirectory $runtime -LegacyUi
try {if(-not $legacy.WaitForExit(10000) -or $legacy.ExitCode -ne 0){throw 'Old recovery workers received unsupported arguments'}}finally{$legacy.Dispose()}
if(Test-Path -LiteralPath (Join-Path $root 'local-data/update-transaction.json')){throw 'Render checks must not start an installation'}
Write-TandemJson (Join-Path $Evidence 'result.json') @{passed=$true;old_hidden_start_visible=$baseline.passed;new_start_visible=$result.passed;stages=5;failed_start_rejected=$failed;legacy_worker_compatible=$true;no_install=$true}
Write-Output 'PASS visible updater handoff, five persistent progress stages and failed-start handling'
