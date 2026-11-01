param([Parameter(Mandatory=$true)][string]$Root,[string]$Output)
$ErrorActionPreference='Stop'
$compiler=Join-Path $env:SystemRoot 'Microsoft.NET/Framework64/v4.0.30319/csc.exe'
if(-not (Test-Path -LiteralPath $compiler)){throw 'The Windows .NET Framework C# compiler is unavailable. Launch-Tandem.cmd still works.'}
$source=Join-Path $Root 'source/distribution/launcher/ProjectTandem.cs'
$icon=Join-Path $Root 'source/distribution/assets/project-tandem.ico'
if(-not $Output){$Output=Join-Path $Root 'ProjectTandem.exe'}
& $compiler /nologo /target:winexe /optimize+ /reference:System.Windows.Forms.dll ("/win32icon:"+$icon) ("/out:"+$Output) $source
if($LASTEXITCODE -ne 0){throw 'Could not build the graphical launcher.'}
