# Shared by setup, launch and the standalone updater. PowerShell 5.1 compatible.
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
    $file=Join-Path $Root 'local-data\settings.json'
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
    $metadata=Join-Path $Root 'local-data\game-image.json'
    try {
        if(-not (Test-Path -LiteralPath $metadata -PathType Leaf)){return $null}
        $entry=Get-Content -LiteralPath $metadata -Raw|ConvertFrom-Json
        if($entry.format -ne 1 -or $entry.file -notin @('game-image.iso','game-image.ciso')){return $null}
        $file=Join-Path (Join-Path $Root 'local-data') $entry.file
        if((Test-Path -LiteralPath $file -PathType Leaf) -and (Get-Item -LiteralPath $file).Length -eq $entry.bytes){return $file}
    }catch{}
    return $null
}
function Save-TandemImageCache([string]$Root,[string]$Image) {
    $folder=Join-Path $Root 'local-data';$extension=[IO.Path]::GetExtension($Image).ToLowerInvariant()
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
    $build=Join-Path $Root 'local-data\build-portable'
    if(-not (Test-Path -LiteralPath (Join-Path $build 'CMakeCache.txt'))){return $false}
    return -not ((Test-TandemPathEqual (Get-TandemCacheValue $build 'CMAKE_HOME_DIRECTORY') (Join-Path $Root 'source')) -and
                 (Test-TandemPathEqual (Get-TandemCacheValue $build 'CMAKE_CACHEFILE_DIR') $build))
}
function Assert-TandemClosed([string]$Root) {
    $prefix=[IO.Path]::GetFullPath($Root).TrimEnd('\')+'\'
    if(Get-Process -Name mkdd -ErrorAction SilentlyContinue | Where-Object {$_.Path -and $_.Path.StartsWith($prefix,[StringComparison]::OrdinalIgnoreCase)}) {
        throw 'Close this Project Tandem game before repairing or updating it.'
    }
}
function Save-TandemSettings([string]$Root,$Settings,[string]$GameImage) {
    $values=@{game_image=$GameImage;game_executable=(Join-Path $Root 'local-data\build-portable\mkdd.exe');
              python=(Join-Path $Root 'tools\python\python.exe');source=(Join-Path $Root 'source')}
    foreach($key in $values.Keys){$Settings|Add-Member -NotePropertyName $key -NotePropertyValue $values[$key] -Force}
    if(-not $Settings.PSObject.Properties['player_name']){$Settings|Add-Member -NotePropertyName player_name -NotePropertyValue 'Player'}
    $file=Join-Path $Root 'local-data\settings.json';$temporary=$file+'.tmp'
    $Settings|ConvertTo-Json -Depth 16|Set-Content -LiteralPath $temporary -Encoding UTF8
    Move-Item -LiteralPath $temporary -Destination $file -Force
}
