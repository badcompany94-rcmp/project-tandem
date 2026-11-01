# Shared launcher state and child-process arguments. Windows PowerShell 5.1.
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
        $manifest=Get-Content -LiteralPath (Join-Path $rootPath 'package-manifest.json') -Raw -ErrorAction Stop|ConvertFrom-Json -ErrorAction Stop
        if(-not $manifest.compatibility -or -not $manifest.files){throw 'Invalid package manifest.'}
        $result.Release=[string]$manifest.release;$result.Compatibility=[string]$manifest.compatibility
        $result.PackageValid=$true
        $identityFile=Join-Path $rootPath 'local-data/build-portable/identity/build-identity.json'
        if(Test-Path -LiteralPath $identityFile -PathType Leaf){$result.Installed=[string](Get-Content -LiteralPath $identityFile -Raw -ErrorAction Stop|ConvertFrom-Json -ErrorAction Stop).id}
        $exe=Join-Path $rootPath 'local-data/build-portable/mkdd.exe'
        $result.Ready=($result.Installed -eq $result.Compatibility) -and (Test-Path -LiteralPath $exe -PathType Leaf) -and
            (Test-Path -LiteralPath (Join-Path $rootPath 'local-data/settings.json') -PathType Leaf) -and
            -not (Test-Path -LiteralPath (Join-Path $rootPath 'local-data/update-pending.json'))
        $result.Detail=if($result.Ready){'Ready to play. LAN and Internet use the same game.'}else{'Setup or a local rebuild is needed. Your saves and controls are kept.'}
    }catch {$result.Detail='Package information is missing or unreadable. Extract the complete download again.'}
    return [pscustomobject]$result
}
function New-TandemLauncherJob([string]$Root,[ValidateSet('Play','Setup','Repair')][string]$Action,[string]$GameImage,[string]$Worker) {
    $rootPath=[IO.Path]::GetFullPath($Root)
    $folder=Join-Path $rootPath ('local-data/launcher-logs/'+(Get-Date -Format 'yyyyMMdd-HHmmss-fff')+'-'+[guid]::NewGuid().ToString('N').Substring(0,6))
    New-Item -ItemType Directory -Path $folder -Force | Out-Null
    $arguments=@('-NoProfile','-STA','-ExecutionPolicy','Bypass','-File',$Worker,'-Root',$rootPath,'-Action',$Action,'-Result',(Join-Path $folder 'result.json'))
    if($GameImage){$arguments+=@('-GameImage',$GameImage)}
    $ps=Join-Path $env:SystemRoot 'System32/WindowsPowerShell/v1.0/powershell.exe'
    $process=Start-Process -FilePath $ps -ArgumentList (Join-TandemArguments $arguments) -WorkingDirectory $rootPath -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $folder 'stdout.log') -RedirectStandardError (Join-Path $folder 'stderr.log')
    return [pscustomobject]@{Process=$process;Folder=$folder;Result=(Join-Path $folder 'result.json');Action=$Action}
}
