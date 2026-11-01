param([string]$Root=$PSScriptRoot,[switch]$SelfTest,[string]$Evidence,
      [ValidateSet('Current','Available','Checking','Offline')][string]$TestView='Current')
$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
. (Join-Path $PSScriptRoot 'Launcher-Core.ps1')
. (Join-Path $PSScriptRoot 'Tandem-Paths.ps1')
. (Join-Path $PSScriptRoot 'Updater-Core.ps1')
if($TestView -ne 'Current' -and -not $SelfTest){throw 'TestView is only available for hidden self-tests.'}
[Windows.Forms.Application]::EnableVisualStyles()
$Root=[IO.Path]::GetFullPath($Root)
Add-Type -Path (Join-Path $Root 'source/distribution/launcher/TaskbarIdentity.cs')
[TandemTaskbarIdentity]::InitializeProcess()
$script:job=$null;$script:latestLog=$null;$script:checkJob=$null;$script:candidate=$null
$script:lastCheck=$null;$script:checkError='';$script:lastCheckAttempt=[DateTime]::MinValue
$script:lastStateRefresh=[DateTime]::MinValue
if($SelfTest) {
    Add-Type -ReferencedAssemblies System.Windows.Forms,System.Drawing -TypeDefinition @'
public class TandemRenderForm : System.Windows.Forms.Form {
    protected override bool ShowWithoutActivation {get{return true;}}
}
'@
    $form=New-Object TandemRenderForm
    $form.ShowInTaskbar=$false;$form.Opacity=0
}else {$form=New-Object Windows.Forms.Form}
$form.Text='Project Tandem';$form.ClientSize=New-Object Drawing.Size(960,720)
$form.MinimumSize=New-Object Drawing.Size(976,759);$form.StartPosition='CenterScreen'
$form.BackColor=[Drawing.Color]::FromArgb(8,28,83);$form.ForeColor=[Drawing.Color]::White
$form.Font=New-Object Drawing.Font('Segoe UI',11)
$form.AutoScaleMode=[Windows.Forms.AutoScaleMode]::Dpi
$icon=Join-Path $Root 'source/distribution/assets/project-tandem.ico'
if(Test-Path -LiteralPath $icon){$form.Icon=New-Object Drawing.Icon($icon)}
$form.add_HandleCreated({[TandemTaskbarIdentity]::Apply($form.Handle,$Root,$icon,$form.Icon.Handle)})
function Label([string]$Text,[int]$X,[int]$Y,[int]$W,[int]$H,[int]$Size=11) {
    $l=New-Object Windows.Forms.Label;$l.Text=$Text;$l.Location=New-Object Drawing.Point($X,$Y);$l.Size=New-Object Drawing.Size($W,$H)
    $l.Font=New-Object Drawing.Font('Segoe UI',$Size);$form.Controls.Add($l);return $l
}
$heading=Label 'PROJECT TANDEM' 28 23 620 53 28
$heading.ForeColor=[Drawing.Color]::FromArgb(255,220,90)
$sub=Label 'Play together. One game for LAN and Internet.' 31 83 620 28
$credit=New-Object Windows.Forms.LinkLabel;$credit.Text='Artwork by POOTERMAN';$credit.LinkColor=[Drawing.Color]::FromArgb(255,220,90)
$credit.Location=New-Object Drawing.Point(32,116);$credit.Size=New-Object Drawing.Size(400,26)
$credit.add_LinkClicked({Start-Process 'https://www.deviantart.com/pooterman'});$form.Controls.Add($credit)
$picture=New-Object Windows.Forms.PictureBox;$picture.Location=New-Object Drawing.Point(784,14);$picture.Size=New-Object Drawing.Size(146,146)
$picture.SizeMode=[Windows.Forms.PictureBoxSizeMode]::Zoom
$art=Join-Path $Root 'source/distribution/assets/project-tandem.png'
if(Test-Path -LiteralPath $art){$picture.Image=[Drawing.Image]::FromFile($art)}
$form.Controls.Add($picture)
$buttons=New-Object Collections.Generic.List[Windows.Forms.Button]
function Button([string]$Text,[int]$Y) {
    $b=New-Object Windows.Forms.Button;$b.Text=$Text;$b.Location=New-Object Drawing.Point(30,$Y);$b.Size=New-Object Drawing.Size(235,54)
    $b.FlatStyle='Flat';$b.FlatAppearance.BorderColor=[Drawing.Color]::FromArgb(255,220,90)
    $b.BackColor=[Drawing.Color]::FromArgb(20,61,135);$b.ForeColor=[Drawing.Color]::White
    $b.Cursor=[Windows.Forms.Cursors]::Hand;$form.Controls.Add($b);$buttons.Add($b);return $b
}
$play=Button 'PLAY' 181;$play.BackColor=[Drawing.Color]::FromArgb(255,220,90);$play.ForeColor=[Drawing.Color]::FromArgb(8,28,83)
$setup=Button 'Set up / repair' 249
$check=Button 'Check for updates' 317
$updates=Button 'Up to date' 385
$help=Button 'Help & controls' 453
$help.UseMnemonic=$false
$logs=Button 'Open logs' 521
$status=Label '' 297 181 630 56 14
$version=Label '' 297 241 630 28
$build=Label '' 297 273 630 26
$updateStatus=Label 'Check for updates to keep your game ready for online play.' 297 311 630 54
$releaseNotes=New-Object Windows.Forms.LinkLabel;$releaseNotes.Text="What's new";$releaseNotes.LinkColor=[Drawing.Color]::FromArgb(255,220,90)
$releaseNotes.SetBounds(300,368,150,28);$releaseNotes.Visible=$false;$form.Controls.Add($releaseNotes)
$auto=New-Object Windows.Forms.CheckBox;$auto.Text='Check automatically at startup and while open';$auto.SetBounds(300,402,620,30);$auto.Checked=$true;$form.Controls.Add($auto)
$preferences=Join-Path $Root 'local-data/launcher-settings.json'
try {if(Test-Path -LiteralPath $preferences){$auto.Checked=[bool](Get-Content -LiteralPath $preferences -Raw|ConvertFrom-Json).check_at_startup}}catch{}
$auto.add_CheckedChanged({if(-not $SelfTest){try{Write-TandemJson $preferences @{check_at_startup=$auto.Checked}}catch{$output.Text='Could not save launcher preferences: '+$_.Exception.Message}}})
$matching=Label 'Online players need the same build. Game settings are in F1.' 297 440 630 28 10
$progress=New-Object Windows.Forms.ProgressBar;$progress.Location=New-Object Drawing.Point(300,474);$progress.Size=New-Object Drawing.Size(626,8);$progress.Style='Marquee';$progress.Visible=$false;$form.Controls.Add($progress)
$output=New-Object Windows.Forms.TextBox;$output.Multiline=$true;$output.ReadOnly=$true;$output.ScrollBars='Vertical'
$output.Location=New-Object Drawing.Point(300,499);$output.Size=New-Object Drawing.Size(626,142)
$output.BackColor=[Drawing.Color]::FromArgb(5,18,52);$output.ForeColor=[Drawing.Color]::FromArgb(220,232,255)
$output.Font=New-Object Drawing.Font('Consolas',9);$form.Controls.Add($output)
$footer=Label 'Updates download, verify and install here. Your disc, saves and controls stay on your PC.' 31 672 900 35 10
function Refresh-State {
    $script:lastStateRefresh=[DateTime]::UtcNow
    $state=Get-TandemLauncherState $Root
    if(-not $script:job){$status.Text=if($state.Running){'Game running. Updates can be checked while you play.'}else{$state.Detail}}
    $version.Text=$state.Release;$build.Text='Build: '+$state.Compatibility
    $play.Text=if($state.Running){'GAME RUNNING'}elseif($state.Ready){'PLAY'}else{'SET UP & PLAY'}
    $busy=($null -ne $script:job)
    $play.Enabled=$state.PackageValid -and -not $busy -and -not $state.UpdatePending -and -not $state.Running
    $setup.Enabled=$play.Enabled
    $check.Enabled=$state.PackageValid -and -not $busy -and -not $script:checkJob -and -not $state.UpdatePending
    $updates.Text=if($state.UpdateRecovery){'Recover update'}elseif($script:candidate){'Install update'}elseif($script:lastCheck -and -not $script:checkError){'Up to date'}else{'Install update'}
    $updates.Enabled=(-not $busy -and -not $script:checkJob -and -not $state.Running -and ($state.UpdateRecovery -or ($script:candidate -and -not $state.UpdatePending)))
    $updates.BackColor=if($updates.Enabled){[Drawing.Color]::FromArgb(255,220,90)}else{[Drawing.Color]::FromArgb(20,61,135)}
    $updates.ForeColor=if($updates.Enabled){[Drawing.Color]::FromArgb(8,28,83)}else{[Drawing.Color]::White}
    $releaseNotes.Visible=($null -ne $script:candidate)
    if($script:checkJob){$updateStatus.Text='Checking GitHub for a newer Windows release...'}
    elseif($script:checkError){$updateStatus.Text=$script:checkError}
    elseif($script:candidate){$size=if($script:candidate.Cumulative){$script:candidate.Cumulative.size}else{$script:candidate.Full.size};$updateStatus.Text=$script:candidate.Name+' is available. Download: '+[Math]::Round($size/1MB,1)+' MB.'}
    elseif($script:lastCheck){$updateStatus.Text='Up to date. Last checked '+$script:lastCheck.ToLocalTime().ToString('g')+'.'}
}
function Start-UpdateCheck {
    if($script:checkJob -or $script:job){return}
    try {$script:lastCheckAttempt=[DateTime]::UtcNow;$script:checkError='';$script:checkJob=New-TandemLauncherJob $Root 'CheckUpdate' '' (Join-Path $PSScriptRoot 'Launcher-Action.ps1');Refresh-State}
    catch {$script:checkError='Could not check for updates. You can still play. '+$_.Exception.Message;Refresh-State}
}
function Start-Action([string]$Action,[string]$Image) {
    if($script:job){return}
    try {
        $script:job=New-TandemLauncherJob $Root $Action $Image (Join-Path $PSScriptRoot 'Launcher-Action.ps1')
        $script:latestLog=$script:job.Folder;$progress.Visible=$true
        Refresh-State
        $status.Text=if($Action -eq 'Play'){'Starting the game and networking...'}else{'Preparing your game. The first build can take several minutes.'}
        $output.Text='Progress and errors will appear here. Saves and controller settings are kept.'
    }catch {$status.Text='Unable to start.';$output.Text=$_.Exception.Message;$script:job=$null;Refresh-State}
}
function Choose-Setup([bool]$PlayAfter) {
    $cached=Get-TandemCachedImage $Root
    if($cached){$script:playAfterSetup=$PlayAfter;Start-Action 'Setup' $cached;return}
    $dialog=New-Object Windows.Forms.OpenFileDialog;$dialog.Title='Choose your supported USA Double Dash disc image';$dialog.Filter='Disc images (*.iso;*.ciso)|*.iso;*.ciso'
    try {if($dialog.ShowDialog($form) -eq [Windows.Forms.DialogResult]::OK){$script:playAfterSetup=$PlayAfter;Start-Action 'Setup' $dialog.FileName}}
    finally {$dialog.Dispose()}
}
$script:playAfterSetup=$false
$play.add_Click({if((Get-TandemLauncherState $Root).Ready){Start-Action 'Play' ''}else{Choose-Setup $true}})
$setup.add_Click({$script:playAfterSetup=$false;if((Get-TandemLauncherState $Root).Ready){Start-Action 'Repair' ''}else{Choose-Setup $false}})
$check.add_Click({Start-UpdateCheck})
$releaseNotes.add_LinkClicked({if($script:candidate){Start-Process $script:candidate.Url}})
$updates.add_Click({
    try {
        $state=Get-TandemLauncherState $Root;if($state.Running){throw 'Close the game before installing an update.'}
        $bootstrapId=0;$bootstrapStarted=''
        try {$parentId=(Get-CimInstance Win32_Process -Filter "ProcessId=$PID").ParentProcessId;$parent=Get-Process -Id $parentId -ErrorAction Stop
            if(Test-TandemPathEqual $parent.Path (Join-Path $Root 'ProjectTandem.exe')){$bootstrapId=$parent.Id;$bootstrapStarted=$parent.StartTime.ToUniversalTime().ToString('o')}}catch{}
        if($state.UpdateRecovery) {
            $workerArguments=@('-NoProfile','-STA','-ExecutionPolicy','Bypass','-File',(Join-Path $Root 'local-data/updates/active/Update-Tandem.ps1'),'-Root',$Root,'-Recover',
                '-WaitId',[string]$PID,'-WaitStarted',(Get-Process -Id $PID).StartTime.ToUniversalTime().ToString('o'),'-BootstrapId',[string]$bootstrapId,'-BootstrapStarted',$bootstrapStarted)
            Start-Process -FilePath (Join-Path $env:SystemRoot 'System32/WindowsPowerShell/v1.0/powershell.exe') -ArgumentList (Join-TandemArguments $workerArguments) -WindowStyle Hidden|Out-Null
        }elseif($script:candidate){New-TandemUpdateWorker $Root $script:candidate.Tag $bootstrapId $bootstrapStarted|Out-Null}
        else{return}
        $form.Close()
    }catch {$output.Text=$_.Exception.Message}
})
$help.add_Click({$file=Join-Path $Root 'docs/CONTROLS.md';if(Test-Path -LiteralPath $file){Start-Process -FilePath 'notepad.exe' -ArgumentList (Join-TandemArguments @($file))}else{Start-Process 'https://github.com/badcompany94-rcmp/project-tandem#readme'}})
$logs.add_Click({$folder=if($script:latestLog){$script:latestLog}else{Join-Path $Root 'local-data'};if(Test-Path -LiteralPath $folder){Start-Process -FilePath 'explorer.exe' -ArgumentList (Join-TandemArguments @($folder))}})
$timer=New-Object Windows.Forms.Timer;$timer.Interval=400
$timer.add_Tick({
    if($script:checkJob) {
        try {
            $script:checkJob.Process.Refresh()
            if($script:checkJob.Process.HasExited) {
                $checkResult=if(Test-Path -LiteralPath $script:checkJob.Result){Get-Content -LiteralPath $script:checkJob.Result -Raw|ConvertFrom-Json}else{$null}
                if($checkResult -and $checkResult.succeeded){$script:candidate=$checkResult.update.Candidate;$script:lastCheck=[DateTime]::Parse($checkResult.update.CheckedAt);$script:checkError=''}
                else{$script:checkError='Could not check for updates. You can still play. '+$checkResult.message}
                $script:checkJob.Process.Dispose();$script:checkJob=$null
            }
        }catch {$script:checkError='Could not check for updates. Try again later.';$script:checkJob=$null}
    }
    if([DateTime]::UtcNow.Subtract($script:lastStateRefresh).TotalSeconds -ge 1){Refresh-State}
    if($auto.Checked -and -not $script:job -and -not $script:checkJob -and [DateTime]::UtcNow.Subtract($script:lastCheckAttempt).TotalMinutes -ge 15){Start-UpdateCheck}
    if(-not $script:job){return}
    try {
        $parts=@();foreach($name in @('stdout.log','stderr.log')){$file=Join-Path $script:job.Folder $name;if(Test-Path -LiteralPath $file){$parts+=Get-Content -LiteralPath $file -Tail 16 -ErrorAction SilentlyContinue}}
        $output.Text=($parts -join [Environment]::NewLine);$output.SelectionStart=$output.TextLength;$output.ScrollToCaret()
        $script:job.Process.Refresh();if(-not $script:job.Process.HasExited){return}
        $action=$script:job.Action;$succeeded=$false;$message='The task stopped without a result. Open logs for details.'
        if(Test-Path -LiteralPath $script:job.Result){$result=Get-Content -LiteralPath $script:job.Result -Raw|ConvertFrom-Json;$succeeded=$result.succeeded;$message=$result.message}
        $script:job.Process.Dispose();$script:job=$null;$progress.Visible=$false
        Refresh-State;$status.Text=$message
        if($succeeded -and $action -eq 'Setup' -and $script:playAfterSetup){$script:playAfterSetup=$false;Start-Action 'Play' ''}
    }catch {$output.Text=$_.Exception.Message}
})
$form.add_FormClosing({param($sender,$e)if($script:job -and -not $script:job.Process.HasExited){$e.Cancel=$true;$status.Text='Please leave the launcher open until the current task finishes.'}})
Refresh-State
if($SelfTest) {
    switch($TestView) {
        'Available' {$script:candidate=[pscustomobject]@{Name='Public Beta 9 (preview only)';Tag='v0.1.0-beta.9';Cumulative=[pscustomobject]@{size=64MB};Url='https://github.com/badcompany94-rcmp/project-tandem/releases'};Refresh-State}
        'Checking' {$updateStatus.Text='Checking GitHub for a newer Windows release...';$check.Enabled=$false}
        'Offline' {$script:checkError='Could not reach GitHub. You can still play and try again later.';Refresh-State}
    }
    if(-not $Evidence){throw 'SelfTest needs an evidence directory.'}
    New-Item -ItemType Directory -Path $Evidence -Force|Out-Null
    $form.StartPosition='Manual';$form.Location=New-Object Drawing.Point(-32000,-32000);$form.Show()
    $form.CreateControl();foreach($control in $form.Controls){$control.CreateControl();if($control.Right -gt $form.ClientSize.Width -or $control.Bottom -gt $form.ClientSize.Height){throw 'A launcher control is clipped.'}}
    $taskbar=[TandemTaskbarIdentity]::Inspect($form.Handle)
    $taskbar|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $Evidence 'taskbar.json') -Encoding UTF8
    if($taskbar['window_app_id'] -ne [TandemTaskbarIdentity]::AppId -or $taskbar['process_app_id'] -ne [TandemTaskbarIdentity]::AppId -or $taskbar['icon_resource'] -ne ($icon+',0') -or $taskbar['large_icon_handle'] -ne $form.Icon.Handle.ToInt64().ToString() -or $taskbar['small_icon_handle'] -eq '0'){throw 'Launcher taskbar identity or artwork was not applied.'}
    $bitmap=New-Object Drawing.Bitmap($form.Width,$form.Height)
    try {$form.DrawToBitmap($bitmap,(New-Object Drawing.Rectangle(0,0,$bitmap.Width,$bitmap.Height)));$bitmap.Save((Join-Path $Evidence 'launcher.png'),[Drawing.Imaging.ImageFormat]::Png)}finally{$bitmap.Dispose()}
    @{passed=$true;taskbar_identity=$taskbar;buttons=@($buttons|ForEach-Object {$_.Text});state=(Get-TandemLauncherState $Root);no_game_launched=$true;view=$TestView;update_enabled=$updates.Enabled;play_enabled=$play.Enabled;startup_check=$auto.Checked;update_text=$updateStatus.Text}|ConvertTo-Json -Depth 6|Set-Content -LiteralPath (Join-Path $Evidence 'result.json') -Encoding UTF8
}else {$form.add_Shown({if($auto.Checked){Start-UpdateCheck}});$timer.Start();[void]$form.ShowDialog()}
$timer.Stop();$timer.Dispose();if($picture.Image){$picture.Image.Dispose()};$form.Dispose()
