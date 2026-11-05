. (Join-Path $PSScriptRoot 'Tandem-Paths.ps1')
# Shared launcher state and child-process arguments. Windows PowerShell 5.1.
$script:TandemManifestCache=@{}
function Get-TandemLauncherPreferences([string]$Path) {
    $values=@{check_at_startup=$true;close_on_play=$false}
    try {
        if(Test-Path -LiteralPath $Path){
            $saved=Get-Content -LiteralPath $Path -Raw|ConvertFrom-Json
            foreach($key in @('check_at_startup','close_on_play')){
                if($saved.$key -is [bool]){$values[$key]=$saved.$key}
            }
        }
    }catch{}
    return $values
}
function Save-TandemLauncherPreferences([string]$Path,[bool]$Check,[bool]$CloseOnPlay) {
    $values=@{}
    if(Test-Path -LiteralPath $Path){
        try{(Get-Content -LiteralPath $Path -Raw|ConvertFrom-Json).PSObject.Properties|ForEach-Object{$values[$_.Name]=$_.Value}}catch{}
    }
    $values.check_at_startup=$Check;$values.close_on_play=$CloseOnPlay
    Write-TandemJson $Path $values
}
function Test-TandemCloseAfterPlay([bool]$Enabled,[string]$Action,[bool]$Succeeded) {
    return $Enabled -and $Succeeded -and $Action -eq 'Play'
}
function Join-TandemArguments([string[]]$Values) {
    # CommandLineToArgvW quoting, including embedded quotes and trailing slashes.
    return (($Values | ForEach-Object {
        $v=[regex]::Replace($_,'(\\*)"','$1$1\"')
        '"'+[regex]::Replace($v,'(\\+)$','$1$1')+'"'
    }) -join ' ')
}
function Get-TandemLauncherState([string]$Root) {
    $rootPath=[IO.Path]::GetFullPath($Root)
    $result=[ordered]@{Root=$rootPath;Release='Project Tandem';Compatibility='Unknown';Installed='Not built';Ready=$false;PackageValid=$false;Detail='Extract the complete Tandem download into this folder.'}
    try {
        $manifestFile=Get-Item -LiteralPath (Join-Path $rootPath 'package-manifest.json') -ErrorAction Stop
        $stamp=$manifestFile.LastWriteTimeUtc.Ticks.ToString()+':'+$manifestFile.Length
        $cached=$script:TandemManifestCache[$rootPath]
        if($cached -and $cached.Stamp -eq $stamp){$manifest=$cached.Summary}
        else {
            $parsed=Get-Content -LiteralPath $manifestFile.FullName -Raw -ErrorAction Stop|ConvertFrom-Json -ErrorAction Stop
            if(-not $parsed.compatibility -or -not $parsed.files){throw 'Invalid package manifest.'}
            $manifest=[pscustomobject]@{release=[string]$parsed.release;compatibility=[string]$parsed.compatibility}
            $script:TandemManifestCache[$rootPath]=[pscustomobject]@{Stamp=$stamp;Summary=$manifest}
        }
        $result.Release=[string]$manifest.release;$result.Compatibility=[string]$manifest.compatibility
        $result.PackageValid=$true
        $identityFile=Join-Path $rootPath 'local-data/build-portable/identity/build-identity.json'
        if(Test-Path -LiteralPath $identityFile -PathType Leaf){$result.Installed=[string](Get-Content -LiteralPath $identityFile -Raw -ErrorAction Stop|ConvertFrom-Json -ErrorAction Stop).id}
        $exe=Join-Path $rootPath 'local-data/build-portable/mkdd.exe'
        $result.Ready=($result.Installed -eq $result.Compatibility) -and (Test-Path -LiteralPath $exe -PathType Leaf) -and
            (Test-Path -LiteralPath (Join-Path (Get-TandemDataRoot $rootPath) 'settings.json') -PathType Leaf) -and
            -not (Test-Path -LiteralPath (Join-Path $rootPath 'local-data/update-pending.json'))
        $result.Detail=if($result.Ready){'Ready to play. LAN and Internet use the same game.'}else{'Setup or a local rebuild is needed. Your saves and controls are kept.'}
    }catch {$result.Ready=$false;$result.Detail=if($result.PackageValid){$_.Exception.Message}else{'Package information is missing or unreadable. Extract the complete download again.'}}
    $result.Running=$false;$result.UpdatePending=$false;$result.UpdateRecovery=$false
    try {$prefix=$rootPath.TrimEnd('\')+'\';$result.Running=@(Get-Process -Name mkdd -ErrorAction SilentlyContinue|Where-Object {$_.Path -and $_.Path.StartsWith($prefix,[StringComparison]::OrdinalIgnoreCase)}).Count -gt 0}catch{}
    if(Get-Command Get-TandemUpdateStatus -ErrorAction SilentlyContinue) {
        $update=Get-TandemUpdateStatus $rootPath;$result.UpdatePending=$update.Pending;$result.UpdateRecovery=$update.Recovery
        if($update.Pending){$result.Ready=$false;$result.Detail=if($update.Recovery){'An interrupted update needs recovery.'}else{'An update is in progress. Return to its window.'}}
    }
    return [pscustomobject]$result
}
function New-TandemLauncherJob([string]$Root,[ValidateSet('Play','Setup','Repair','CheckUpdate')][string]$Action,[string]$GameImage,[string]$Worker) {
    $rootPath=[IO.Path]::GetFullPath($Root)
    $folder=Join-Path (Initialize-TandemDataRoot $rootPath) ('launcher-logs/'+(Get-Date -Format 'yyyyMMdd-HHmmss-fff')+'-'+[guid]::NewGuid().ToString('N').Substring(0,6))
    New-Item -ItemType Directory -Path $folder -Force | Out-Null
    $arguments=@('-NoProfile','-STA','-ExecutionPolicy','Bypass','-File',$Worker,'-Root',$rootPath,'-Action',$Action,'-Result',(Join-Path $folder 'result.json'))
    if($GameImage){$arguments+=@('-GameImage',$GameImage)}
    $ps=Join-Path $env:SystemRoot 'System32/WindowsPowerShell/v1.0/powershell.exe'
    $process=Start-Process -FilePath $ps -ArgumentList (Join-TandemArguments $arguments) -WorkingDirectory $rootPath -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $folder 'stdout.log') -RedirectStandardError (Join-Path $folder 'stderr.log')
    return [pscustomobject]@{Process=$process;Folder=$folder;Result=(Join-Path $folder 'result.json');Action=$Action}
}
