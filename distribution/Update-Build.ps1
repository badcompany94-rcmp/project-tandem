param([Parameter(Mandatory=$true)][string]$Root,[Parameter(Mandatory=$true)][string]$GameImage)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'Updater-Core.ps1')
# Wait until the parent assigns this process to its kill-on-close Windows job.
# No compiler can escape the job during this admission handshake.
$deadline=[DateTime]::UtcNow.AddSeconds(30)
do {
    $owner=Read-TandemJson (Join-Path $Root 'local-data/update-transaction.json')
    if($owner.BuildOwnerId -eq $PID -and $owner.BuildOwnerStarted -eq (Get-Process -Id $PID).StartTime.ToUniversalTime().ToString('o')){break}
    if([DateTime]::UtcNow -gt $deadline){throw 'The update build was not admitted.'}
    Start-Sleep -Milliseconds 100
}while($true)
& (Join-Path $Root 'Setup-Tandem.ps1') -GameImage $GameImage
