param([Parameter(Mandatory=$true)][string]$Session)
$ErrorActionPreference='Stop'
$sessions=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot 'local-data\sessions')).TrimEnd('\')+'\'
$Session=[IO.Path]::GetFullPath($Session).TrimEnd('\')
if(-not $Session.StartsWith($sessions,[StringComparison]::OrdinalIgnoreCase)){throw 'The online session is outside this installation.'}
$log=Join-Path $Session 'watch-online.log'
function Note([string]$message) {Add-Content -LiteralPath $log -Value ((Get-Date).ToUniversalTime().ToString('o')+' '+$message)}
$owned=$false
try {
    $game=Get-Content -LiteralPath (Join-Path $Session 'game-session.json') -Raw|ConvertFrom-Json
    $service=Get-Content -LiteralPath (Join-Path $Session 'online-service.json') -Raw|ConvertFrom-Json
    $expected=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot 'local-data\build-portable\mkdd.exe'))
    if(-not $game.online -or -not $expected.Equals([IO.Path]::GetFullPath($game.game_path),[StringComparison]::OrdinalIgnoreCase)) {throw 'The recorded game is outside this installation.'}
    if(-not $Session.Equals([IO.Path]::GetFullPath($service.session).TrimEnd('\'),[StringComparison]::OrdinalIgnoreCase)) {throw 'The online service belongs to a different session.'}
    $owned=$true
    Note 'Watching this installation across native race and lobby process replacements.'
    'ready'|Set-Content -LiteralPath (Join-Path $Session 'watch-online.ready') -Encoding ascii
    $clock=[Diagnostics.Stopwatch]::StartNew();$absentSince=$null
    while($true) {
        # _execv starts a replacement Windows process for race/lobby changes.
        # Follow the exact installed executable, not only the first process ID.
        $running=@(Get-Process -Name mkdd -ErrorAction SilentlyContinue|Where-Object {
            try {$_.Path -and $expected.Equals($_.Path,[StringComparison]::OrdinalIgnoreCase)}catch{$false}
        })
        if($running.Count){$absentSince=$null}
        elseif($null -eq $absentSince){$absentSince=$clock.ElapsedMilliseconds}
        elseif($clock.ElapsedMilliseconds-$absentSince -ge 2000){Note 'Game closed; stopping this session online services.';break}
        $latestFile=Join-Path $PSScriptRoot 'local-data\online-service.json'
        try {
            $latest=Get-Content -LiteralPath $latestFile -Raw|ConvertFrom-Json
            if($latest.session -and -not $Session.Equals([IO.Path]::GetFullPath($latest.session).TrimEnd('\'),[StringComparison]::OrdinalIgnoreCase)) {
                Note 'A new launch owns the installation; retiring this session watcher.';break
            }
        }catch{} # A new launcher can be replacing the latest-session record.
        Start-Sleep -Milliseconds 500
    }
}catch {Note ('Watcher error: '+$_.Exception.Message);throw}
finally {
    if($owned){& (Join-Path $PSScriptRoot 'Stop-Online.ps1') -Session $Session;Note 'Online cleanup complete.'}
}
