param([Parameter(Mandatory=$true)][string]$Root,[string]$Output)
$ErrorActionPreference='Stop'
$compiler=Join-Path $env:SystemRoot 'Microsoft.NET/Framework64/v4.0.30319/csc.exe'
if(-not (Test-Path -LiteralPath $compiler)){throw 'The Windows .NET Framework C# compiler is unavailable. Launch-Tandem.cmd still works.'}
$source=Join-Path $Root 'source/distribution/launcher/ProjectTandem.cs'
$icon=Join-Path $Root 'source/distribution/assets/project-tandem.ico'
$standard=(-not $Output)
if($standard){$Output=Join-Path $Root 'ProjectTandem.exe'}
$resourceNames=@('Install-Tandem.ps1','Installer-Core.ps1','Installer-Worker.ps1','Updater-Core.ps1','Tandem-Paths.ps1','Launcher-Core.ps1')
$resources=@();$fingerprint=(Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash+':'+(Get-FileHash -LiteralPath $icon -Algorithm SHA256).Hash
foreach($name in $resourceNames){
    $file=Join-Path $Root ('source/distribution/'+$name)
    $fingerprint+=':'+(Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash
    $resources+=('/resource:'+ $file+',TandemInstaller.'+$name)
}
$resources+=('/resource:'+$icon+',TandemInstaller.project-tandem.ico')
$identitySource=Join-Path $Root 'source/distribution/launcher/TaskbarIdentity.cs'
$fingerprint+=':'+(Get-FileHash -LiteralPath $identitySource -Algorithm SHA256).Hash
$resources+=('/resource:'+$identitySource+',TandemInstaller.TaskbarIdentity.cs')
$metadata=Join-Path $Root 'local-data/launcher-build.json'
if($standard -and (Test-Path -LiteralPath $Output) -and (Test-Path -LiteralPath $metadata)) {
    try {$built=Get-Content -LiteralPath $metadata -Raw|ConvertFrom-Json
        if($built.source -eq $fingerprint -and $built.binary -eq (Get-FileHash -LiteralPath $Output -Algorithm SHA256).Hash){return}
    }catch{}
}
# Compile beside the destination, then replace atomically. A stopped compiler
# must never leave a partial ProjectTandem.exe as the recovery entry point.
$temporary=Join-Path (Split-Path -Parent $Output) ('launcher-'+[guid]::NewGuid().ToString('N')+'.exe')
try {
    & $compiler /nologo /target:winexe /optimize+ /reference:System.Windows.Forms.dll ("/win32icon:"+$icon) @resources ("/out:"+$temporary) $source
    if($LASTEXITCODE -ne 0){throw 'Could not build the graphical launcher.'}
    if(Test-Path -LiteralPath $Output){[IO.File]::Replace($temporary,$Output,[System.Management.Automation.Language.NullString]::Value)}else{[IO.File]::Move($temporary,$Output)}
    if($standard){New-Item -ItemType Directory -Path (Split-Path -Parent $metadata) -Force|Out-Null
        @{source=$fingerprint;binary=(Get-FileHash -LiteralPath $Output -Algorithm SHA256).Hash}|ConvertTo-Json|Set-Content -LiteralPath $metadata -Encoding UTF8}
}finally {if(Test-Path -LiteralPath $temporary){Remove-Item -LiteralPath $temporary}}
