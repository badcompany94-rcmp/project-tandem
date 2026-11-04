# A separate random identity for community ghost uploads, kept with user data.
# It is never included in packages or sent to the peer-to-peer room service.
function Initialize-TandemTrialIdentity([string]$DataRoot) {
    $folder=Join-Path $DataRoot 'trials'
    [IO.Directory]::CreateDirectory($folder) | Out-Null
    $path=Join-Path $folder 'identity.json'
    if(-not [IO.File]::Exists($path)) {
        $bytes=New-Object byte[] 32
        $rng=[Security.Cryptography.RandomNumberGenerator]::Create()
        try {$rng.GetBytes($bytes)} finally {$rng.Dispose()}
        $key=[Convert]::ToBase64String($bytes).TrimEnd('=').Replace('+','-').Replace('/','_')
        $content=[Text.Encoding]::UTF8.GetBytes((@{scope='public';key=$key}|ConvertTo-Json -Compress))
        $stream=$null
        try {
            $stream=[IO.File]::Open($path,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
            $stream.Write($content,0,$content.Length)
        } catch [IO.IOException] {if(-not [IO.File]::Exists($path)){throw}}
        finally {if($stream){$stream.Dispose()}}
    }
    if((Get-Item -LiteralPath $path).Length -gt 2048){throw 'Trial identity is too large. Restore your saved identity.json.'}
    $config=Get-Content -LiteralPath $path -Raw | ConvertFrom-Json
    if($config.scope -cne 'public' -or $config.key -cnotmatch '^[A-Za-z0-9_-]{43}$') {
        throw 'Trial identity is invalid. Restore your saved identity.json; it was not overwritten.'
    }
    return $path
}
