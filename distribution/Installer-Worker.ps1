param([Parameter(Mandatory=$true)][string]$Root,[Parameter(Mandatory=$true)][ValidateSet('Download','Build')][string]$Action,
      [Parameter(Mandatory=$true)][string]$Result,[Parameter(Mandatory=$true)][string]$ProgressFile,
      [Parameter(Mandatory=$true)][string]$Bootstrap,[string]$GameImage,[string]$CancelFile,[Parameter(Mandatory=$true)][string]$AdmissionFile)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'Installer-Core.ps1')
$lock=$null;$record=@{succeeded=$false;message='';action=$Action}
try{
    # The visible installer owns a kill-on-close job. Wait for admission before
    # starting downloads or any compiler descendants, including on a fast PC.
    $deadline=[DateTime]::UtcNow.AddSeconds(30)
    do{
        $admitted=$false
        if(Test-Path -LiteralPath $AdmissionFile){
            $owner=Read-TandemJson $AdmissionFile
            $admitted=$owner.id -eq $PID -and $owner.started -eq (Get-Process -Id $PID).StartTime.ToUniversalTime().ToString('o')
        }
        if($admitted){break}
        if([DateTime]::UtcNow -gt $deadline){throw 'The installer worker was not admitted. Please retry.'}
        Start-Sleep -Milliseconds 50
    }while($true)
    $lock=Initialize-TandemInstall $Root $Bootstrap
    $progress={param($stage,$detail,$percent)
        if($CancelFile -and (Test-Path -LiteralPath $CancelFile)){throw (New-Object OperationCanceledException('Download cancelled. You can retry the installation.'))}
        Write-TandemJson $ProgressFile @{stage=$stage;detail=$detail;percent=$percent}
    }
    if($Action -eq 'Download'){
        & $progress 'Checking GitHub' 'Finding the latest Windows release.' -1
        $candidate=Get-TandemInstallRelease
        $manifest=Install-TandemPackage $Root $candidate $progress
        $record.release=[string]$manifest.release
    }else{
        if(-not $GameImage -or -not (Test-Path -LiteralPath $GameImage -PathType Leaf)){throw 'Select a supported USA Double Dash ISO or CISO.'}
        & (Join-Path $Root 'Setup-Tandem.ps1') -GameImage $GameImage
        if(-not (Get-TandemLauncherState $Root).Ready){throw 'Setup did not finish. Check the details and try again.'}
    }
    $record.succeeded=$true
}catch{$record.message=$_.Exception.Message;[Console]::Error.WriteLine($record.message)}
finally{if($lock){$lock.Dispose()};Write-TandemJson $Result $record}
if(-not $record.succeeded){exit 1}
