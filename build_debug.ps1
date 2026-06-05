$sln = Join-Path (Split-Path -Parent $MyInvocation.MyCommand.Path) "projects\lgpt.vcxproj"

$nativeArch = (Get-ItemProperty "HKLM:\SYSTEM\CurrentControlSet\Control\Session Manager\Environment" -Name PROCESSOR_ARCHITECTURE).PROCESSOR_ARCHITECTURE
$platform = if ($nativeArch -eq "ARM64") { "ARM64" } else { "x64" }

Write-Host "Cleaning Debug|$platform..."
msbuild $sln /t:Clean /p:Configuration=Debug /p:Platform=$platform /m /nologo
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "Building Debug|$platform..."
msbuild $sln /t:Build /p:Configuration=Debug /p:Platform=$platform /m /nologo /v:minimal
exit $LASTEXITCODE
