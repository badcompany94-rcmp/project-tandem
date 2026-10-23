param([switch]$Offline,[uint32]$Frames=0)
$ErrorActionPreference='Stop'
$root=$PSScriptRoot;$data=Join-Path $root 'local-data'
$settingsFile=Join-Path $data 'settings.json'
if(-not (Test-Path -LiteralPath $settingsFile)){throw 'Run Setup-Tandem.cmd first and select your own compatible disc image.'}
. (Join-Path $root 'Tandem-Paths.ps1')
$settings=Get-TandemSettings $root
Assert-TandemClosed $root
$image=Resolve-TandemImage $root $settings $null
if(-not $image){Write-Host 'Launch cancelled.';return}
$build=Join-Path $data 'build-portable'
$package=Get-Content -LiteralPath (Join-Path $root 'package-manifest.json') -Raw|ConvertFrom-Json
$installedIdentity=Join-Path $build 'identity\build-identity.json';$installedBuild=$null
if(Test-Path -LiteralPath $installedIdentity){try{$installedBuild=(Get-Content -LiteralPath $installedIdentity -Raw|ConvertFrom-Json).id}catch{}}
if($installedBuild -ne $package.compatibility -or (Test-Path -LiteralPath (Join-Path $data 'update-pending.json')) -or (Test-TandemBuildMoved $root) -or -not (Test-Path -LiteralPath (Join-Path $build 'mkdd.exe')) -or
   -not (Test-TandemPathEqual (Get-TandemCacheValue $build 'GAME_ISO') $image)) {
    Write-Host 'Updating the local build for the current game and ISO locations...'
    & (Join-Path $root 'Setup-Tandem.ps1') -GameImage $image
    $settings=Get-TandemSettings $root
} else {
    Save-TandemSettings $root $settings $image
}
foreach($file in @($settings.game_image,$settings.game_executable,$settings.python)){if(-not (Test-Path -LiteralPath $file -PathType Leaf)){throw "Required local file missing: $file. Run setup again."}}
if(Get-Process -Name mkdd -ErrorAction SilentlyContinue | Where-Object {$_.Path -eq $settings.game_executable}){throw 'This package is already running. Close its game window before starting another session.'}
foreach($key in @([Environment]::GetEnvironmentVariables('Process').Keys)) {
    if($key -like 'MKDD_NET_*' -or $key -in @('MKDD_ROOM_PORT','MKDD_ROOM_BACKEND','MKDD_ONLINE_MENU','MKDD_EXPECT_PREDICTION','TANDEM_EOS_ROLLBACK','TANDEM_EOS_MESH','TANDEM_EOS_ORDERED_INPUT','GCN_INPUT','GCN_CLOCK','GCN_CPU_SCALE','GCN_NET_STATS','GCN_FRAME_STATS','GCN_PIPELINE_TRACE','GCN_GAME_SCHEDULING','MKDD_SYNC_TRACE','MKDD_MENU_TEST')){[Environment]::SetEnvironmentVariable($key,$null,'Process')}
}
function Quote([string]$value){return '"'+$value+'"'}
function Health([int]$port) {
    $request=[Net.HttpWebRequest]::Create("http://127.0.0.1:$port/health");$request.Proxy=$null;$request.AllowAutoRedirect=$false;$request.Timeout=1000
    $response=$request.GetResponse();try{$reader=New-Object IO.StreamReader($response.GetResponseStream());try{return ($reader.ReadToEnd()|ConvertFrom-Json)}finally{$reader.Dispose()}}finally{$response.Dispose()}
}
$session=Join-Path $data ('sessions\'+(Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
New-Item -ItemType Directory -Force -Path $session | Out-Null
$online=$false;$helper=$null;$upstream=$null
if(-not $Offline) {
    # The EOS helper negotiates prediction only when this switch is present.
    # Set it before starting the helper; native-only developer fixtures are
    # insufficient to verify the normal portable launch path.
    $env:TANDEM_EOS_ROLLBACK='1';$env:MKDD_EXPECT_PREDICTION='1'
    $helperExe=Join-Path $root 'runtime\mkdd_eos.exe';$config=Join-Path $root 'config\eos.json'
    if(-not (Test-Path -LiteralPath $helperExe) -or -not (Test-Path -LiteralPath $config)){throw 'The EOS runtime or product configuration is missing. Extract the complete test package again.'}
    if(-not (Test-Path -LiteralPath (Join-Path $root 'Watch-Online.ps1'))){throw 'The online shutdown helper is missing. Extract the complete update again.'}
    $eosData=Join-Path $data 'eos';New-Item -ItemType Directory -Force -Path $eosData | Out-Null
    $localConfig=Join-Path $eosData 'eos.json'
    Copy-Item -LiteralPath $config -Destination $localConfig -Force
    $helperHash=(Get-FileHash -LiteralPath $helperExe -Algorithm SHA256).Hash
    $configHash=(Get-FileHash -LiteralPath $config -Algorithm SHA256).Hash
    $latestFile=Join-Path $data 'online-service.json'
    if(Test-Path -LiteralPath $latestFile) {
        # Each launch starts a fresh room session. An old running match must
        # not strand this new title menu in an already-started EOS lobby.
        & (Join-Path $root 'Stop-Online.ps1')
    }
    if(-not $online) {
        $upReady=Join-Path $session 'upstream-ready.json';$ready=Join-Path $session 'eos-ready.json'
        try {
            $upstream=Start-Process -FilePath $settings.python -ArgumentList @((Quote (Join-Path $settings.source 'services\rooms.py')),'--bind','127.0.0.1','--port','0','--ready-file',(Quote $upReady)) -WorkingDirectory $session -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $session 'rooms-stdout.log') -RedirectStandardError (Join-Path $session 'rooms-stderr.log')
            $deadline=(Get-Date).AddSeconds(15)
            while(-not (Test-Path -LiteralPath $upReady)) {if($upstream.HasExited -or (Get-Date) -gt $deadline){throw 'Local room model did not start.'};Start-Sleep -Milliseconds 100;$upstream.Refresh()}
            $up=Get-Content -LiteralPath $upReady -Raw|ConvertFrom-Json
            Write-Host 'Connecting Project Tandem to EOS...'
            $helper=Start-Process -FilePath $helperExe -ArgumentList @('--serve',(Quote $localConfig),$up.port,(Quote $ready)) -WorkingDirectory $session -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $session 'eos-stdout.log') -RedirectStandardError (Join-Path $session 'eos-stderr.log')
            $deadline=(Get-Date).AddSeconds(65)
            while(-not (Test-Path -LiteralPath $ready)) {if($helper.HasExited -or (Get-Date) -gt $deadline){throw 'EOS login did not complete. Check your Internet connection and the session logs.'};Start-Sleep -Milliseconds 100;$helper.Refresh()}
            $state=Get-Content -LiteralPath $ready -Raw|ConvertFrom-Json;$port=$state.port;$health=Health $port
            if($health.scope -ne 'eos-internet-playtest' -or $health.protocol -ne 1){throw 'The online helper did not authenticate with EOS.'}
            $serviceState=@{helper_id=$helper.Id;helper_started=$helper.StartTime.ToUniversalTime().ToString('o');helper_path=$helperExe;helper_hash=$helperHash;config_hash=$configHash;upstream_id=$upstream.Id;upstream_started=$upstream.StartTime.ToUniversalTime().ToString('o');upstream_path=$settings.python;port=$port;ready=$ready;session=$session}|ConvertTo-Json
            $serviceState|Set-Content -LiteralPath (Join-Path $session 'online-service.json') -Encoding UTF8
            $serviceState|Set-Content -LiteralPath $latestFile -Encoding UTF8
            $online=$true
        }catch {
            # Only the children created by this failed startup are stopped.
            if($helper -and -not $helper.HasExited){Stop-Process -Id $helper.Id -ErrorAction SilentlyContinue}
            if($upstream -and -not $upstream.HasExited){Stop-Process -Id $upstream.Id -ErrorAction SilentlyContinue}
            throw
        }
    }
    $env:MKDD_ONLINE_MENU='1';$env:MKDD_ROOM_BACKEND='eos';$env:MKDD_ROOM_PORT=[string]$port
    $env:MKDD_ONLINE_NAME=if($settings.player_name){$settings.player_name}else{'Player'}
    $env:GCN_NET_STATS=$ready+'.stats.json'
    $env:GCN_FRAME_STATS='1'
    $env:GCN_PIPELINE_TRACE='1'
}
$run=Join-Path $data 'game';New-Item -ItemType Directory -Force -Path $run | Out-Null
$env:GCN_CURSOR_ASSET=Join-Path $root 'assets\menu-cursor.png'
$env:MKDD_MENU_KART_ASSET=Join-Path $root 'assets\menu-kart.gctex'
$env:MKDD_MENU_ART=Join-Path $root 'assets\classic'
$arguments=@('--no-input-log','--widescreen','--resolution=1920x1080','--fullscreen','--scale=4')
if($Frames){$arguments+="--frames=$Frames"}
$game=$null;$watcher=$null
try {
    $game=Start-Process -FilePath $settings.game_executable -ArgumentList $arguments -WorkingDirectory $run -WindowStyle Normal -PassThru -RedirectStandardOutput (Join-Path $session 'game-stdout.log') -RedirectStandardError (Join-Path $session 'game-stderr.log')
    $gameState=@{game_id=$game.Id;game_path=$settings.game_executable;session=$session;online=$online}
    $gameState|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $session 'game-session.json') -Encoding UTF8
    if($online) {
        $powershell=Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
        $watcher=Start-Process -FilePath $powershell -ArgumentList @('-NoProfile','-ExecutionPolicy','Bypass','-File',(Quote (Join-Path $root 'Watch-Online.ps1')),'-Session',(Quote $session)) -WorkingDirectory $session -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $session 'watch-stdout.log') -RedirectStandardError (Join-Path $session 'watch-stderr.log')
        $deadline=(Get-Date).AddSeconds(10)
        while(-not (Test-Path -LiteralPath (Join-Path $session 'watch-online.ready'))) {
            if($watcher.HasExited -or (Get-Date) -gt $deadline){throw 'The automatic online shutdown helper did not start. See the session logs.'}
            Start-Sleep -Milliseconds 100;$watcher.Refresh()
        }
        $gameState.watcher_id=$watcher.Id
    }
    $gameState|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $data 'latest-game.json') -Encoding UTF8
}catch {
    if($game -and -not $game.HasExited){Stop-Process -Id $game.Id -ErrorAction SilentlyContinue}
    if($online){& (Join-Path $root 'Stop-Online.ps1') -Session $session}
    throw
}
Write-Host 'Project Tandem launched. Online services close automatically after you exit the game; no separate stop command is needed.'
