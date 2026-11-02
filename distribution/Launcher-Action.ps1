param([Parameter(Mandatory=$true)][string]$Root,
      [Parameter(Mandatory=$true)][ValidateSet('Play','Setup','Repair','CheckUpdate')][string]$Action,
      [Parameter(Mandatory=$true)][string]$Result,[string]$GameImage)
$ErrorActionPreference='Stop'
$rootPath=[IO.Path]::GetFullPath($Root)
$record=@{succeeded=$false;action=$Action;message='';finished=$null}
try {
    . (Join-Path $PSScriptRoot 'Launcher-Core.ps1')
    $state=Get-TandemLauncherState $rootPath
    if(-not $state.PackageValid){throw $state.Detail}
    if($Action -eq 'CheckUpdate') {
        . (Join-Path $PSScriptRoot 'Updater-Core.ps1')
        $record.update=Get-TandemUpdate $rootPath
        Write-TandemJson (Join-Path (Get-TandemDataRoot $rootPath) 'update-check.json') $record.update
        $record.message=if($record.update.Available){'An update is available.'}else{'You have the latest published Windows release.'}
    }elseif($Action -eq 'Play') {
        $started=Get-Date
        & (Join-Path $rootPath 'Launch-Tandem.ps1')
        $latest=Join-Path (Get-TandemDataRoot $rootPath) 'latest-game.json'
        if(-not (Test-Path -LiteralPath $latest) -or (Get-Item -LiteralPath $latest).LastWriteTime -lt $started){throw 'The game did not start. Check the launch log or run setup.'}
        $game=Get-Content -LiteralPath $latest -Raw|ConvertFrom-Json
        if(-not (Get-Process -Id $game.game_id -ErrorAction SilentlyContinue)){throw 'The game exited during startup. Check the latest session logs.'}
        $record.message='Game launched. Networking closes automatically when you exit.'
    }else {
        if($GameImage){& (Join-Path $rootPath 'Setup-Tandem.ps1') -GameImage $GameImage}
        else {& (Join-Path $rootPath 'Setup-Tandem.ps1')}
        if(-not (Get-TandemLauncherState $rootPath).Ready){throw 'Setup did not finish. Your existing saves are unchanged; see the log for the cause.'}
        $record.message='Setup complete. Ready to play.'
    }
    $record.succeeded=$true
}catch {
    $record.message=$_.Exception.Message
    [Console]::Error.WriteLine($record.message)
}finally {
    $record.finished=(Get-Date).ToUniversalTime().ToString('o')
    $record|ConvertTo-Json -Depth 5|Set-Content -LiteralPath $Result -Encoding UTF8
}
if(-not $record.succeeded){exit 1}
