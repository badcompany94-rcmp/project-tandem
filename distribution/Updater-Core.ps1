# Automatic Windows updates. Public GitHub releases only; PowerShell 5.1/.NET 4.
$script:TandemRepository='badcompany94-rcmp/project-tandem'
function Read-TandemJson([string]$Path) {
    for($attempt=0;$attempt -lt 5;$attempt++) {
        try {
            $stream=[IO.File]::Open($Path,[IO.FileMode]::Open,[IO.FileAccess]::Read,([IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete))
            $reader=New-Object IO.StreamReader($stream)
            try {$text=$reader.ReadToEnd()}finally{$reader.Dispose()}
            return ($text|ConvertFrom-Json)
        }catch [IO.IOException] {if($attempt -eq 4){throw};Start-Sleep -Milliseconds 25}
    }
}
function Write-TandemJson([string]$Path,$Value) {
    $temporary=$Path+'.'+[guid]::NewGuid().ToString('N')+'.tmp'
    [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($Path))|Out-Null
    try {
        [IO.File]::WriteAllText($temporary,($Value|ConvertTo-Json -Depth 24),(New-Object Text.UTF8Encoding($false)))
        if([IO.File]::Exists($Path)){[IO.File]::Replace($temporary,$Path,[System.Management.Automation.Language.NullString]::Value)}else{[IO.File]::Move($temporary,$Path)}
    }finally {if([IO.File]::Exists($temporary)){[IO.File]::Delete($temporary)}}
}
function Get-TandemHash([string]$Path) {
    $stream=[IO.File]::OpenRead($Path);$hash=[Security.Cryptography.SHA256]::Create()
    try {return ([BitConverter]::ToString($hash.ComputeHash($stream))).Replace('-','').ToLowerInvariant()}
    finally {$hash.Dispose();$stream.Dispose()}
}
function Get-TandemVersion([string]$Tag) {
    if($Tag -notmatch '^v(\d+)\.(\d+)\.(\d+)(?:-beta\.(\d+))?$'){return $null}
    try {
        $beta=if($Matches[4]){[int]$Matches[4]}else{[int]::MaxValue}
        return New-Object Version ([int]$Matches[1]),([int]$Matches[2]),([int]$Matches[3]),$beta
    }catch {return $null}
}
function Get-TandemInstalledTag([string]$Root) {
    $manifest=Get-Content -LiteralPath (Join-Path $Root 'package-manifest.json') -Raw|ConvertFrom-Json
    if($manifest.release_tag -and (Get-TandemVersion $manifest.release_tag)){return [string]$manifest.release_tag}
    if([string]$manifest.release -match '^Public Beta (\d+)(?:\b|$)'){return 'v0.1.0-beta.'+$Matches[1]}
    throw 'This installation has no recognized release version. Run setup from a current full download.'
}
function Assert-TandemUrl([uri]$Uri,[switch]$Asset,[switch]$Redirect) {
    if($Uri.Scheme -ne 'https' -or $Uri.UserInfo -or -not $Uri.IsDefaultPort){throw 'The update address is not a supported HTTPS address.'}
    $repo='/'+$script:TandemRepository+'/'
    $valid=if($Redirect){$Uri.Host -in @('release-assets.githubusercontent.com','objects.githubusercontent.com')}
        elseif($Asset){$Uri.Host -eq 'github.com' -and $Uri.AbsolutePath.StartsWith($repo+'releases/download/',[StringComparison]::Ordinal)}
        else {$Uri.Host -eq 'api.github.com' -and $Uri.AbsolutePath.StartsWith('/repos'+$repo+'releases',[StringComparison]::Ordinal)}
    if(-not $valid){throw 'The updater only accepts the official Project Tandem GitHub release service.'}
}
function Open-TandemResponse([string]$Url,[switch]$Asset) {
    $uri=[uri]$Url;Assert-TandemUrl $uri -Asset:$Asset
    [Net.ServicePointManager]::SecurityProtocol=[Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12
    for($i=0;$i -lt 6;$i++) {
        $request=[Net.HttpWebRequest]::Create($uri);$request.AllowAutoRedirect=$false
        $request.UserAgent='Project-Tandem-Launcher/1';$request.Timeout=15000;$request.ReadWriteTimeout=15000
        $request.Accept=if($Asset){'application/octet-stream'}else{'application/vnd.github+json'}
        if(-not $Asset){$request.Headers['X-GitHub-Api-Version']='2026-03-10'}
        try {$response=$request.GetResponse()}
        catch [Net.WebException] {
            $reply=$_.Exception.Response
            if($reply -and [int]$reply.StatusCode -in @(403,429)){$reply.Dispose();throw 'GitHub is limiting update checks. Try again later; your installed game is unchanged.'}
            if($reply){$reply.Dispose()};throw 'Could not reach GitHub. Check your connection and try again; your installed game is unchanged.'
        }
        if([int]$response.StatusCode -in @(301,302,303,307,308)) {
            try {$next=New-Object Uri($uri,$response.Headers['Location'])}finally{$response.Dispose()}
            # API metadata does not need redirects. Asset redirects stay on GitHub's release storage.
            if(-not $Asset){throw 'Unexpected redirect while checking releases.'}
            Assert-TandemUrl $next -Redirect;$uri=$next;continue
        }
        return $response
    }
    throw 'Too many redirects while downloading the update.'
}
function Get-TandemReleaseJson([string]$Url) {
    $response=Open-TandemResponse $Url
    try {
        if($response.ContentLength -gt 16MB){throw 'The release response is too large.'}
        $reader=New-Object IO.StreamReader($response.GetResponseStream())
        try {$text=$reader.ReadToEnd();if($text.Length -gt 16MB){throw 'The release response is too large.'};return ($text|ConvertFrom-Json)}finally{$reader.Dispose()}
    }finally {$response.Dispose()}
}
function ConvertTo-TandemCandidate($Release) {
    $version=Get-TandemVersion ([string]$Release.tag_name)
    if(-not $version -or $Release.draft -or -not $Release.published_at){return $null}
    $assets=@($Release.assets|Where-Object {$_.state -eq 'uploaded'})
    $cumulative=@($assets|Where-Object {$_.name -match '^Project-Tandem-Public-Beta-\d+-Cumulative-Update\.zip$' -or $_.name -eq 'Project-Tandem-Windows-Update.zip'})
    $full=@($assets|Where-Object {$_.name -match '^Project-Tandem-Windows-Public-Beta-\d+\.zip$' -or $_.name -eq 'Project-Tandem-Windows.zip'})
    if($full.Count -ne 1 -or $cumulative.Count -gt 1){return $null}
    foreach($a in @($full)+@($cumulative)) {
        Assert-TandemUrl ([uri]$a.browser_download_url) -Asset
        $expected='/'+$script:TandemRepository+'/releases/download/'+$Release.tag_name+'/'
        if(-not ([uri]$a.browser_download_url).AbsolutePath.StartsWith($expected,[StringComparison]::Ordinal)){throw 'The release asset does not match its version.'}
        if([long]$a.size -le 0 -or [long]$a.size -gt 2GB){throw 'The update download size is invalid.'}
    }
    return [pscustomobject]@{Tag=[string]$Release.tag_name;Version=$version.ToString();Name=[string]$Release.name;
        Url=('https://github.com/'+$script:TandemRepository+'/releases/tag/'+$Release.tag_name);
        Prerelease=[bool]$Release.prerelease;Full=$full[0];Cumulative=if($cumulative.Count){$cumulative[0]}else{$null};
        Checksums=@($assets|Where-Object {$_.name -eq 'SHA256SUMS.txt'})|Select-Object -First 1}
}
function Get-TandemUpdate([string]$Root) {
    $current=Get-TandemInstalledTag $Root;$version=Get-TandemVersion $current
    $releases=@(Get-TandemReleaseJson ('https://api.github.com/repos/'+$script:TandemRepository+'/releases?per_page=100'))
    $best=$null
    foreach($r in $releases) {
        $rv=Get-TandemVersion ([string]$r.tag_name)
        if(-not $rv -or $rv -le $version -or ($best -and $rv -le [version]$best.Version)){continue}
        $candidate=ConvertTo-TandemCandidate $r;if($candidate){$best=$candidate}
    }
    return [pscustomobject]@{Available=($null -ne $best);InstalledTag=$current;Candidate=$best;CheckedAt=[DateTime]::UtcNow.ToString('o')}
}
function Get-TandemUpdateStatus([string]$Root) {
    $file=Join-Path $Root 'local-data/update-transaction.json'
    if(-not [IO.File]::Exists($file)){return [pscustomobject]@{Pending=$false;Active=$false;Recovery=$false;Phase=''}}
    try {
        $record=Read-TandemJson $file
        $active=$false
        try {$p=Get-Process -Id ([int]$record.OwnerId) -ErrorAction Stop;$active=($p.StartTime.ToUniversalTime().ToString('o') -eq $record.OwnerStarted)}catch{}
        return [pscustomobject]@{Pending=$true;Active=$active;Recovery=(-not $active);Phase=[string]$record.Phase}
    }catch {return [pscustomobject]@{Pending=$true;Active=$false;Recovery=$true;Phase='Recovery needed'}}
}
function Assert-TandemSafePath([string]$Root,[string]$Path) {
    $rootPath=[IO.Path]::GetFullPath($Root).TrimEnd('\');$full=[IO.Path]::GetFullPath($Path)
    if(-not $full.StartsWith($rootPath+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'An update path is outside its installation.'}
    $cursor=$full
    while($cursor -and $cursor.Length -ge $rootPath.Length) {
        if([IO.File]::Exists($cursor) -or [IO.Directory]::Exists($cursor)) {
            if(([IO.File]::GetAttributes($cursor) -band [IO.FileAttributes]::ReparsePoint) -ne 0){throw 'Updates cannot replace linked files or folders. Use a normal extracted installation.'}
        }
        if($cursor -eq $rootPath){break};$cursor=[IO.Path]::GetDirectoryName($cursor)
    }
    return $full
}
function Get-TandemPackagePath([string]$Name,[switch]$Generated) {
    $name=$Name.Replace('\','/')
    if(-not $name -or $name.StartsWith('/') -or $name.Length -gt 220 -or $name -match '[:\x00-\x1f]'){throw 'An update contains an invalid file path.'}
    $parts=$name.Split('/')
    foreach($part in $parts) {
        if(-not $part -or $part -in @('.','..') -or $part -match '[. ]$|^(?i:CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\.|$)'){throw 'An update contains an unsafe Windows file path.'}
        if($part -in @('local-data','saves','.git','gen','disc','eos-cache','private-auth','__pycache__')){throw 'An update attempts to replace private data.'}
    }
    if([IO.Path]::GetExtension($name) -in @('.iso','.ciso','.rvz','.gcz','.gcm','.dol','.raw','.pem','.key') -or [IO.Path]::GetFileName($name) -ieq 'mkdd.exe'){throw 'An update contains game data or a private file.'}
    if($parts.Count -gt 1) {
        if($parts[0] -notin @('source','docs','licenses','assets','config','runtime','tools')){throw 'An update contains an unsupported folder.'}
        if($parts[0] -eq 'source' -and [IO.Path]::GetExtension($name) -in @('.exe','.dll','.obj','.o','.lib','.a','.pdb')){throw 'An update contains compiled source artifacts.'}
    }elseif($name -notin @('package-manifest.json','update-release.json','LICENSE','EULA.txt') -and
        [IO.Path]::GetExtension($name) -notin @('.ps1','.cmd','.md','.txt','.json') -and
        -not ($Generated -and $name -in @('ProjectTandem.exe','Project Tandem.lnk'))) {throw 'An update contains an unsupported root file.'}
    return $name
}
function Send-TandemProgress([scriptblock]$Progress,[string]$Stage,[string]$Detail,[int]$Percent=-1) {
    Write-Output ($Stage+': '+$Detail)|Out-Host
    if($Progress){& $Progress $Stage $Detail $Percent}
}
function Save-TandemAsset($Asset,[string]$Folder,[scriptblock]$Progress) {
    if($Asset.name -notmatch '^[A-Za-z0-9_.-]+$'){throw 'Invalid update filename.'}
    $path=Join-Path $Folder $Asset.name
    $digest=[string]$Asset.digest
    if($digest -notmatch '^sha256:([a-fA-F0-9]{64})$'){throw 'This release has no SHA-256 asset digest. Download it manually from GitHub instead.'}
    $expected=$Matches[1].ToLowerInvariant()
    if([IO.File]::Exists($path) -and (Get-Item -LiteralPath $path).Length -eq [long]$Asset.size -and (Get-TandemHash $path) -eq $expected){return $path}
    $part=$path+'.part'
    for($attempt=1;$attempt -le 2;$attempt++) {
        $response=$null;$inputStream=$null;$outputStream=$null
        try {
            $response=Open-TandemResponse ([string]$Asset.browser_download_url) -Asset
            $inputStream=$response.GetResponseStream();$outputStream=[IO.File]::Create($part)
            $buffer=New-Object byte[] 131072;$received=[long]0;$lastPercent=-1
            while(($count=$inputStream.Read($buffer,0,$buffer.Length)) -gt 0) {
                $received+=$count;if($received -gt [long]$Asset.size){throw 'The update is larger than its published size.'}
                $outputStream.Write($buffer,0,$count);$percent=[int](100*$received/[long]$Asset.size)
                if($percent -ne $lastPercent){Send-TandemProgress $Progress 'Downloading' (('{0:N1} / {1:N1} MB' -f ($received/1MB),([long]$Asset.size/1MB))) $percent;$lastPercent=$percent}
            }
            $outputStream.Dispose();$outputStream=$null;$inputStream.Dispose();$inputStream=$null;$response.Dispose();$response=$null
            if($received -ne [long]$Asset.size){throw 'The update download was incomplete.'}
            Send-TandemProgress $Progress 'Verifying' 'Checking the download SHA-256.'
            if((Get-TandemHash $part) -ne $expected){throw 'The downloaded update failed its SHA-256 check.'}
            if([IO.File]::Exists($path)){[IO.File]::Delete($path)};[IO.File]::Move($part,$path);return $path
        }catch {
            if($_.Exception -is [OperationCanceledException] -or $attempt -eq 2){throw}
            Send-TandemProgress $Progress 'Retrying' 'The download failed; retrying once.'
        }finally {
            if($outputStream){$outputStream.Dispose()};if($inputStream){$inputStream.Dispose()};if($response){$response.Dispose()}
            if([IO.File]::Exists($part)){[IO.File]::Delete($part)}
        }
    }
}
function Expand-TandemUpdate([string]$Archive,[string]$Stage,[string]$Root,$Candidate,[scriptblock]$Progress) {
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $zip=[IO.Compression.ZipFile]::OpenRead($Archive)
    try {
        $entries=@{};$total=[long]0
        if($zip.Entries.Count -gt 30000){throw 'The update has too many files.'}
        foreach($entry in $zip.Entries) {
            $name=$entry.FullName.Replace('\','/');if($name.EndsWith('/')){$null=Get-TandemPackagePath ($name.TrimEnd('/')+'/__directory__');continue}
            $name=Get-TandemPackagePath $name
            if($entries.ContainsKey($name)){throw 'The update contains duplicate file paths.'}
            if((([long]$entry.ExternalAttributes -shr 16) -band 0xf000) -eq 0xa000){throw 'The update contains a symbolic link.'}
            $total+=$entry.Length;if($entry.Length -gt 1GB -or $total -gt 4GB){throw 'The expanded update exceeds its size limit.'}
            $entries[$name]=$entry
        }
        if(-not $entries.ContainsKey('package-manifest.json')){throw 'The update has no package manifest.'}
        $manifestEntry=$entries['package-manifest.json'];if($manifestEntry.Length -gt 16MB){throw 'The package manifest is too large.'}
        $reader=New-Object IO.StreamReader($manifestEntry.Open());try{$manifest=$reader.ReadToEnd()|ConvertFrom-Json}finally{$reader.Dispose()}
        if($manifest.minimum_updater -and [int]$manifest.minimum_updater -gt 1){throw 'This release needs a newer installer. Download its full package from GitHub.'}
        if(-not $manifest.public_beta -or $manifest.includes_game_data -or [string]$manifest.compatibility -notmatch '^[a-f0-9]{16}$'){throw 'The update is not a supported public player package.'}
        $tag=[string]$manifest.release_tag
        if(-not $tag -and [string]$manifest.release -match '^Public Beta (\d+)(?:\b|$)'){$tag='v0.1.0-beta.'+$Matches[1]}
        if($tag -ne $Candidate.Tag){throw 'The package version does not match the selected release.'}
        $files=@{};foreach($file in $manifest.files) {
            $name=Get-TandemPackagePath ([string]$file.path)
            if($name -eq 'package-manifest.json' -or $files.ContainsKey($name) -or [string]$file.sha256 -notmatch '^[a-f0-9]{64}$'){throw 'The package file manifest is invalid.'}
            $files[$name]=[string]$file.sha256
        }
        foreach($required in @('Setup-Tandem.ps1','Launch-Tandem.ps1','source/tools/build_identity.py')){if(-not $files.ContainsKey($required)){throw 'The package is missing required setup files.'}}
        foreach($name in $entries.Keys){if($name -ne 'package-manifest.json' -and -not $files.ContainsKey($name)){throw 'The update contains a file outside its manifest.'}}
        $drive=New-Object IO.DriveInfo([IO.Path]::GetPathRoot($Root))
        if($drive.AvailableFreeSpace -lt ($total+1536MB)){throw 'Not enough free space. Free space for the update and local rebuild, then try again.'}
        [IO.Directory]::CreateDirectory($Stage)|Out-Null;$index=0
        foreach($name in $files.Keys) {
            $index++;$entry=$entries[$name]
            if($entry) {
                $dest=Assert-TandemSafePath $Stage (Join-Path $Stage $name)
                [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($dest))|Out-Null
                [IO.Compression.ZipFileExtensions]::ExtractToFile($entry,$dest,$true)
                if((Get-TandemHash $dest) -ne $files[$name]){throw ('Package file failed verification: '+$name)}
            }else {
                if(-not $name.StartsWith('tools/')){throw 'The update is missing a required package file.'}
                $existing=Assert-TandemSafePath $Root (Join-Path $Root $name)
                if(-not [IO.File]::Exists($existing) -or (Get-TandemHash $existing) -ne $files[$name]){throw (New-Object IO.InvalidDataException('TANDEM_FULL_PACKAGE_REQUIRED'))}
            }
            if($index % 100 -eq 0){Send-TandemProgress $Progress 'Verifying files' ($index.ToString()+' / '+$files.Count) ([int](100*$index/$files.Count))}
        }
        [IO.Compression.ZipFileExtensions]::ExtractToFile($manifestEntry,(Join-Path $Stage 'package-manifest.json'),$true)
        return $manifest
    }finally {$zip.Dispose()}
}
function Copy-TandemAtomic([string]$From,[string]$To) {
    [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($To))|Out-Null
    $temporary=$To+'.'+[guid]::NewGuid().ToString('N')+'.update'
    try {
        [IO.File]::Copy($From,$temporary,$true)
        if([IO.File]::Exists($To)){[IO.File]::Replace($temporary,$To,[System.Management.Automation.Language.NullString]::Value)}else{[IO.File]::Move($temporary,$To)}
    }finally {if([IO.File]::Exists($temporary)){[IO.File]::Delete($temporary)}}
}
function Get-TandemUpdateLock([string]$Root) {
    $folder=Join-Path $Root 'local-data/updates';$null=Assert-TandemSafePath $Root $folder
    [IO.Directory]::CreateDirectory($folder)|Out-Null
    try {return [IO.File]::Open((Join-Path $folder 'update.lock'),[IO.FileMode]::OpenOrCreate,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)}
    catch {throw 'An update is already running for this installation.'}
}
function Set-TandemTransaction([string]$Root,$Record,[string]$Phase) {
    $Record.Phase=$Phase;$Record.OwnerId=$PID;$Record.OwnerStarted=(Get-Process -Id $PID).StartTime.ToUniversalTime().ToString('o')
    Write-TandemJson (Join-Path $Root 'local-data/update-transaction.json') $Record
}
function New-TandemUpdateWorker([string]$Root,[string]$Tag,[int]$BootstrapId=0,[string]$BootstrapStarted='',[switch]$Headless) {
    if(-not (Get-TandemVersion $Tag)){throw 'No update version was selected.'}
    $lock=Get-TandemUpdateLock $Root
    try {
        if((Get-TandemUpdateStatus $Root).Pending){throw 'Finish or recover the previous update before starting another.'}
        $active=Join-Path $Root 'local-data/updates/active';$null=Assert-TandemSafePath $Root $active
        [IO.Directory]::CreateDirectory($active)|Out-Null
        foreach($name in @('Updater-Core.ps1','Update-Tandem.ps1','Update-Build.ps1','Tandem-Paths.ps1','Launcher-Core.ps1')){Copy-TandemAtomic (Join-Path $PSScriptRoot $name) (Join-Path $active $name)}
        $art=Join-Path $Root 'source/distribution/assets/project-tandem.png'
        if([IO.File]::Exists($art)){Copy-TandemAtomic $art (Join-Path $active 'artwork.png')}
        $record=[pscustomobject]@{Format=1;Id=[guid]::NewGuid().ToString('N');Root=[IO.Path]::GetFullPath($Root);Tag=$Tag;Phase='Starting';OwnerId=0;OwnerStarted=''}
        Set-TandemTransaction $Root $record 'Starting'
        $arguments=@('-NoProfile','-STA','-ExecutionPolicy','Bypass','-File',(Join-Path $active 'Update-Tandem.ps1'),'-Root',$Root,'-TransactionId',$record.Id,
            '-WaitId',[string]$PID,'-WaitStarted',(Get-Process -Id $PID).StartTime.ToUniversalTime().ToString('o'))
        if($BootstrapId){$arguments+=@('-BootstrapId',[string]$BootstrapId,'-BootstrapStarted',$BootstrapStarted)}
        if($Headless){$arguments+='-Headless'}
        try {return Start-Process -FilePath (Join-Path $env:SystemRoot 'System32/WindowsPowerShell/v1.0/powershell.exe') -ArgumentList (Join-TandemArguments $arguments) -WorkingDirectory $active -WindowStyle Hidden -PassThru}
        catch {[IO.File]::Delete((Join-Path $Root 'local-data/update-transaction.json'));throw}
    }finally {$lock.Dispose()}
}
function Get-TandemJournal([string]$Root,$Record) {
    if($Record.Format -ne 1 -or $Record.Id -notmatch '^[a-f0-9]{32}$' -or -not (Test-TandemPathEqual $Root $Record.Root)){throw 'The update recovery record is invalid.'}
    $workspace=Assert-TandemSafePath $Root (Join-Path $Root ('local-data/updates/'+$Record.Id))
    return [pscustomobject]@{Folder=$workspace;File=(Join-Path $workspace 'journal.json')}
}
function Invoke-TandemUpdateBuild([string]$Root,[string]$Image,$Record,[string]$Folder,[scriptblock]$Progress) {
    if(-not ('TandemBuildJob' -as [type])) {
        Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public sealed class TandemBuildJob : IDisposable {
    [StructLayout(LayoutKind.Sequential)] struct BasicLimits {
        public long ProcessTime,JobTime; public uint Flags;
        public UIntPtr MinWorking,MaxWorking; public uint ActiveProcesses;
        public UIntPtr Affinity; public uint Priority,Scheduling;
    }
    [StructLayout(LayoutKind.Sequential)] struct Counters {public ulong ReadOps,WriteOps,OtherOps,ReadBytes,WriteBytes,OtherBytes;}
    [StructLayout(LayoutKind.Sequential)] struct ExtendedLimits {public BasicLimits Basic;public Counters IO;public UIntPtr ProcessMemory,JobMemory,PeakProcessMemory,PeakJobMemory;}
    [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern IntPtr CreateJobObject(IntPtr attributes,string name);
    [DllImport("kernel32.dll",SetLastError=true)] static extern bool SetInformationJobObject(IntPtr job,int kind,IntPtr data,uint size);
    [DllImport("kernel32.dll",SetLastError=true)] static extern bool AssignProcessToJobObject(IntPtr job,IntPtr process);
    [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr handle);
    IntPtr handle;
    public TandemBuildJob() {
        handle=CreateJobObject(IntPtr.Zero,null);
        if(handle==IntPtr.Zero)throw new System.ComponentModel.Win32Exception();
        var limits=new ExtendedLimits();limits.Basic.Flags=0x2000; // JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE
        int size=Marshal.SizeOf(limits);IntPtr pointer=Marshal.AllocHGlobal(size);
        try {Marshal.StructureToPtr(limits,pointer,false);if(!SetInformationJobObject(handle,9,pointer,(uint)size))throw new System.ComponentModel.Win32Exception();}
        catch {Dispose();throw;}finally{Marshal.FreeHGlobal(pointer);}
    }
    public void Attach(System.Diagnostics.Process process){if(!AssignProcessToJobObject(handle,process.Handle))throw new System.ComponentModel.Win32Exception();}
    public void Dispose(){if(handle!=IntPtr.Zero){CloseHandle(handle);handle=IntPtr.Zero;}}
}
'@
    }
    $job=New-Object TandemBuildJob;$child=$null
    try {
        $buildArguments=@('-NoProfile','-ExecutionPolicy','Bypass','-File',(Join-Path $PSScriptRoot 'Update-Build.ps1'),'-Root',$Root,'-GameImage',$Image)
        $child=Start-Process -FilePath (Join-Path $env:SystemRoot 'System32/WindowsPowerShell/v1.0/powershell.exe') -ArgumentList (Join-TandemArguments $buildArguments) -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $Folder 'build-stdout.log') -RedirectStandardError (Join-Path $Folder 'build-stderr.log')
        $job.Attach($child)
        $Record|Add-Member -NotePropertyName BuildOwnerId -NotePropertyValue $child.Id -Force
        $Record|Add-Member -NotePropertyName BuildOwnerStarted -NotePropertyValue $child.StartTime.ToUniversalTime().ToString('o') -Force
        Set-TandemTransaction $Root $Record 'Building'
        $last='';$percent=-1
        while(-not $child.WaitForExit(250)) {
            $log=Join-Path $Folder 'build-stdout.log';$tail=if([IO.File]::Exists($log)){Get-Content -LiteralPath $log -Tail 1 -ErrorAction SilentlyContinue}else{''}
            if($tail -and $tail -ne $last){$last=[string]$tail;if($last -match '(\d+)%'){$percent=[int]$Matches[1]}else{$percent=-1}}
            Send-TandemProgress $Progress 'Building' $(if($last){$last}else{'Rebuilding from your cached disc. This can take several minutes.'}) $percent
        }
        if($child.ExitCode -ne 0){throw ('Local rebuild failed. Details are saved in '+(Join-Path $Folder 'build-stderr.log'))}
    }finally {
        # Closing the job also stops any remaining compiler descendants; on an
        # abrupt updater exit Windows closes this handle and does the same.
        $job.Dispose();if($child){$child.WaitForExit(10000)|Out-Null;$child.Dispose()}
    }
}
function Restore-TandemUpdate([string]$Root,$Record,[scriptblock]$Progress) {
    if($Record.BuildOwnerId) {
        try {$previous=Get-Process -Id ([int]$Record.BuildOwnerId) -ErrorAction Stop
            if($previous.StartTime.ToUniversalTime().ToString('o') -eq $Record.BuildOwnerStarted -and -not $previous.WaitForExit(10000)){throw 'The previous build is still stopping. Reopen the launcher to retry recovery.'}
        }catch [Microsoft.PowerShell.Commands.ProcessCommandException]{}
    }
    $where=Get-TandemJournal $Root $Record
    if([IO.File]::Exists($where.File)) {
        $journal=Get-Content -LiteralPath $where.File -Raw|ConvertFrom-Json
        if($journal.Prepared) {
            Send-TandemProgress $Progress 'Restoring' 'Restoring the previous version and build.'
            foreach($file in $journal.Files) {
                $name=Get-TandemPackagePath ([string]$file.Path) -Generated
                $dest=Assert-TandemSafePath $Root (Join-Path $Root $name)
                if($file.Existed) {
                    $from=Assert-TandemSafePath $where.Folder (Join-Path $where.Folder ('backup/files/'+$name))
                    if((Get-TandemHash $from) -ne $file.Hash){throw 'An update backup failed verification. Recovery has stopped.'}
                    Copy-TandemAtomic $from $dest
                }elseif([IO.File]::Exists($dest)){[IO.File]::Delete($dest)}
            }
            $build=Assert-TandemSafePath $Root (Join-Path $Root 'local-data/build-portable')
            $old=Assert-TandemSafePath $where.Folder (Join-Path $where.Folder 'backup/build-portable')
            if([IO.Directory]::Exists($old) -or (-not $journal.HadBuild -and $journal.BuildStarted)) {
                if([IO.Directory]::Exists($build)) {
                    $failed=Assert-TandemSafePath $where.Folder (Join-Path $where.Folder ('failed-build-'+[guid]::NewGuid().ToString('N')))
                    [IO.Directory]::Move($build,$failed)
                }
                if([IO.Directory]::Exists($old)){[IO.Directory]::Move($old,$build)}
            }
            foreach($name in @('settings.json','game-image.json','update-pending.json')) {
                $entry=@($journal.PrivateFiles|Where-Object {$_.Path -eq $name})|Select-Object -First 1
                if(-not $entry){throw 'The private update backup record is incomplete.'}
                $dest=Assert-TandemSafePath $Root (Join-Path $Root ('local-data/'+$name))
                if($entry.Existed) {
                    $from=Assert-TandemSafePath $where.Folder (Join-Path $where.Folder ('backup/'+$name))
                    if((Get-TandemHash $from) -ne $entry.Hash){throw 'The settings backup failed verification.'};Copy-TandemAtomic $from $dest
                }elseif([IO.File]::Exists($dest)){[IO.File]::Delete($dest)}
            }
        }
        $journal.Phase='RolledBack';Write-TandemJson $where.File $journal
    }
    $Record.Phase='RolledBack';Write-TandemJson (Join-Path $where.Folder 'result.json') $Record
    [IO.File]::Delete((Join-Path $Root 'local-data/update-transaction.json'))
}
function Install-TandemUpdate([string]$Root,[string]$Stage,$Manifest,$Record,[scriptblock]$Progress) {
    Assert-TandemClosed $Root
    $where=Get-TandemJournal $Root $Record;$backup=Join-Path $where.Folder 'backup'
    [IO.Directory]::CreateDirectory($backup)|Out-Null
    $old=Get-Content -LiteralPath (Join-Path $Root 'package-manifest.json') -Raw|ConvertFrom-Json
    $installed='';try{$installed=[string](Get-Content -LiteralPath (Join-Path $Root 'local-data/build-portable/identity/build-identity.json') -Raw|ConvertFrom-Json).id}catch{}
    $rebuild=($installed -ne $Manifest.compatibility) -or -not [IO.File]::Exists((Join-Path $Root 'local-data/build-portable/mkdd.exe')) -or (Test-TandemBuildMoved $Root)
    $paths=@{};foreach($e in $Manifest.files) {if([IO.File]::Exists((Join-Path $Stage $e.path))){$paths[(Get-TandemPackagePath $e.path)]='Replace'}}
    $paths['package-manifest.json']='Replace'
    $newPaths=@{};foreach($e in $Manifest.files){$newPaths[[string]$e.path]=$true}
    foreach($e in $old.files){$name=Get-TandemPackagePath ([string]$e.path);if(-not $newPaths.ContainsKey($name)){$paths[$name]='Remove'}}
    # These are compiled locally, never downloaded from a release archive.
    foreach($name in @('ProjectTandem.exe','Project Tandem.lnk')){$paths[$name]='Generated'}
    $files=New-Object Collections.Generic.List[object]
    foreach($name in $paths.Keys) {
        $dest=Assert-TandemSafePath $Root (Join-Path $Root $name)
        $exists=[IO.File]::Exists($dest);$hash=if($exists){Get-TandemHash $dest}else{''}
        if($paths[$name] -eq 'Replace' -and $exists -and $hash -eq (Get-TandemHash (Join-Path $Stage $name))){continue}
        if($paths[$name] -eq 'Remove' -and -not $exists){continue}
        if($exists){$copy=Join-Path $backup ('files/'+$name);[IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($copy))|Out-Null;[IO.File]::Copy($dest,$copy,$true)}
        $files.Add([pscustomobject]@{Path=$name;Existed=$exists;Hash=$hash;Operation=$paths[$name]})
    }
    $private=New-Object Collections.Generic.List[object]
    foreach($name in @('settings.json','game-image.json','update-pending.json')) {
        $path=Assert-TandemSafePath $Root (Join-Path $Root ('local-data/'+$name));$exists=[IO.File]::Exists($path)
        $hash=if($exists){Get-TandemHash $path}else{''};if($exists){[IO.File]::Copy($path,(Join-Path $backup $name),$true)}
        $private.Add([pscustomobject]@{Path=$name;Existed=$exists;Hash=$hash})
    }
    $journal=[pscustomobject]@{Format=1;Phase='Prepared';Prepared=$true;Files=@($files.ToArray());PrivateFiles=@($private.ToArray());
        HadBuild=[IO.Directory]::Exists((Join-Path $Root 'local-data/build-portable'));BuildStarted=$false;Rebuild=$rebuild}
    Write-TandemJson $where.File $journal
    # There are no package writes until the entire backup/journal is durable.
    Set-TandemTransaction $Root $Record 'Installing'
    Send-TandemProgress $Progress 'Installing' 'Installing verified files. Please keep this window open.'
    $stop=Join-Path $Root 'Stop-Online.ps1';if([IO.File]::Exists($stop)){& $stop}
    foreach($file in ($journal.Files|Sort-Object @{Expression={if($_.Path -eq 'package-manifest.json'){2}elseif($_.Path -eq 'Tandem-Launcher.ps1'){1}else{0}}},Path)) {
        $dest=Assert-TandemSafePath $Root (Join-Path $Root $file.Path)
        if($file.Operation -eq 'Replace'){Copy-TandemAtomic (Join-Path $Stage $file.Path) $dest}
        elseif($file.Operation -eq 'Remove' -and [IO.File]::Exists($dest)){[IO.File]::Delete($dest)}
    }
    # Rebuild the authored launcher after the old bootstrap releases its EXE.
    & (Join-Path $Root 'Build-Launcher.ps1') -Root $Root
    if($rebuild) {
        $image=Get-TandemCachedImage $Root;if(-not $image){throw 'A cached disc is required. Run setup/repair first.'}
        $build=Assert-TandemSafePath $Root (Join-Path $Root 'local-data/build-portable')
        $previous=Assert-TandemSafePath $where.Folder (Join-Path $backup 'build-portable')
        if([IO.Directory]::Exists($build)){[IO.Directory]::Move($build,$previous)}
        $journal.BuildStarted=$true;$journal.Phase='Building';Write-TandemJson $where.File $journal
        Set-TandemTransaction $Root $Record 'Building';Send-TandemProgress $Progress 'Building' 'Rebuilding locally from your cached disc. This can take several minutes.'
        Invoke-TandemUpdateBuild $Root $image $Record $where.Folder $Progress
    }else {
        & (Join-Path $Root 'Create-Shortcut.ps1') -Root $Root
    }
    $identity=Get-Content -LiteralPath (Join-Path $Root 'local-data/build-portable/identity/build-identity.json') -Raw|ConvertFrom-Json
    if($identity.id -ne $Manifest.compatibility){throw 'The updated game build does not match the release.'}
    Send-TandemProgress $Progress 'Final checks' 'Verifying installed files and build compatibility.'
    foreach($entry in $Manifest.files) {
        $path=Assert-TandemSafePath $Root (Join-Path $Root $entry.path)
        if((Get-TandemHash $path) -ne $entry.sha256){throw ('Installed file failed verification: '+$entry.path)}
    }
    $journal.Phase='Complete';Write-TandemJson $where.File $journal
    Write-TandemJson (Join-Path $where.Folder 'result.json') ([pscustomobject]@{Succeeded=$true;Tag=$Record.Tag;Compatibility=$Manifest.compatibility;Rebuilt=$rebuild;Finished=[DateTime]::UtcNow.ToString('o')})
    [IO.File]::Delete((Join-Path $Root 'local-data/update-transaction.json'))
    Send-TandemProgress $Progress 'Ready' 'Update installed. Ready to play.' 100
}
