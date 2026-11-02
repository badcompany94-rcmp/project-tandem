param([Parameter(Mandatory=$true)][string]$Evidence,
      [Parameter(Mandatory=$true)][string]$Runtime,
      [Parameter(Mandatory=$true)][string]$GameImage,
      [Parameter(Mandatory=$true)][string]$Python,
      [Parameter(Mandatory=$true)][string]$Compatibility)
$ErrorActionPreference='Stop'
$source=Split-Path -Parent $PSScriptRoot
$base=[IO.Path]::GetFullPath($Evidence)
New-Item -ItemType Directory -Force -Path $base|Out-Null
$root=Join-Path $base 'original install'
if(Test-Path -LiteralPath $root){throw 'Use a fresh evidence directory.'}
foreach($dir in @('local-data/build-portable/identity','tools/python','source/gcn-recomp/recomp')) {
    New-Item -ItemType Directory -Force -Path (Join-Path $root $dir)|Out-Null
}
foreach($name in @('Launch-Tandem.ps1','Tandem-Paths.ps1','Mods-Core.ps1')) {
    Copy-Item -LiteralPath (Join-Path $source ('distribution/'+$name)) -Destination (Join-Path $root $name)
}
foreach($name in @('extract_dol.py','dol.py')) {
    Copy-Item -LiteralPath (Join-Path $source ('gcn-recomp/recomp/'+$name)) -Destination (Join-Path $root ('source/gcn-recomp/recomp/'+$name))
}
Copy-Item -Path (Join-Path (Split-Path -Parent $Python) '*') -Destination (Join-Path $root 'tools/python') -Recurse
New-Item -ItemType HardLink -Path (Join-Path $root 'local-data/build-portable/mkdd.exe') -Target $Runtime|Out-Null
New-Item -ItemType HardLink -Path (Join-Path $root 'local-data/game-image.iso') -Target $GameImage|Out-Null
@{format=1;file='game-image.iso';bytes=(Get-Item -LiteralPath $GameImage).Length}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $root 'local-data/game-image.json')
@{compatibility=$Compatibility;files=@(@{path='fixture';sha256='0'})}|ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $root 'package-manifest.json')
@{id=$Compatibility}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $root 'local-data/build-portable/identity/build-identity.json')
@{player_name='PORTABLE';game_image=(Join-Path $root 'local-data/game-image.iso');source=(Join-Path $root 'source')}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $root 'local-data/settings.json')
"throw 'Unexpected native rebuild'"|Set-Content -LiteralPath (Join-Path $root 'Setup-Tandem.ps1')
@("CMAKE_HOME_DIRECTORY:INTERNAL=$root/source","CMAKE_CACHEFILE_DIR:INTERNAL=$root/local-data/build-portable","GAME_ISO:FILEPATH=$root/local-data/game-image.iso")|Set-Content -LiteralPath (Join-Path $root 'local-data/build-portable/CMakeCache.txt')
$cacheHash=(Get-FileHash -LiteralPath (Join-Path $root 'local-data/build-portable/CMakeCache.txt')).Hash
$exeHash=(Get-FileHash -LiteralPath $Runtime).Hash
$global:TandemPortableTestProcess=$null
function Start-Process {
    param($FilePath,$ArgumentList,$WorkingDirectory,$WindowStyle,[switch]$PassThru,$RedirectStandardOutput,$RedirectStandardError)
    if(-not $FilePath.EndsWith('\mkdd.exe')){throw 'Unexpected helper in offline relocation test'}
    foreach($key in @('INPUT','DISPLAY','SOUND','PRESENTATION','OVERLAY')){[Environment]::SetEnvironmentVariable('GCN_'+$key+'_CONFIG',(Join-Path $WorkingDirectory ($key+'.cfg')),'Process')}
    $args=@($ArgumentList|ForEach-Object {switch -Wildcard ($_){'--fullscreen'{'--hidden'} '--scale=*'{'--scale=1'} '--resolution=*'{'--resolution=640x480'} default{$_}}})
    $args+='--fast'
    $global:TandemPortableTestProcess=Microsoft.PowerShell.Management\Start-Process -FilePath $FilePath -ArgumentList $args -WorkingDirectory $WorkingDirectory -WindowStyle Hidden -PassThru -RedirectStandardOutput $RedirectStandardOutput -RedirectStandardError $RedirectStandardError
    return $global:TandemPortableTestProcess
}
$reports=@()
foreach($name in @('original install',"renamed spaces % and ' quote",'moved/portable install')) {
    $next=[IO.Path]::GetFullPath((Join-Path $base $name))
    if(-not $next.StartsWith($base.TrimEnd('\')+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Fixture move escapes evidence directory'}
    if($root -ne $next){New-Item -ItemType Directory -Force -Path (Split-Path -Parent $next)|Out-Null;Move-Item -LiteralPath $root -Destination $next;$root=$next}
    & (Join-Path $root 'Launch-Tandem.ps1') -Offline -Frames 900
    if(-not $global:TandemPortableTestProcess.WaitForExit(60000)){throw 'Native relocation launch timed out'}
    $state=Get-Content -LiteralPath (Join-Path $root 'local-data/latest-game.json') -Raw|ConvertFrom-Json
    $log=Get-Content -LiteralPath (Join-Path $state.session 'game-stderr.log') -Raw
    if(-not $log.Contains('[exit] frame limit 900 reached') -or $log.Contains('FATAL:')){throw 'Moved runtime failed to boot'}
    if((Get-FileHash -LiteralPath (Join-Path $root 'local-data/build-portable/mkdd.exe')).Hash -ne $exeHash){throw 'Native binary changed'}
    if((Get-FileHash -LiteralPath (Join-Path $root 'local-data/build-portable/CMakeCache.txt')).Hash -ne $cacheHash){throw 'Launch altered compiler cache'}
    $settings=Get-Content -LiteralPath (Join-Path $root 'local-data/settings.json') -Raw|ConvertFrom-Json
    if($settings.source -ne (Join-Path $root 'source') -or $settings.game_image -ne (Join-Path $root 'local-data/game-image.iso')){throw 'Settings did not follow moved install'}
    $reports+=@{path=$root;native_boot=$true;binary_unchanged=$true;compiler_not_called=$true}
}
# Relocation must not bypass a new version, pending update, or missing runtime.
foreach($kind in @('new-version','pending-update','missing-runtime')) {
    $idFile=Join-Path $root 'local-data/build-portable/identity/build-identity.json'
    $exe=Join-Path $root 'local-data/build-portable/mkdd.exe'
    if($kind -eq 'new-version'){@{id='outdated'}|ConvertTo-Json|Set-Content -LiteralPath $idFile}
    elseif($kind -eq 'pending-update'){'{}'|Set-Content -LiteralPath (Join-Path $root 'local-data/update-pending.json')}
    else{Move-Item -LiteralPath $exe -Destination ($exe+'.saved')}
    $rejected=$false
    try {& (Join-Path $root 'Launch-Tandem.ps1') -Offline -Frames 900}catch{$rejected=$_.Exception.Message -match 'Unexpected native rebuild'}
    finally {
        @{id=$Compatibility}|ConvertTo-Json|Set-Content -LiteralPath $idFile
        if($kind -eq 'pending-update'){Remove-Item -LiteralPath (Join-Path $root 'local-data/update-pending.json')}
        if($kind -eq 'missing-runtime'){Move-Item -LiteralPath ($exe+'.saved') -Destination $exe}
    }
    if(-not $rejected){throw "Required rebuild was bypassed: $kind"}
}
$reports|ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $base 'result.json')
'PASS: three real native launches after move/rename; no compile; outdated/pending/missing builds still require setup.'

