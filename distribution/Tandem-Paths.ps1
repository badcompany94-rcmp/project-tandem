# Shared by setup, launch and the standalone updater. PowerShell 5.1 compatible.
function Get-TandemControlRoot([string]$Root){return [IO.Path]::GetFullPath((Join-Path $Root 'local-data'))}
function Get-TandemBuildRoot([string]$Root){return Join-Path (Get-TandemControlRoot $Root) 'build-portable'}
function Assert-TandemNoLinks([string]$Path){
    $cursor=[IO.Path]::GetFullPath($Path)
    while($cursor){
        if(Test-Path -LiteralPath $cursor){if((Get-Item -LiteralPath $cursor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Tandem data paths cannot use links or junctions.'}}
        $parent=[IO.Path]::GetDirectoryName($cursor);if($parent -eq $cursor){break};$cursor=$parent
    }
}
function Get-TandemDataRoot([string]$Root){
    $install=[IO.Path]::GetFullPath($Root).TrimEnd('\')
    $default=Get-TandemControlRoot $install
    if(Test-Path -LiteralPath (Join-Path $install 'portable.txt') -PathType Leaf){return $default}
    $file=Join-Path $install 'paths.json'
    if(-not (Test-Path -LiteralPath $file)){return $default}
    try{
        if(-not (Test-Path -LiteralPath $file -PathType Leaf)){throw 'paths.json must be a file.'}
        Assert-TandemNoLinks $file
        if((Get-Item -LiteralPath $file).Length -gt 16KB){throw 'File is too large.'}
        $config=Get-Content -LiteralPath $file -Raw|ConvertFrom-Json
        $value=if($config.data){$config.data}else{$config.data_path}
        if($value -isnot [string] -or [string]::IsNullOrWhiteSpace($value) -or $value -match '[\x00-\x1f]'){throw 'Specify a data folder as a nonempty string.'}
        if($config.data -and $config.data_path -and $config.data -cne $config.data_path){throw 'Use one unambiguous data path.'}
        $value=[Environment]::ExpandEnvironmentVariables($value)
        if($value -match '%[^%]+%'){throw 'An environment variable is not defined.'}
        if($value.StartsWith('\\')){throw 'Choose a local-drive data folder.'}
        if(([IO.Path]::IsPathRooted($value) -and $value -notmatch '^[A-Za-z]:[\\/]') -or $value -match '^[A-Za-z]:[^\\/]'){throw 'Use a full drive path or a path relative to this installation.'}
        $full=Get-TandemFullPath $install $value;$full=$full.TrimEnd('\')
        if($full -eq [IO.Path]::GetPathRoot($full).TrimEnd('\') -or $install.Equals($full,[StringComparison]::OrdinalIgnoreCase) -or
           $install.StartsWith($full+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Choose a dedicated data folder, not a drive root or installation parent.'}
        foreach($reserved in @('source','tools','runtime','assets','config','docs','licenses','mods','.git','local-data')){
            $prefix=Join-Path $install $reserved
            if($full.Equals($prefix,[StringComparison]::OrdinalIgnoreCase) -or $full.StartsWith($prefix+'\',[StringComparison]::OrdinalIgnoreCase)){
                if($full.Equals($default,[StringComparison]::OrdinalIgnoreCase)){return $default}
                throw 'The data folder overlaps game code, tools or update/build storage.'
            }
        }
        Assert-TandemNoLinks $full
        return $full
    }catch{throw ('Invalid paths.json: '+$_.Exception.Message+' Default files were not moved. Fix paths.json or use portable.txt.')}
}
function Assert-TandemUserPath([string]$DataRoot,[string]$Path){
    $base=[IO.Path]::GetFullPath($DataRoot).TrimEnd('\');$full=[IO.Path]::GetFullPath($Path)
    if(-not $full.StartsWith($base+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'A user-data path escapes its configured folder.'}
    Assert-TandemNoLinks $full;return $full
}
function Initialize-TandemDataRoot([string]$Root){
    $data=Get-TandemDataRoot $Root
    Assert-TandemNoLinks $data
    try{
        New-Item -ItemType Directory -Force -Path $data|Out-Null
        $probe=Join-Path $data ('.write-test-'+[guid]::NewGuid().ToString('N'))
        [IO.File]::WriteAllText($probe,'');[IO.File]::Delete($probe)
    }catch{throw ('Cannot write Tandem data at '+$data+'. Choose a writable folder in paths.json. No alternate location was selected.')}
    if(Test-TandemPathEqual $data (Get-TandemControlRoot $Root)){return $data}
    $control=Get-TandemControlRoot $Root;Assert-TandemNoLinks $control
    $idFile=Join-Path $control 'installation-id.txt'
    if(-not (Test-Path -LiteralPath $idFile)){
        try{New-Item -ItemType Directory -Force -Path $control|Out-Null;[IO.File]::WriteAllText($idFile,[guid]::NewGuid().ToString('N'))}
        catch{throw 'Custom data setup needs a writable installation once to record its identity. Run setup with the required permissions.'}
    }
    if((Get-Item -LiteralPath $idFile).Length -gt 64){throw 'The installation identity is invalid.'}
    $id=([IO.File]::ReadAllText($idFile)).Trim();$parsed=[guid]::Empty
    if(-not [guid]::TryParse($id,[ref]$parsed)){throw 'The installation identity is invalid; custom data was not opened.'}
    $ownerFile=Assert-TandemUserPath $data (Join-Path $data '.tandem-owner.json')
    if(Test-Path -LiteralPath $ownerFile){
        if((Get-Item -LiteralPath $ownerFile).Length -gt 4KB){throw 'The user-data owner record is invalid.'}
        $owner=Get-Content -LiteralPath $ownerFile -Raw|ConvertFrom-Json
        if($owner.format -ne 1 -or $owner.id -ne $id){throw 'This data folder belongs to another Tandem installation or has an invalid owner record. Choose a separate folder.'}
        if($owner.root -and -not (Test-TandemPathEqual $owner.root $Root) -and (Test-TandemPathEqual $owner.data $data) -and (Test-Path -LiteralPath $owner.root)){
            throw 'Another installation still owns this data folder. Choose a separate folder for this copy.'
        }
        if((Test-TandemPathEqual $owner.root $Root) -and (Test-TandemPathEqual $owner.data $data)){return $data}
    }
    $value=@{format=1;id=$id;root=[IO.Path]::GetFullPath($Root);data=$data}|ConvertTo-Json
    $temporary=$ownerFile+'.'+[guid]::NewGuid().ToString('N')+'.tmp';[IO.File]::WriteAllText($temporary,$value);Move-Item -LiteralPath $temporary -Destination $ownerFile -Force
    return $data
}
function Select-TandemFile([string]$Title,[string]$Filter,[string]$InitialDirectory) {
    Add-Type -AssemblyName System.Windows.Forms
    $dialog=New-Object Windows.Forms.OpenFileDialog
    $dialog.Title=$Title;$dialog.Filter=$Filter;$dialog.CheckFileExists=$true
    if($InitialDirectory -and (Test-Path -LiteralPath $InitialDirectory -PathType Container)){$dialog.InitialDirectory=$InitialDirectory}
    $owner=New-Object Windows.Forms.Form
    $owner.TopMost=$true;$owner.ShowInTaskbar=$false;$owner.Opacity=0
    try {
        $owner.Show();$owner.Activate()
        if($dialog.ShowDialog($owner) -eq [Windows.Forms.DialogResult]::OK){return $dialog.FileName}
        return $null
    } finally {$dialog.Dispose();$owner.Dispose()}
}
function Get-TandemSettings([string]$Root) {
    $file=Join-Path (Get-TandemDataRoot $Root) 'settings.json'
    if(Test-Path -LiteralPath $file){return (Get-Content -LiteralPath $file -Raw|ConvertFrom-Json)}
    return [pscustomobject]@{player_name='Player'}
}
function Get-TandemFullPath([string]$Root,[string]$Path) {
    if(-not $Path){return $null}
    if([IO.Path]::IsPathRooted($Path)){return [IO.Path]::GetFullPath($Path)}
    return [IO.Path]::GetFullPath((Join-Path $Root $Path))
}
function Resolve-TandemImage([string]$Root,$Settings,[string]$GameImage) {
    if(-not $GameImage){$cached=Get-TandemCachedImage $Root;if($cached){return $cached}}
    $candidates=@()
    if($GameImage){$candidates+=Get-TandemFullPath $Root $GameImage}
    elseif($Settings.game_image) {
        # If the image lived inside the installation, follow it when that entire
        # directory moves. Do not scan unrelated drives or guess by filename.
        $oldImage=Get-TandemFullPath $Root $Settings.game_image
        if($Settings.source -and [IO.Path]::IsPathRooted($Settings.source)) {
            $oldRoot=[IO.Path]::GetFullPath((Split-Path -Parent $Settings.source)).TrimEnd('\')+'\'
            if($oldImage.StartsWith($oldRoot,[StringComparison]::OrdinalIgnoreCase)) {
                $candidates+=Get-TandemFullPath $Root $oldImage.Substring($oldRoot.Length)
            }
        }
        $candidates+=$oldImage
    }
    foreach($candidate in $candidates){if(Test-Path -LiteralPath $candidate -PathType Leaf){return $candidate}}
    Write-Host 'Locate your ISO/CISO: its previous location is unavailable, or this is a new setup.'
    Write-Host 'A file-selection window is opening. Cancel leaves the installation unchanged.'
    return Select-TandemFile 'Locate your supported USA Double Dash ISO/CISO' 'GameCube disc images (*.iso;*.ciso)|*.iso;*.ciso' $Root
}
function Get-TandemCachedImage([string]$Root) {
    $metadata=Join-Path (Get-TandemDataRoot $Root) 'game-image.json'
    try {
        if(-not (Test-Path -LiteralPath $metadata -PathType Leaf)){return $null}
        $entry=Get-Content -LiteralPath $metadata -Raw|ConvertFrom-Json
        if($entry.format -ne 1 -or $entry.file -notin @('game-image.iso','game-image.ciso')){return $null}
        $file=Join-Path (Get-TandemDataRoot $Root) $entry.file
        if((Test-Path -LiteralPath $file -PathType Leaf) -and (Get-Item -LiteralPath $file).Length -eq $entry.bytes){return $file}
    }catch{}
    return $null
}
function Save-TandemImageCache([string]$Root,[string]$Image) {
    $folder=Initialize-TandemDataRoot $Root;$extension=[IO.Path]::GetExtension($Image).ToLowerInvariant()
    if($extension -notin @('.iso','.ciso')){throw 'Choose an ISO or CISO disc image.'}
    $name='game-image'+$extension;$target=Join-Path $folder $name
    if(-not (Test-TandemPathEqual $Image $target)) {
        if((Test-Path -LiteralPath $target) -and ((Get-Item -LiteralPath $target).Attributes -band [IO.FileAttributes]::ReparsePoint)){throw 'The private game-data cache cannot be a linked file.'}
        $temporary=Join-Path $folder ('game-image-'+[guid]::NewGuid().ToString('N')+'.tmp')
        $inputStream=$null;$outputStream=$null
        try {
            $inputStream=[IO.File]::OpenRead($Image);$outputStream=[IO.File]::Create($temporary)
            $buffer=New-Object byte[] (4MB);$copied=[long]0
            while(($read=$inputStream.Read($buffer,0,$buffer.Length)) -gt 0) {
                $outputStream.Write($buffer,0,$read);$copied+=$read
                Show-TandemProgress '2/5 Cache your game data' ([int][Math]::Floor(100*$copied/$inputStream.Length)) "$([Math]::Round($copied/1MB)) / $([Math]::Round($inputStream.Length/1MB)) MB"
            }
            $outputStream.Flush();$outputStream.Dispose();$outputStream=$null;$inputStream.Dispose();$inputStream=$null
            Move-Item -LiteralPath $temporary -Destination $target -Force
        }finally {
            if($outputStream){$outputStream.Dispose()};if($inputStream){$inputStream.Dispose()}
            if(Test-Path -LiteralPath $temporary){Remove-Item -LiteralPath $temporary}
        }
    }
    @{format=1;file=$name;bytes=(Get-Item -LiteralPath $target).Length}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $folder 'game-image.json') -Encoding UTF8
    Show-TandemProgress '2/5 Cache your game data' 100 'Updates will reuse this private copy' -Complete
    return $target
}
function Get-TandemCacheValue([string]$Build,[string]$Key) {
    $file=Join-Path $Build 'CMakeCache.txt'
    if(-not (Test-Path -LiteralPath $file)){return $null}
    $pattern='^'+[regex]::Escape($Key)+':[^=]*=(.*)$'
    # ReadAllLines closes the handle before returning; a lazy ReadLines iterator
    # can retain a Windows file lock when a PowerShell 5.1 foreach returns early.
    foreach($line in [IO.File]::ReadAllLines($file)){if($line -match $pattern){return $Matches[1]}}
    return $null
}
function Test-TandemPathEqual([string]$First,[string]$Second) {
    if(-not $First -or -not $Second){return $false}
    return [IO.Path]::GetFullPath($First).TrimEnd('\').Equals([IO.Path]::GetFullPath($Second).TrimEnd('\'),[StringComparison]::OrdinalIgnoreCase)
}
function Test-TandemBuildMoved([string]$Root) {
    $build=Get-TandemBuildRoot $Root
    if(-not (Test-Path -LiteralPath (Join-Path $build 'CMakeCache.txt'))){return $false}
    return -not ((Test-TandemPathEqual (Get-TandemCacheValue $build 'CMAKE_HOME_DIRECTORY') (Join-Path $Root 'source')) -and
                 (Test-TandemPathEqual (Get-TandemCacheValue $build 'CMAKE_CACHEFILE_DIR') $build))
}
function Assert-TandemClosed([string]$Root) {
    $transaction=Join-Path $Root 'local-data/update-transaction.json'
    if(Test-Path -LiteralPath $transaction) {
        try {$owner=Get-Content -LiteralPath $transaction -Raw|ConvertFrom-Json}
        catch {throw 'An interrupted update needs recovery. Open ProjectTandem.exe.'}
        $started=(Get-Process -Id $PID).StartTime.ToUniversalTime().ToString('o')
        if(-not (($owner.OwnerId -eq $PID -and $owner.OwnerStarted -eq $started) -or
                 ($owner.BuildOwnerId -eq $PID -and $owner.BuildOwnerStarted -eq $started))) {
            throw 'An update is running or needs recovery. Open ProjectTandem.exe and finish it before playing or repairing.'
        }
    }
    $prefix=[IO.Path]::GetFullPath($Root).TrimEnd('\')+'\'
    if(Get-Process -Name mkdd -ErrorAction SilentlyContinue | Where-Object {$_.Path -and $_.Path.StartsWith($prefix,[StringComparison]::OrdinalIgnoreCase)}) {
        throw 'Close this Project Tandem game before repairing or updating it.'
    }
}
function Save-TandemSettings([string]$Root,$Settings,[string]$GameImage) {
    $values=@{game_image=$GameImage;game_executable=(Join-Path (Get-TandemBuildRoot $Root) 'mkdd.exe');
              python=(Join-Path $Root 'tools\python\python.exe');source=(Join-Path $Root 'source')}
    foreach($key in $values.Keys){$Settings|Add-Member -NotePropertyName $key -NotePropertyValue $values[$key] -Force}
    if(-not $Settings.PSObject.Properties['player_name']){$Settings|Add-Member -NotePropertyName player_name -NotePropertyValue 'Player'}
    $file=Join-Path (Get-TandemDataRoot $Root) 'settings.json';$temporary=$file+'.tmp'
    $Settings|ConvertTo-Json -Depth 16|Set-Content -LiteralPath $temporary -Encoding UTF8
    Move-Item -LiteralPath $temporary -Destination $file -Force
}
