param([Parameter(Mandatory=$true)][string]$Root,[string]$TransactionId,[switch]$Recover,
      [int]$WaitId=0,[string]$WaitStarted,[int]$BootstrapId=0,[string]$BootstrapStarted,[switch]$Headless,
      [string]$ReadyFile,[switch]$SelfTest,[string]$Evidence)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'Updater-Core.ps1')
. (Join-Path $PSScriptRoot 'Tandem-Paths.ps1')
. (Join-Path $PSScriptRoot 'Launcher-Core.ps1')
$Root=[IO.Path]::GetFullPath($Root)
if($SelfTest -and ($Headless -or -not $Evidence)){throw 'Updater render tests need a GUI and an evidence directory.'}
$script:cancel=$false;$script:installing=$false;$script:finished=$false;$script:restart=$false
if(-not $Headless) {
    Add-Type -AssemblyName System.Windows.Forms,System.Drawing
    [Windows.Forms.Application]::EnableVisualStyles()
    $form=New-Object Windows.Forms.Form;$form.Text='Project Tandem - Update';$form.ClientSize=New-Object Drawing.Size(680,330)
    $form.StartPosition='CenterScreen';$form.FormBorderStyle='FixedDialog';$form.MaximizeBox=$false
    $form.BackColor=[Drawing.Color]::FromArgb(8,28,83);$form.ForeColor=[Drawing.Color]::White;$form.Font=New-Object Drawing.Font('Segoe UI',11)
    $icon=Join-Path $PSScriptRoot 'artwork.ico';$identity=Join-Path $PSScriptRoot 'TaskbarIdentity.cs'
    if(Test-Path -LiteralPath $icon){$form.Icon=New-Object Drawing.Icon($icon)}
    if((Test-Path -LiteralPath $identity) -and (Test-Path -LiteralPath $icon)) {
        Add-Type -Path $identity
        [TandemTaskbarIdentity]::InitializeProcess()
        $form.add_HandleCreated({[TandemTaskbarIdentity]::Apply($form.Handle,$Root,$icon,$form.Icon.Handle)})
    }
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class TandemUpdateWindow {
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr window);
}
'@
    $heading=New-Object Windows.Forms.Label;$heading.Text='UPDATING PROJECT TANDEM';$heading.SetBounds(25,20,620,45)
    $heading.Font=New-Object Drawing.Font('Segoe UI',22);$heading.ForeColor=[Drawing.Color]::FromArgb(255,220,90);$form.Controls.Add($heading)
    $stageLabel=New-Object Windows.Forms.Label;$stageLabel.SetBounds(27,84,620,34);$stageLabel.Text='Preparing update';$form.Controls.Add($stageLabel)
    $detailLabel=New-Object Windows.Forms.Label;$detailLabel.SetBounds(27,122,620,85);$detailLabel.Text='This window stays open while the update downloads, installs and builds.';$form.Controls.Add($detailLabel)
    $bar=New-Object Windows.Forms.ProgressBar;$bar.SetBounds(28,216,620,15);$bar.Style='Marquee';$form.Controls.Add($bar)
    $button=New-Object Windows.Forms.Button;$button.Text='Cancel download';$button.SetBounds(442,260,205,40)
    $button.FlatStyle='Flat';$button.BackColor=[Drawing.Color]::FromArgb(20,61,135);$form.Controls.Add($button)
    $button.add_Click({if($script:finished){$script:restart=$true;$form.Close()}elseif(-not $script:installing){$script:cancel=$true;$button.Enabled=$false;$detailLabel.Text='Cancelling safely...'}})
    $form.add_FormClosing({param($sender,$e)if(-not $script:finished){$e.Cancel=$true;if(-not $script:installing){$script:cancel=$true}else{$detailLabel.Text='Please wait while installation or recovery finishes.'}}})
    if($SelfTest){$form.StartPosition='Manual';$form.Location=New-Object Drawing.Point(-32000,-32000);$form.ShowInTaskbar=$false}
    $form.WindowState='Normal';$form.Show()
    if(-not $SelfTest){$form.BringToFront();$form.Activate()}
    $form.Refresh();[Windows.Forms.Application]::DoEvents()
    $visible=[TandemUpdateWindow]::IsWindowVisible($form.Handle)
    if($ReadyFile){$null=Assert-TandemSafePath $Root $ReadyFile;Write-TandemJson $ReadyFile @{ProcessId=$PID;Visible=$visible}}
    if(-not $visible -and -not $SelfTest){throw 'The update progress window is not visible.'}
}
$progress={param($stage,$detail,$percent)
    if($stage -in @('Installing','Building','Final checks','Restoring')){$script:installing=$true}
    if(-not $Headless) {
        $stageLabel.Text=$stage;$detailLabel.Text=$detail
        if($percent -ge 0){$bar.Style='Continuous';$bar.Value=[Math]::Max(0,[Math]::Min(100,$percent))}else{$bar.Style='Marquee'}
        $button.Enabled=(-not $script:installing);[Windows.Forms.Application]::DoEvents()
    }
    if($script:cancel -and -not $script:installing){throw (New-Object OperationCanceledException('Update cancelled. Your installed game is unchanged.'))}
}
if($SelfTest) {
    [IO.Directory]::CreateDirectory($Evidence)|Out-Null
    $views=@(@('Downloading','24.0 / 60.0 MB',40),@('Verifying files','Checking the package files.',65),@('Installing','Installing verified files.',-1),@('Building','4/5 Build game 50% 222 / 444 build steps',50),@('Final checks','Verifying the installed release.',100))
    $initial=$form.Handle.ToInt64();$results=@()
    foreach($view in $views) {
        & $progress $view[0] $view[1] $view[2]
        $bitmap=New-Object Drawing.Bitmap($form.Width,$form.Height)
        try {$form.DrawToBitmap($bitmap,(New-Object Drawing.Rectangle(0,0,$bitmap.Width,$bitmap.Height)));$bitmap.Save((Join-Path $Evidence ($view[0].Replace(' ','-')+'.png')),[Drawing.Imaging.ImageFormat]::Png)}finally{$bitmap.Dispose()}
        $results+=@{stage=$stageLabel.Text;detail=$detailLabel.Text;visible=[TandemUpdateWindow]::IsWindowVisible($form.Handle);handle=$form.Handle.ToInt64();cancel_enabled=$button.Enabled;percent=$bar.Value}
    }
    Write-TandemJson (Join-Path $Evidence 'result.json') @{passed=$visible;initial_handle=$initial;views=$results;no_install=$true}
    $script:finished=$true;$form.Dispose();exit 0
}
function Wait-TandemParent([int]$ProcessId,[string]$Started) {
    if(-not $ProcessId){return}
    $deadline=[DateTime]::UtcNow.AddSeconds(30)
    do {
        try {$parent=Get-Process -Id $ProcessId -ErrorAction Stop;if($parent.StartTime.ToUniversalTime().ToString('o') -ne $Started){return}}catch{return}
        if([DateTime]::UtcNow -gt $deadline){throw 'The old launcher did not close. Close it, then reopen Project Tandem to recover.'}
        if(-not $Headless){[Windows.Forms.Application]::DoEvents()};Start-Sleep -Milliseconds 100
    }while($true)
}
$lock=$null;$record=$null;$claimed=$false;$success=$false;$message='';$errorLog=$null
try {
    # The visible UI acknowledges startup before the launcher exits and releases
    # its handoff lock. Wait before acquiring that lock, for normal updates too.
    Wait-TandemParent $WaitId $WaitStarted;Wait-TandemParent $BootstrapId $BootstrapStarted
    $lock=Get-TandemUpdateLock $Root
    $marker=Join-Path $Root 'local-data/update-transaction.json'
    $record=Read-TandemJson $marker
    $where=Get-TandemJournal $Root $record;$errorLog=Join-Path $where.Folder 'updater.log'
    [IO.Directory]::CreateDirectory($where.Folder)|Out-Null
    if($Recover) {
        if((Get-TandemUpdateStatus $Root).Active){throw 'An updater is already running. Return to that window.'}
    }elseif($record.Id -ne $TransactionId){throw 'The update handoff does not match this installation.'}
    Set-TandemTransaction $Root $record 'Starting';$claimed=$true
    Assert-TandemClosed $Root
    if($Recover) {
        Restore-TandemUpdate $Root $record $progress
        $message='The interrupted update was recovered. Reopening the launcher.';$success=$true
    }else {
        Send-TandemProgress $progress 'Checking release' 'Confirming the selected release on GitHub.'
        $release=Get-TandemReleaseJson ('https://api.github.com/repos/'+$script:TandemRepository+'/releases/tags/'+$record.Tag)
        $candidate=ConvertTo-TandemCandidate $release
        if(-not $candidate -or $candidate.Tag -ne $record.Tag -or [version]$candidate.Version -le (Get-TandemVersion (Get-TandemInstalledTag $Root))){throw 'This release is no longer a newer supported update. Check for updates again.'}
        if(-not (Get-TandemCachedImage $Root)){throw 'Run setup/repair once to cache your disc before using automatic updates.'}
        Set-TandemTransaction $Root $record 'Downloading'
        $asset=if($candidate.Cumulative){$candidate.Cumulative}else{$candidate.Full}
        $archive=Save-TandemAsset $asset $where.Folder $progress
        $stage=Join-Path $where.Folder 'staged'
        try {$manifest=Expand-TandemUpdate $archive $stage $Root $candidate $progress}
        catch {
            if($_.Exception.Message -notmatch 'TANDEM_FULL_PACKAGE_REQUIRED' -or $asset.name -eq $candidate.Full.name){throw}
            Send-TandemProgress $progress 'Repairing tools' 'Downloading the full package to replace missing or changed build tools.'
            $archive=Save-TandemAsset $candidate.Full $where.Folder $progress
            $stage=Join-Path $where.Folder 'staged-full';$manifest=Expand-TandemUpdate $archive $stage $Root $candidate $progress
        }
        Install-TandemUpdate $Root $stage $manifest $record $progress
        $success=$true;$message='Update installed. Reopening the launcher.'
    }
}catch {
    $message=$_.Exception.Message
    if($errorLog){Add-Content -LiteralPath $errorLog -Value ([DateTime]::UtcNow.ToString('o')+' '+$message)}
    if($claimed) {
        try {Restore-TandemUpdate $Root $record $progress;$message+=' Your previous version is ready to use.'}
        catch {$message+=' Recovery needs attention: '+$_.Exception.Message;Set-TandemTransaction $Root $record 'RecoveryNeeded'}
    }
}finally {if($lock){$lock.Dispose()}}
if($Headless){Write-Output $message;if(-not $success){exit 1};exit 0}
$script:finished=$true;$stageLabel.Text=if($success){'Ready to play'}else{'Update did not finish'}
$detailLabel.Text=$message;$bar.Visible=$false;$button.Text='Back to launcher';$button.Enabled=$true
if($success){$script:restart=$true;$form.Close()}else{$form.Hide();[void]$form.ShowDialog()}
$form.Dispose()
if($script:restart -and -not (Get-TandemUpdateStatus $Root).Pending){Start-Process -FilePath (Join-Path $Root 'ProjectTandem.exe') -WorkingDirectory $Root -WindowStyle Normal|Out-Null}
