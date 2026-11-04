$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot '../distribution/Trials-Core.ps1')
$folder=Join-Path ([IO.Path]::GetTempPath()) ('tandem-trial-identity-'+[guid]::NewGuid().ToString('N'))
[IO.Directory]::CreateDirectory($folder)|Out-Null
$a=Initialize-TandemTrialIdentity (Join-Path $folder 'first')
$bytes=[IO.File]::ReadAllBytes($a)
if((Initialize-TandemTrialIdentity (Join-Path $folder 'first')) -ne $a){throw 'Identity path changed'}
if([Convert]::ToBase64String([IO.File]::ReadAllBytes($a)) -cne [Convert]::ToBase64String($bytes)){throw 'Existing identity changed'}
$b=Initialize-TandemTrialIdentity (Join-Path $folder 'second')
if((Get-Content $a -Raw) -ceq (Get-Content $b -Raw)){throw 'Installations share an identity'}
[IO.File]::WriteAllText($a,'{"scope":"public","key":"broken"}')
$rejected=$false
try {Initialize-TandemTrialIdentity (Join-Path $folder 'first')|Out-Null} catch {$rejected=$true}
if(-not $rejected -or (Get-Content $a -Raw) -cne '{"scope":"public","key":"broken"}'){throw 'Corrupt identity overwritten or accepted'}
Write-Host 'PASS trial identity: unique installations, stable reuse, corrupt data preserved'
