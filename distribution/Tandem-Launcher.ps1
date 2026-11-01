param([string]$Root=$PSScriptRoot,[switch]$SelfTest,[string]$Evidence)
$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
. (Join-Path $PSScriptRoot 'Launcher-Core.ps1')
. (Join-Path $PSScriptRoot 'Tandem-Paths.ps1')
[Windows.Forms.Application]::EnableVisualStyles()
$Root=[IO.Path]::GetFullPath($Root)
$script:job=$null;$script:latestLog=$null
if($SelfTest) {
    Add-Type -ReferencedAssemblies System.Windows.Forms,System.Drawing -TypeDefinition @'
public class TandemRenderForm : System.Windows.Forms.Form {
    protected override bool ShowWithoutActivation {get{return true;}}
}
'@
    $form=New-Object TandemRenderForm
    $form.ShowInTaskbar=$false;$form.Opacity=0
}else {$form=New-Object Windows.Forms.Form}
$form.Text='Project Tandem';$form.ClientSize=New-Object Drawing.Size(880,610)
$form.MinimumSize=New-Object Drawing.Size(896,649);$form.StartPosition='CenterScreen'
$form.BackColor=[Drawing.Color]::FromArgb(8,28,83);$form.ForeColor=[Drawing.Color]::White
$form.Font=New-Object Drawing.Font('Segoe UI',11)
$form.AutoScaleMode=[Windows.Forms.AutoScaleMode]::Dpi
$icon=Join-Path $Root 'source/distribution/assets/project-tandem.ico'
if(Test-Path -LiteralPath $icon){$form.Icon=New-Object Drawing.Icon($icon)}
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
$picture=New-Object Windows.Forms.PictureBox;$picture.Location=New-Object Drawing.Point(704,14);$picture.Size=New-Object Drawing.Size(146,146)
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
$updates=Button 'Update downloads' 317
$help=Button 'Help & controls' 385
$help.UseMnemonic=$false
$logs=Button 'Open logs' 453
$status=Label '' 297 181 550 56 14
$version=Label '' 297 245 550 28
$build=Label '' 297 277 550 26
$matching=Label 'Online players need the same build. Settings are available in-game with F1.' 297 312 550 48
$progress=New-Object Windows.Forms.ProgressBar;$progress.Location=New-Object Drawing.Point(300,371);$progress.Size=New-Object Drawing.Size(546,8);$progress.Style='Marquee';$progress.Visible=$false;$form.Controls.Add($progress)
$output=New-Object Windows.Forms.TextBox;$output.Multiline=$true;$output.ReadOnly=$true;$output.ScrollBars='Vertical'
$output.Location=New-Object Drawing.Point(300,393);$output.Size=New-Object Drawing.Size(546,135)
$output.BackColor=[Drawing.Color]::FromArgb(5,18,52);$output.ForeColor=[Drawing.Color]::FromArgb(220,232,255)
$output.Font=New-Object Drawing.Font('Consolas',9);$form.Controls.Add($output)
$footer=Label 'Launcher preview  |  Update downloads opens GitHub; updates are still installed manually.' 31 553 820 40 10
function Refresh-State {
    $state=Get-TandemLauncherState $Root
    $status.Text=$state.Detail;$version.Text=$state.Release;$build.Text='Build: '+$state.Compatibility
    $play.Text=if($state.Ready){'PLAY'}else{'SET UP & PLAY'}
    $play.Enabled=$state.PackageValid;$setup.Enabled=$state.PackageValid
}
function Start-Action([string]$Action,[string]$Image) {
    if($script:job){return}
    try {
        $script:job=New-TandemLauncherJob $Root $Action $Image (Join-Path $PSScriptRoot 'Launcher-Action.ps1')
        $script:latestLog=$script:job.Folder;$progress.Visible=$true
        foreach($b in $buttons){$b.Enabled=$false};$logs.Enabled=$true
        $status.Text=if($Action -eq 'Play'){'Starting the game and networking...'}else{'Preparing your game. The first build can take several minutes.'}
        $output.Text='Progress and errors will appear here. Saves and controller settings are kept.'
    }catch {$status.Text='Unable to start.';$output.Text=$_.Exception.Message;$script:job=$null}
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
$updates.add_Click({Start-Process 'https://github.com/badcompany94-rcmp/project-tandem/releases'})
$help.add_Click({$file=Join-Path $Root 'docs/CONTROLS.md';if(Test-Path -LiteralPath $file){Start-Process -FilePath 'notepad.exe' -ArgumentList (Join-TandemArguments @($file))}else{Start-Process 'https://github.com/badcompany94-rcmp/project-tandem#readme'}})
$logs.add_Click({$folder=if($script:latestLog){$script:latestLog}else{Join-Path $Root 'local-data'};if(Test-Path -LiteralPath $folder){Start-Process -FilePath 'explorer.exe' -ArgumentList (Join-TandemArguments @($folder))}})
$timer=New-Object Windows.Forms.Timer;$timer.Interval=400
$timer.add_Tick({
    if(-not $script:job){return}
    try {
        $parts=@();foreach($name in @('stdout.log','stderr.log')){$file=Join-Path $script:job.Folder $name;if(Test-Path -LiteralPath $file){$parts+=Get-Content -LiteralPath $file -Tail 16 -ErrorAction SilentlyContinue}}
        $output.Text=($parts -join [Environment]::NewLine);$output.SelectionStart=$output.TextLength;$output.ScrollToCaret()
        $script:job.Process.Refresh();if(-not $script:job.Process.HasExited){return}
        $action=$script:job.Action;$succeeded=$false;$message='The task stopped without a result. Open logs for details.'
        if(Test-Path -LiteralPath $script:job.Result){$result=Get-Content -LiteralPath $script:job.Result -Raw|ConvertFrom-Json;$succeeded=$result.succeeded;$message=$result.message}
        $script:job.Process.Dispose();$script:job=$null;$progress.Visible=$false;foreach($b in $buttons){$b.Enabled=$true}
        Refresh-State;$status.Text=$message
        if($succeeded -and $action -eq 'Setup' -and $script:playAfterSetup){$script:playAfterSetup=$false;Start-Action 'Play' ''}
    }catch {$output.Text=$_.Exception.Message}
})
$form.add_FormClosing({param($sender,$e)if($script:job -and -not $script:job.Process.HasExited){$e.Cancel=$true;$status.Text='Please leave the launcher open until the current task finishes.'}})
Refresh-State
if($SelfTest) {
    if(-not $Evidence){throw 'SelfTest needs an evidence directory.'}
    New-Item -ItemType Directory -Path $Evidence -Force|Out-Null
    $form.StartPosition='Manual';$form.Location=New-Object Drawing.Point(-32000,-32000);$form.Show()
    $form.CreateControl();foreach($control in $form.Controls){$control.CreateControl();if($control.Right -gt $form.ClientSize.Width -or $control.Bottom -gt $form.ClientSize.Height){throw 'A launcher control is clipped.'}}
    $bitmap=New-Object Drawing.Bitmap($form.Width,$form.Height)
    try {$form.DrawToBitmap($bitmap,(New-Object Drawing.Rectangle(0,0,$bitmap.Width,$bitmap.Height)));$bitmap.Save((Join-Path $Evidence 'launcher.png'),[Drawing.Imaging.ImageFormat]::Png)}finally{$bitmap.Dispose()}
    @{passed=$true;buttons=@($buttons|ForEach-Object {$_.Text});state=(Get-TandemLauncherState $Root);no_game_launched=$true}|ConvertTo-Json -Depth 6|Set-Content -LiteralPath (Join-Path $Evidence 'result.json') -Encoding UTF8
}else {$timer.Start();[void]$form.ShowDialog()}
$timer.Stop();$timer.Dispose();if($picture.Image){$picture.Image.Dispose()};$form.Dispose()
