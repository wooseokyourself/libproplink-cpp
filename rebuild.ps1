$projectRoot = Get-Location

if (Test-Path -Path "build") {
    Write-Host "Removing build dir..." -ForegroundColor Yellow
    Remove-Item -Path "build" -Recurse -Force
}

Write-Host "Creating build dir..." -ForegroundColor Green
New-Item -Path "build" -ItemType Directory | Out-Null

Set-Location -Path "build"

Write-Host "Running CMake configuration..." -ForegroundColor Cyan
cmake .. -G "Visual Studio 17 2022" -A x64 -T v142 -DCMAKE_TOOLCHAIN_FILE=C:/Users/woose/vcpkg/scripts/buildsystems/vcpkg.cmake

Write-Host "Building project..." -ForegroundColor Cyan
cmake --build . --config Release

Set-Location -Path $projectRoot

Write-Host "Build completed!" -ForegroundColor Green