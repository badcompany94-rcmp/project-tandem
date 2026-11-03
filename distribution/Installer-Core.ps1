# First installation uses the same official release, digest and archive checks
# as updates. No game data or compiler is embedded in the downloadable launcher.
. (Join-Path $PSScriptRoot 'Updater-Core.ps1')
. (Join-Path $PSScriptRoot 'Launcher-Core.ps1')
function Get-TandemInstallRelease {
    $best=$null
    foreach($release in @(Get-TandemReleaseJson ('https://api.github.com/repos/'+$script:TandemRepository+'/releases?per_page=100'))){
        $version=Get-TandemVersion ([string]$release.tag_name)
        if(-not $version -or ($best -and $version -le [version]$best.Version)){continue}
        $candidate=ConvertTo-TandemCandidate $release
        if($candidate){$best=$candidate}
    }
    if(-not $best){throw 'No supported Windows release was found. Please try again later.'}
    return $best
}
function Initialize-TandemInstall([string]$Root,[string]$Bootstrap){
    $full=[IO.Path]::GetFullPath($Root).TrimEnd('\')
    if($full -notmatch '^[A-Za-z]:\\' -or $full -eq [IO.Path]::GetPathRoot($full).TrimEnd('\')){throw 'Choose a dedicated folder on a local drive.'}
    Assert-TandemNoLinks $full
    $receipt=Join-Path $full '.tandem-install.json'
    if(Test-Path -LiteralPath $full){
        if(-not (Test-Path -LiteralPath $full -PathType Container)){throw 'Choose a folder, not a file.'}
        $children=@(Get-ChildItem -LiteralPath $full -Force)
        if(Test-Path -LiteralPath $receipt -PathType Leaf){
            if((Get-Item -LiteralPath $receipt).Length -gt 16KB){throw 'The installation record is invalid.'}
            $record=Read-TandemJson $receipt
            if($record.format -ne 1 -or -not (Test-TandemPathEqual ([string]$record.root) $full)){throw 'The installation record belongs to another folder.'}
        }elseif($children.Count -and -not ($children.Count -eq 1 -and (Test-TandemPathEqual $children[0].FullName $Bootstrap))){
            throw 'Choose an empty folder for this installation. Existing Tandem copies have their own launcher.'
        }
    }
    [IO.Directory]::CreateDirectory($full)|Out-Null
    $lock=Get-TandemUpdateLock $full
    try {
        if(-not (Test-Path -LiteralPath $receipt)) {Write-TandemJson $receipt @{format=1;root=$full;phase='Downloading'}}
    }catch{$lock.Dispose();throw}
    return $lock
}
function Install-TandemPackage([string]$Root,$Candidate,[scriptblock]$Progress){
    $folder=Assert-TandemSafePath $Root (Join-Path $Root 'local-data/installer')
    [IO.Directory]::CreateDirectory($folder)|Out-Null
    $archive=Save-TandemAsset $Candidate.Full $folder $Progress
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $zip=[IO.Compression.ZipFile]::OpenRead($archive)
    try{$expanded=[long]0;foreach($entry in $zip.Entries){$expanded+=$entry.Length}}finally{$zip.Dispose()}
    $drive=New-Object IO.DriveInfo([IO.Path]::GetPathRoot($Root))
    if($drive.AvailableFreeSpace -lt (2*$expanded+3GB)){throw 'Not enough free space for installation, your disc cache and the local build. Free some space or choose another drive.'}
    $stage=Join-Path $folder ('staged-'+[guid]::NewGuid().ToString('N'))
    # A fresh install always requires the full archive; there is no old tool cache.
    $manifest=Expand-TandemUpdate $archive $stage $Root $Candidate $Progress
    $files=@($manifest.files|ForEach-Object {[string]$_.path})+@('package-manifest.json')
    $index=0
    foreach($name in $files){
        $from=Assert-TandemSafePath $stage (Join-Path $stage $name)
        if(-not [IO.File]::Exists($from)){throw 'The full installation package is incomplete.'}
        Copy-TandemAtomic $from (Assert-TandemSafePath $Root (Join-Path $Root $name))
        if(++$index % 100 -eq 0){Send-TandemProgress $Progress 'Installing files' "$index / $($files.Count)" ([int](100*$index/$files.Count))}
    }
    Write-TandemJson (Join-Path $Root '.tandem-install.json') @{format=1;root=[IO.Path]::GetFullPath($Root).TrimEnd('\');phase='FilesReady';tag=$Candidate.Tag}
    # Stage is owned by this invocation, inside the verified installation root.
    $stage=Assert-TandemSafePath $folder $stage
    Remove-Item -LiteralPath $stage -Recurse -Force
    Send-TandemProgress $Progress 'Files ready' 'Choose your Double Dash disc image to finish setup.' 100
    return $manifest
}
function Complete-TandemInstall([string]$Root,[string]$Bootstrap,[string]$Registration){
    if(-not (Get-TandemLauncherState $Root).Ready){throw 'Setup has not finished. Select your disc image and try again.'}
    $target=Assert-TandemSafePath $Root (Join-Path $Root 'ProjectTandem.exe')
    if(-not (Test-TandemPathEqual $Bootstrap $target)){Copy-TandemAtomic $Bootstrap $target}
    Write-TandemJson (Join-Path $Root '.tandem-install.json') @{format=1;root=[IO.Path]::GetFullPath($Root).TrimEnd('\');phase='Ready';tag=(Get-TandemInstalledTag $Root)}
    # This remembers the installation for the originally downloaded EXE too.
    Assert-TandemNoLinks $Registration
    [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($Registration))|Out-Null
    $temporary=$Registration+'.'+[guid]::NewGuid().ToString('N')+'.tmp'
    [IO.File]::WriteAllText($temporary,[IO.Path]::GetFullPath($Root),(New-Object Text.UTF8Encoding($false)))
    try{if([IO.File]::Exists($Registration)){[IO.File]::Replace($temporary,$Registration,[System.Management.Automation.Language.NullString]::Value)}else{[IO.File]::Move($temporary,$Registration)}}
    finally{if([IO.File]::Exists($temporary)){[IO.File]::Delete($temporary)}}
}
function New-TandemDesktopShortcut([string]$Root,[string]$Desktop){
    $launcher=Join-Path $Root 'ProjectTandem.exe'
    if(-not [IO.File]::Exists($launcher)){throw 'The launcher is not installed yet.'}
    [IO.Directory]::CreateDirectory($Desktop)|Out-Null
    $shell=New-Object -ComObject WScript.Shell
    $path=Join-Path $Desktop 'Project Tandem.lnk';$number=1
    while(Test-Path -LiteralPath $path){
        $existing=$shell.CreateShortcut($path)
        if(Test-TandemPathEqual $existing.TargetPath $launcher){return $path}
        $path=Join-Path $Desktop ('Project Tandem ('+(++$number)+').lnk')
    }
    $shortcut=$shell.CreateShortcut($path);$shortcut.TargetPath=$launcher;$shortcut.WorkingDirectory=$Root
    $shortcut.IconLocation=(Join-Path $Root 'source/distribution/assets/project-tandem.ico')+',0'
    $shortcut.Description='Project Tandem - artwork by POOTERMAN (deviantart.com/pooterman)'
    $shortcut.Save();return $path
}
function Get-TandemSteamShortcutInfo([string]$Root){
    # Steam itself registers this Games-menu route. It requires the player to
    # confirm in Steam; do not edit shortcuts.vdf or restart a running client.
    return [pscustomobject]@{Uri='steam://open/addnonsteamgame';Launcher=(Join-Path ([IO.Path]::GetFullPath($Root)) 'ProjectTandem.exe');
        Instructions='In Steam, click Browse, paste the copied launcher path into File name, then click Open and Add Selected Programs.'}
}
function ConvertTo-TandemInstallProgress([string]$Line){
    if($Line -notmatch '^([1-5]/5 (?:Verify package|Check disc|Cache your game data|Configure|Build game|Final checks))\s+(?:(\d+)%\s*)?(.*)$'){return $null}
    return [pscustomobject]@{Stage=$Matches[1];Percent=$(if($Matches[2]){[int]$Matches[2]}else{-1});Detail=$Matches[3]}
}
