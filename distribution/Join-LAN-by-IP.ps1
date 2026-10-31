$address=Read-Host 'Host local IPv4 address'
if($address){ & (Join-Path $PSScriptRoot 'Launch-Tandem.ps1') -Lan -LanHost $address }
