param([Parameter(Mandatory=$true)][string]$Bootstrap,[switch]$SelfTest,[switch]$IntegrationTest,[string]$Evidence,[string]$TestImage)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'Installer-Core.ps1')
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
[Windows.Forms.Application]::EnableVisualStyles()
Add-Type -Path (Join-Path $PSScriptRoot 'TaskbarIdentity.cs')
[TandemTaskbarIdentity]::InitializeProcess()
if(($SelfTest -or $IntegrationTest) -and -not $Evidence){throw 'Installer tests require an isolated evidence folder.'}
if($Evidence){[IO.Directory]::CreateDirectory($Evidence)|Out-Null}
$form=New-Object Windows.Forms.Form
$form.Text='Project Tandem';$form.ClientSize=New-Object Drawing.Size(780,570)
$form.StartPosition='CenterScreen';$form.FormBorderStyle='FixedSingle';$form.MaximizeBox=$false
$form.BackColor=[Drawing.Color]::FromArgb(8,28,83);$form.ForeColor=[Drawing.Color]::White
$form.Font=New-Object Drawing.Font('Segoe UI',11)
$form.Icon=New-Object Drawing.Icon((Join-Path $PSScriptRoot 'project-tandem.ico'))
$form.add_HandleCreated({[TandemTaskbarIdentity]::ApplyExecutable($form.Handle,$Bootstrap,$Bootstrap,$form.Icon.Handle)})
function Label([string]$Text,[int]$X,[int]$Y,[int]$W,[int]$H,[int]$Size=11){
    $label=New-Object Windows.Forms.Label;$label.Text=$Text;$label.SetBounds($X,$Y,$W,$H)
    $label.Font=New-Object Drawing.Font('Segoe UI',$Size);$form.Controls.Add($label);return $label
}
$heading=Label 'PROJECT TANDEM' 28 20 560 54 27;$heading.ForeColor=[Drawing.Color]::FromArgb(255,220,90)
$intro=Label 'Install once. Play and update from this launcher.' 31 79 710 30 13
$steps=Label 'Download files  >  Choose disc image  >  Build game  >  Play' 31 120 710 35
$folderLabel=Label 'Install location' 31 173 400 26
$folder=New-Object Windows.Forms.TextBox;$folder.SetBounds(31,204,597,29)
$folder.Text=Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) 'Programs/Project Tandem'
if($SelfTest -or $IntegrationTest){$folder.Text=Join-Path $Evidence 'installed game'}
$form.Controls.Add($folder)
$browse=New-Object Windows.Forms.Button;$browse.Text='Browse';$browse.SetBounds(642,202,106,33);$form.Controls.Add($browse)
$status=Label 'Ready to install' 31 260 715 34 16
$detailLabel=Label 'The latest Windows release will be downloaded from the official GitHub page.' 31 303 715 58
$progress=New-Object Windows.Forms.ProgressBar;$progress.SetBounds(31,370,717,15);$progress.Style='Continuous';$form.Controls.Add($progress)
$install=New-Object Windows.Forms.Button;$install.Text='INSTALL';$install.SetBounds(31,411,225,52)
$install.BackColor=[Drawing.Color]::FromArgb(255,220,90);$install.ForeColor=[Drawing.Color]::FromArgb(8,28,83)
$install.FlatStyle='Flat';$form.Controls.Add($install)
$cancel=New-Object Windows.Forms.Button;$cancel.Text='Cancel download';$cancel.SetBounds(275,411,185,52);$cancel.Visible=$false;$form.Controls.Add($cancel)
$logs=New-Object Windows.Forms.LinkLabel;$logs.Text='Open installation log';$logs.SetBounds(490,425,230,28)
$logs.LinkColor=[Drawing.Color]::FromArgb(255,220,90);$logs.Visible=$false;$form.Controls.Add($logs)
$footer=Label 'You will need your own supported USA Double Dash ISO or CISO. Your disc image stays on your PC.' 31 495 714 52 10
$desktopOption=New-Object Windows.Forms.CheckBox;$desktopOption.Text='Create a desktop shortcut';$desktopOption.SetBounds(31,175,700,30);$desktopOption.Checked=$true;$desktopOption.Visible=$false;$form.Controls.Add($desktopOption)
$steamOption=New-Object Windows.Forms.CheckBox;$steamOption.Text='Add to Steam (finish in Steam)';$steamOption.SetBounds(31,210,700,30);$steamOption.Visible=$false;$form.Controls.Add($steamOption)
$steamOption.Enabled=Test-Path -LiteralPath 'Registry::HKEY_CLASSES_ROOT\steam'
if(-not $steamOption.Enabled){$steamOption.Text='Add to Steam (install Steam first)'}
$desktop=[Environment]::GetFolderPath('DesktopDirectory')
if($SelfTest -or $IntegrationTest){$desktop=Join-Path $Evidence 'desktop'}
$script:job=$null;$script:filesReady=$false;$script:done=$false;$script:closeWhenDone=$false
$script:testFailure='';$script:views=@();$script:seenStages=@{};$script:finalRoot=''
$registration=Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) 'ProjectTandem/installed-root.txt'
if($SelfTest -or $IntegrationTest){$registration=Join-Path $Evidence 'registration/installed-root.txt'}
function Capture([string]$Name){
    if(-not $Evidence){return}
    $bitmap=New-Object Drawing.Bitmap($form.Width,$form.Height)
    try{$form.DrawToBitmap($bitmap,(New-Object Drawing.Rectangle(0,0,$form.Width,$form.Height)));$bitmap.Save((Join-Path $Evidence ($Name+'.png')))}finally{$bitmap.Dispose()}
}
function Show-Stage([string]$Stage,[string]$Detail,[int]$Percent=-1){
    $status.Text=$Stage;$detailLabel.Text=$Detail
    if($Percent -lt 0){$progress.Style='Marquee'}else{$progress.Style='Continuous';$progress.Value=[Math]::Max(0,[Math]::Min(100,$Percent))}
    if($Evidence -and -not $script:seenStages.ContainsKey($Stage)){
        $script:seenStages[$Stage]=$true;$script:views+=@{stage=$Stage;detail=$Detail;percent=$Percent;worker=[bool]$script:job}
        Capture ('stage-'+$script:views.Count)
    }
}
function Start-InstallerJob([string]$Action,[string]$Image=''){
    $root=[IO.Path]::GetFullPath($folder.Text).TrimEnd('\')
    $probe=Initialize-TandemInstall $root $Bootstrap;$probe.Dispose()
    $where=Assert-TandemSafePath $root (Join-Path $root ('local-data/installer/logs/'+[guid]::NewGuid().ToString('N')))
    [IO.Directory]::CreateDirectory($where)|Out-Null
    $result=Join-Path $where 'result.json';$report=Join-Path $where 'progress.json';$cancelFile=Join-Path $where 'cancel';$admission=Join-Path $where 'admitted.json'
    $arguments=@('-NoProfile','-STA','-ExecutionPolicy','Bypass','-File',(Join-Path $PSScriptRoot 'Installer-Worker.ps1'),'-Root',$root,
        '-Action',$Action,'-Result',$result,'-ProgressFile',$report,'-Bootstrap',$Bootstrap,'-CancelFile',$cancelFile,'-AdmissionFile',$admission)
    if($Image){$arguments+=@('-GameImage',$Image)}
    $program=Join-Path $env:SystemRoot 'System32/WindowsPowerShell/v1.0/powershell.exe'
    $oldModules=$env:PSModulePath
    Initialize-TandemBuildJob;$container=New-Object TandemBuildJob;$child=$null
    try{
        $env:PSModulePath=Join-Path $env:SystemRoot 'System32/WindowsPowerShell/v1.0/Modules'
        $child=Start-Process -FilePath $program -ArgumentList (Join-TandemArguments $arguments) -WorkingDirectory $root -WindowStyle Hidden -PassThru `
            -RedirectStandardOutput (Join-Path $where 'stdout.log') -RedirectStandardError (Join-Path $where 'stderr.log')
        $container.Attach($child)
        Write-TandemJson $admission @{id=$child.Id;started=$child.StartTime.ToUniversalTime().ToString('o')}
    }catch{
        $container.Dispose();if($child -and -not $child.HasExited){$child.Kill();$child.WaitForExit()};throw
    }finally{$env:PSModulePath=$oldModules}
    $script:job=@{Process=$child;Container=$container;Action=$Action;Root=$root;Folder=$where;Result=$result;Progress=$report;Cancel=$cancelFile}
    $script:lastLog=Join-Path $where 'stdout.log';$logs.Visible=$true
    $folder.Enabled=$false;$browse.Enabled=$false;$install.Enabled=$false
    $cancel.Visible=$Action -eq 'Download';$cancel.Enabled=$true
    Show-Stage $(if($Action -eq 'Download'){'Checking GitHub'}else{'Building your game'}) $(if($Action -eq 'Download'){'Finding the latest Windows release.'}else{'Setup is running. This can take several minutes on the first install.'})
}
function Choose-Disc {
    Show-Stage 'Choose your disc image' 'Download complete. Select your supported USA Double Dash ISO or CISO.' 100
    $install.Text='CHOOSE DISC IMAGE';$install.Enabled=$true
    if($IntegrationTest){Start-InstallerJob 'Build' $TestImage;return}
    $dialog=New-Object Windows.Forms.OpenFileDialog;$dialog.Title='Select your Double Dash disc image'
    $dialog.Filter='GameCube disc images (*.iso;*.ciso)|*.iso;*.ciso';$dialog.CheckFileExists=$true
    try{if($dialog.ShowDialog($form) -eq [Windows.Forms.DialogResult]::OK){Start-InstallerJob 'Build' $dialog.FileName}}
    finally{$dialog.Dispose()}
}
function Finish-Install {
    Complete-TandemInstall $folder.Text $Bootstrap $registration
    $script:done=$true;$script:finalRoot=$folder.Text
    $folder.Visible=$false;$folderLabel.Visible=$false;$browse.Visible=$false
    $desktopOption.Visible=$true;$steamOption.Visible=$true
    $steps.Text='Installation complete. Choose your shortcuts below.'
    $footer.Text='Steam opens its game picker for you to confirm the shortcut. Tandem handles game updates.'
    Show-Stage 'Ready to play' ('Installed at '+$folder.Text) 100
    $install.Text='OPEN LAUNCHER';$install.Enabled=$true
    Capture 'installer-complete'
    if($IntegrationTest){Open-InstalledLauncher}
}
function Open-InstalledLauncher {
    if($desktopOption.Checked){$script:createdShortcut=New-TandemDesktopShortcut $folder.Text $desktop}
    if($steamOption.Checked -and -not ($SelfTest -or $IntegrationTest)){
        $steam=Get-TandemSteamShortcutInfo $folder.Text
        [Windows.Forms.Clipboard]::SetText($steam.Launcher)
        Start-Process $steam.Uri|Out-Null
        [void][Windows.Forms.MessageBox]::Show($form,($steam.Instructions+"`r`n`r`n"+$steam.Launcher),'Add Project Tandem to Steam','OK','Information')
    }
    if($IntegrationTest){
        $normal=Join-Path $Evidence 'normal-launcher'
        $child=Start-Process -FilePath (Join-Path $folder.Text 'ProjectTandem.exe') -WorkingDirectory $folder.Text -WindowStyle Hidden -PassThru -ArgumentList (Join-TandemArguments @('--self-test',$normal))
        if(-not $child.WaitForExit(30000) -or $child.ExitCode -ne 0){throw 'The installed launcher did not pass its startup check.'}
    }else{Start-Process -FilePath (Join-Path $folder.Text 'ProjectTandem.exe') -WorkingDirectory $folder.Text -WindowStyle Normal|Out-Null}
    $form.Close()
}
$browse.add_Click({
    $dialog=New-Object Windows.Forms.FolderBrowserDialog;$dialog.Description='Choose an empty folder for Project Tandem';$dialog.SelectedPath=$folder.Text
    try{if($dialog.ShowDialog($form) -eq [Windows.Forms.DialogResult]::OK){$folder.Text=$dialog.SelectedPath}}finally{$dialog.Dispose()}
})
$logs.add_LinkClicked({if($script:lastLog){Start-Process -FilePath 'notepad.exe' -ArgumentList (Join-TandemArguments @($script:lastLog)) -WindowStyle Normal|Out-Null}})
$install.add_Click({
    try{
        if($script:done){Open-InstalledLauncher}elseif($script:filesReady){Choose-Disc}else{Start-InstallerJob 'Download'}
    }catch{Show-Stage 'Installation needs attention' $_.Exception.Message;$install.Enabled=$true;if($IntegrationTest){$script:testFailure=$_.Exception.Message;$form.Close()}}
})
$cancel.add_Click({if($script:job -and $script:job.Action -eq 'Download'){
    [IO.File]::WriteAllText($script:job.Cancel,'cancel');$cancel.Enabled=$false
    Show-Stage 'Cancelling download' 'Waiting for the current file operation to finish.'
}})
$form.add_FormClosing({param($sender,$event)
    if($script:job){
        $event.Cancel=$true
        if($script:job.Action -eq 'Download'){$script:closeWhenDone=$true;$cancel.PerformClick()}
        else{Show-Stage 'Setup is still running' 'Please let setup finish before closing this window. Progress will continue here.'}
    }
})
$timer=New-Object Windows.Forms.Timer;$timer.Interval=250
$timer.add_Tick({
    if(-not $script:job){return}
    try{
        $job=$script:job
        if($job.Action -eq 'Download' -and (Test-Path -LiteralPath $job.Progress)){
            $report=Read-TandemJson $job.Progress;Show-Stage $report.stage $report.detail ([int]$report.percent)
        }elseif($job.Action -eq 'Build'){
            $lines=@(Get-Content -LiteralPath (Join-Path $job.Folder 'stdout.log') -Tail 12 -ErrorAction SilentlyContinue)
            foreach($line in $lines){$report=ConvertTo-TandemInstallProgress $line;if($report){Show-Stage $report.Stage $report.Detail $report.Percent}}
        }
        if(-not $job.Process.HasExited){return}
        $job.Process.WaitForExit();$job.Container.Dispose();$script:job=$null;$cancel.Visible=$false
        if(-not (Test-Path -LiteralPath $job.Result)){throw 'Installation stopped unexpectedly. Open the installation log, then retry.'}
        $result=Read-TandemJson $job.Result
        if(-not $result.succeeded){throw [string]$result.message}
        if($job.Action -eq 'Download'){$script:filesReady=$true;Choose-Disc}else{Finish-Install}
    }catch{
        if($script:job -and -not $script:job.Process.HasExited){$detailLabel.Text=$_.Exception.Message;return}
        $script:job=$null;$cancel.Visible=$false;$install.Enabled=$true
        $install.Text=if($script:filesReady){'CHOOSE DISC IMAGE'}else{'RETRY INSTALL'}
        $folder.Enabled=-not $script:filesReady;$browse.Enabled=$folder.Enabled
        Show-Stage 'Installation needs attention' $_.Exception.Message
        if($IntegrationTest){$script:testFailure=$_.Exception.Message;$form.Close()}
        elseif($script:closeWhenDone){$form.Close()}
    }
})
try{
    if($SelfTest){
        $form.ShowInTaskbar=$false;$form.Opacity=0;$form.Show();[Windows.Forms.Application]::DoEvents();Capture 'installer-start'
        foreach($view in @(@('Downloading','48.0 / 222.6 MB',21),@('Choose your disc image','Files are ready. Select your ISO or CISO.',100),@('4/5 Build game','222 / 444 build steps',50),@('Installation needs attention','The disc revision is unsupported. Select the supported USA image.',-1),@('Ready to play','Opening your launcher...',100))){Show-Stage $view[0] $view[1] $view[2]}
        $identity=[TandemTaskbarIdentity]::Inspect($form.Handle)
        if($identity['window_app_id'] -ne [TandemTaskbarIdentity]::AppId -or $identity['icon_resource'] -ne ($Bootstrap+',0')){throw 'Installer taskbar artwork was not applied.'}
        $folder.Visible=$false;$folderLabel.Visible=$false;$browse.Visible=$false;$desktopOption.Visible=$true;$steamOption.Visible=$true
        $steps.Text='Installation complete. Choose your shortcuts below.';$install.Text='OPEN LAUNCHER';Capture 'installer-complete'
        Write-TandemJson (Join-Path $Evidence 'result.json') @{passed=$true;views=$script:views;standalone=$true;taskbar=$identity}
    }else{
        if($IntegrationTest){$form.ShowInTaskbar=$false;$form.Opacity=0;$form.add_Shown({$install.PerformClick()})}
        $timer.Start();[void]$form.ShowDialog()
        if($IntegrationTest){
            $passed=$script:done -and -not $script:testFailure
            Write-TandemJson (Join-Path $Evidence 'result.json') @{passed=$passed;error=$script:testFailure;root=$script:finalRoot;shortcut=$script:createdShortcut;views=$script:views;ready=$(if($script:done){(Get-TandemLauncherState $script:finalRoot).Ready}else{$false})}
            if(-not $passed){exit 1}
        }
    }
}finally{if($script:job){$script:job.Container.Dispose()};$timer.Stop();$timer.Dispose();$form.Dispose()}
