param([string]$Session)
$ErrorActionPreference='Stop'
$file=Join-Path $PSScriptRoot 'local-data\online-service.json'
if($Session) {
    $sessions=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot 'local-data\sessions')).TrimEnd('\')+'\'
    $folder=[IO.Path]::GetFullPath($Session).TrimEnd('\')
    if(-not $folder.StartsWith($sessions,[StringComparison]::OrdinalIgnoreCase)){throw 'The online session is outside this installation.'}
    # A closing session must never stop a helper started by a later launch.
    $file=Join-Path $folder 'online-service.json'
}
if(-not (Test-Path -LiteralPath $file)){Write-Host 'No owned online helper was recorded.';return}
$state=Get-Content -LiteralPath $file -Raw|ConvertFrom-Json
function Owned([int]$ProcessId,[string]$Path,$Started) {
    try {
        # ConvertFrom-Json returns DateTime on some PowerShell versions and a
        # string on others. Preserve its timezone before comparing UTC times.
        $recorded=if($Started -is [DateTime]){$Started.ToUniversalTime()}else{[DateTimeOffset]::Parse([string]$Started,[Globalization.CultureInfo]::InvariantCulture).UtcDateTime}
        $p=Get-Process -Id $ProcessId -ErrorAction Stop
        if($p.Path -eq $Path -and [Math]::Abs(($p.StartTime.ToUniversalTime()-$recorded).TotalSeconds) -lt 1){return $p}
    }catch{}
    return $null
}
$helper=Owned $state.helper_id $state.helper_path $state.helper_started
if($helper) {
    'stop' | Set-Content -LiteralPath ($state.ready+'.stop') -Encoding ascii
    try{Wait-Process -Id $helper.Id -Timeout 8 -ErrorAction Stop}catch{$helper=Owned $state.helper_id $state.helper_path $state.helper_started;if($helper){Stop-Process -Id $helper.Id}}
}
$upstream=Owned $state.upstream_id $state.upstream_path $state.upstream_started
if($upstream){Stop-Process -Id $upstream.Id}
Write-Host 'Project Tandem online helper stopped.'
