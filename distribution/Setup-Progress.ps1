# Console-only progress: file/build counts are real; unknown durations animate
# without inventing a percentage. Works in stock Windows PowerShell 5.1.
$script:TandemProgressLive=($Host.Name -eq 'ConsoleHost' -and -not [Console]::IsOutputRedirected)
$script:TandemProgressClock=[Diagnostics.Stopwatch]::StartNew()
$script:TandemProgressLast=-1000
$script:TandemProgressWidth=0
$script:TandemProgressKey=''
function Show-TandemProgress([string]$Stage,[int]$Percent=-1,[string]$Detail='',[switch]$Complete) {
    $now=$script:TandemProgressClock.ElapsedMilliseconds
    $key=$Stage+':'+[Math]::Floor($Percent/5)
    if(-not $Complete -and $now-$script:TandemProgressLast -lt 100){return}
    if(-not $script:TandemProgressLive -and -not $Complete -and $key -eq $script:TandemProgressKey){return}
    $script:TandemProgressKey=$key;$script:TandemProgressLast=$now
    $width=20
    if($Percent -ge 0){
        $Percent=[Math]::Max(0,[Math]::Min(100,$Percent));$filled=[int][Math]::Floor($width*$Percent/100)
        $bar=('='*$filled)+('.'*($width-$filled));$amount=('{0,3}%' -f $Percent)
    } else {
        $point=[int]([Math]::Floor($now/150)%$width)
        $bar=('.'*$point)+'>'+('.'*($width-$point-1));$amount='    '
    }
    # Original little driver: blinks while the kart rolls along its own track.
    $driver=if(([int][Math]::Floor($now/180)%8) -eq 7){'o[_-_]o'}else{'o[_^_]o'}
    $lane=[int]([Math]::Floor($now/160)%9)
    $kart=(' '*$lane)+$driver+(' '*(8-$lane))
    $line="$Stage [$bar] $amount [$kart] $Detail"
    if($script:TandemProgressLive){
        try{$columns=[Math]::Max(40,[Console]::WindowWidth-1)}catch{$columns=110}
        if($line.Length -gt $columns){$line=$line.Substring(0,$columns)}
        $padding=[Math]::Max(0,[Math]::Min($script:TandemProgressWidth,$columns)-$line.Length)
        Write-Host -NoNewline ("`r"+$line+(' '*$padding))
        $script:TandemProgressWidth=$line.Length
        if($Complete){Write-Host ''; $script:TandemProgressWidth=0}
    } else {Write-Host "$Stage $(if($Percent -ge 0){"$Percent%"}) $Detail"}
}
function Stop-TandemProgress {
    if($script:TandemProgressLive -and $script:TandemProgressWidth){Write-Host ''; $script:TandemProgressWidth=0}
}
function ConvertTo-TandemArgument([string]$Value) {
    # CommandLineToArgvW quoting, including literal quotes and trailing slashes.
    '"'+[regex]::Replace([regex]::Replace($Value,'(\\*)"','$1$1\"'),'(\\+)$','$1$1')+'"'
}
function Invoke-TandemSetupTool([string]$Program,[string[]]$Arguments,[string]$Log,[string]$Stage) {
    $info=New-Object Diagnostics.ProcessStartInfo
    $info.FileName=$Program
    $info.Arguments=($Arguments | ForEach-Object {ConvertTo-TandemArgument $_}) -join ' '
    $info.UseShellExecute=$false;$info.CreateNoWindow=$true
    $info.RedirectStandardOutput=$true;$info.RedirectStandardError=$true
    $process=New-Object Diagnostics.Process
    $process.StartInfo=$info
    $writer=New-Object IO.StreamWriter($Log,$true,[Text.UTF8Encoding]::new($false))
    $writer.AutoFlush=$true
    $started=$false;$success=$false;$recent=New-Object 'Collections.Generic.Queue[string]'
    try {
        if(-not $process.Start()){throw 'Could not start setup tool.'};$started=$true
        $pending=@($process.StandardOutput.ReadLineAsync(),$process.StandardError.ReadLineAsync())
        $readers=@($process.StandardOutput,$process.StandardError)
        $percent=-1;$detail='Working; details saved in local-data\setup.log'
        while($null -ne $pending[0] -or $null -ne $pending[1]) {
            for($i=0;$i -lt 2;$i++) {
                # Drain available lines without waiting on a silent stream.
                $drained=0
                while($null -ne $pending[$i] -and $pending[$i].IsCompleted -and $drained++ -lt 100) {
                    $line=$pending[$i].GetAwaiter().GetResult()
                    if($null -eq $line){$pending[$i]=$null;break}
                    $writer.WriteLine($line);$recent.Enqueue($line)
                    if($recent.Count -gt 12){[void]$recent.Dequeue()}
                    if($line -match '^\[(\d+)/(\d+)\]') {
                        $done=[int]$Matches[1];$total=[int]$Matches[2]
                        if($total -gt 0){$percent=[Math]::Min(99,[int][Math]::Floor(100*$done/$total));$detail="$done / $total build steps"}
                    }
                    $pending[$i]=$readers[$i].ReadLineAsync()
                }
            }
            Show-TandemProgress $Stage $percent $detail
            Start-Sleep -Milliseconds 100
        }
        $process.WaitForExit()
        if($process.ExitCode -ne 0){
            Stop-TandemProgress
            foreach($line in $recent){Write-Host $line}
            throw "Setup failed (exit $($process.ExitCode)). See local-data\setup.log."
        }
        Show-TandemProgress $Stage 100 'Complete' -Complete
        $success=$true
    } finally {
        # Stop only the process this invocation owns if setup is interrupted.
        if($started -and -not $success -and -not $process.HasExited){$process.Kill();$process.WaitForExit()}
        Stop-TandemProgress;$writer.Dispose();$process.Dispose()
    }
}
