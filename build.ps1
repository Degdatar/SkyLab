param(
    [ValidateSet("Release","Debug")]$Config="Release",
    [switch]$Package,
    [switch]$Clean
)
$ErrorActionPreference="Stop"
if($Clean -and (Test-Path build)){ Remove-Item -Recurse -Force build }
if(!(Test-Path build)){ New-Item -ItemType Directory -Path build | Out-Null }
Push-Location build
cmake .. -A x64 -DCMAKE_BUILD_TYPE=$Config
cmake --build . --config $Config --parallel
Pop-Location
if($Package){
    $ver = (Select-String -Path CMakeLists.txt -Pattern 'VERSION ([0-9.]+)').Matches[0].Groups[1].Value
    $out = "SkyLab-$ver.7z"
    if(Get-Command 7z -ErrorAction SilentlyContinue){ 7z a $out ./contrib/SKSE ./mod -r } else { Compress-Archive -Path contrib,mod -DestinationPath $out -Force }
    Write-Host "Package $out ready"
}
