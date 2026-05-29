$sln = Join-Path (Split-Path -Parent $MyInvocation.MyCommand.Path) "projects\lgpt.vcxproj"

Write-Host "Cleaning Debug|x64..."
msbuild $sln /t:Clean /p:Configuration=Debug /p:Platform=x64 /m /nologo
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "Building Debug|x64..."
msbuild $sln /t:Build /p:Configuration=Debug /p:Platform=x64 /m /nologo /v:minimal
exit $LASTEXITCODE
