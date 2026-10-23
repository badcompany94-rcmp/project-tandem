param([string]$GameImage,[int]$Jobs=8,[switch]$VerifyOnly)
$ErrorActionPreference='Stop'
$root=$PSScriptRoot
. (Join-Path $root 'Setup-Progress.ps1')
. (Join-Path $root 'Tandem-Paths.ps1')
$savedSettings=Get-TandemSettings $root
$data=Join-Path $root 'local-data'
New-Item -ItemType Directory -Force -Path $data | Out-Null
$manifest=Get-Content -LiteralPath (Join-Path $root 'package-manifest.json') -Raw | ConvertFrom-Json
Write-Host 'Verifying the Project Tandem package...'
$verified=0
Show-TandemProgress '1/5 Verify package' 0 "0 / $($manifest.files.Count) files"
foreach($entry in $manifest.files) {
    $file=[IO.Path]::GetFullPath((Join-Path $root $entry.path))
    if(-not $file.StartsWith(([IO.Path]::GetFullPath($root)+'\'),[StringComparison]::OrdinalIgnoreCase)) {throw 'Invalid package manifest path.'}
    if(-not (Test-Path -LiteralPath $file -PathType Leaf) -or (Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash -ne $entry.sha256) {throw "Package file is missing or changed: $($entry.path). Extract the original ZIP again."}
    $verified++
    Show-TandemProgress '1/5 Verify package' ([int][Math]::Floor(100*$verified/$manifest.files.Count)) "$verified / $($manifest.files.Count) files"
}
Show-TandemProgress '1/5 Verify package' 100 "$verified files checked" -Complete
if($VerifyOnly){Write-Host 'Package integrity verified.';return}
if(-not [Environment]::Is64BitOperatingSystem){throw 'This package requires 64-bit Windows 10 or 11.'}
# All compilation dependencies are in this package. Ignore any inherited Visual
# Studio/SDK environment so local installations cannot alter the friend build.
foreach($key in @('INCLUDE','LIB','LIBPATH','CL','_CL_','CFLAGS','CXXFLAGS','LDFLAGS','CPATH','C_INCLUDE_PATH','CPLUS_INCLUDE_PATH','VCINSTALLDIR','VSINSTALLDIR','WindowsSdkDir','WindowsSDKVersion','VCToolsInstallDir','UniversalCRTSdkDir','UCRTVersion')){[Environment]::SetEnvironmentVariable($key,$null,'Process')}
Assert-TandemClosed $root
$GameImage=Resolve-TandemImage $root $savedSettings $GameImage
if(-not $GameImage){Write-Host 'Setup cancelled.';return}
$GameImage=(Resolve-Path -LiteralPath $GameImage).Path
$python=Join-Path $root 'tools\python\python.exe'
$cmake=Join-Path $root 'tools\cmake\bin\cmake.exe'
$llvm=Join-Path $root 'tools\llvm-mingw\bin'
$ninja=Join-Path $root 'tools\ninja\ninja.exe'
$env:PATH="$llvm;$env:SystemRoot\System32;$env:SystemRoot"
$source=Join-Path $root 'source'
$build=Join-Path $data 'build-portable'
New-Item -ItemType Directory -Force -Path $build | Out-Null
$log=Join-Path $data 'setup.log'
function Run-Tool([string]$Program,[string[]]$Arguments) {
    Invoke-TandemSetupTool -Program $Program -Arguments $Arguments -Log $log -Stage $script:SetupStage
}
Write-Host 'Checking your disc revision...'
$script:SetupStage='2/5 Check disc'
Run-Tool $python @((Join-Path $source 'gcn-recomp\recomp\extract_dol.py'),$GameImage,(Join-Path $data 'verified-main.dol'),'GM4E01','e389e50357d4a5076ab3d38672204658f411123d')
$GameImage=Save-TandemImageCache $root $GameImage
if(Test-TandemBuildMoved $root) {
    # CMake/generated code records absolute paths. Preserve the old generated
    # build and make a fresh one; saves/settings live outside this directory.
    $dataFull=[IO.Path]::GetFullPath($data).TrimEnd('\')+'\'
    $oldBuild=[IO.Path]::GetFullPath($build)
    $archive=[IO.Path]::GetFullPath((Join-Path $data ('build-backups\'+[guid]::NewGuid().ToString('N'))))
    if(-not $oldBuild.StartsWith($dataFull,[StringComparison]::OrdinalIgnoreCase) -or
       -not $archive.StartsWith($dataFull,[StringComparison]::OrdinalIgnoreCase) -or
       ((Get-Item -LiteralPath $oldBuild).Attributes -band [IO.FileAttributes]::ReparsePoint)){throw 'Unsafe build-cache location; repair cancelled.'}
    Write-Host 'The game folder moved. Preserving the old compiler cache and rebuilding at its new location.'
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $archive)|Out-Null
    Move-Item -LiteralPath $oldBuild -Destination $archive
    New-Item -ItemType Directory -Force -Path $build|Out-Null
}
Write-Host 'Compiling locally from your disc. This may take several minutes.'
$script:SetupStage='3/5 Configure'
Run-Tool $cmake @('-S',$source,'-B',$build,'-G','Ninja','-DCMAKE_BUILD_TYPE=RelWithDebInfo',"-DCMAKE_MAKE_PROGRAM=$ninja","-DCMAKE_C_COMPILER=$llvm\x86_64-w64-mingw32-clang.exe","-DCMAKE_CXX_COMPILER=$llvm\x86_64-w64-mingw32-clang++.exe",'-DCMAKE_EXE_LINKER_FLAGS=-static',"-DPython3_EXECUTABLE=$python","-DGAME_ISO=$GameImage",'-DGCN_OPENXR=OFF','-DMKDD_EOS=OFF','-DGCN_INPUT_TESTS=OFF','-DGCN_NET_TESTS=OFF','-DMKDD_ONLINE_TESTS=OFF',"-DFETCHCONTENT_SOURCE_DIR_SDL2=$(Join-Path $root 'tools\SDL2')")
$script:SetupStage='4/5 Build game'
Run-Tool $cmake @('--build',$build,'--target','mkdd','--parallel',([Math]::Max(1,[Math]::Min($Jobs,16))))
Show-TandemProgress '5/5 Final checks' -1 'Checking multiplayer compatibility'
$identity=Get-Content -LiteralPath (Join-Path $build 'identity\build-identity.json') -Raw | ConvertFrom-Json
if($identity.id -ne $manifest.compatibility){throw 'This compiler or build configuration differs from the playtest package. See START-HERE.txt; multiplayer requires matching builds.'}
Save-TandemSettings $root $savedSettings $GameImage
$pending=Join-Path $data 'update-pending.json'
if(Test-Path -LiteralPath $pending){Remove-Item -LiteralPath $pending}
Show-TandemProgress '5/5 Final checks' 100 'Ready to play' -Complete
Write-Host 'Project Tandem is ready. Use Launch-Tandem.cmd. If your ISO moves, the launcher will help locate it.'
