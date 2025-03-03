# rebuild.ps1
# 프로젝트 빌드 스크립트

# 현재 디렉토리 저장
$projectRoot = Get-Location

# build 디렉토리가 존재하면 제거
if (Test-Path -Path "build") {
    Write-Host "기존 build 폴더 제거 중..." -ForegroundColor Yellow
    Remove-Item -Path "build" -Recurse -Force
}

# 새 build 디렉토리 생성
Write-Host "새 build 폴더 생성 중..." -ForegroundColor Green
New-Item -Path "build" -ItemType Directory | Out-Null

# build 디렉토리로 이동
Set-Location -Path "build"

# CMake 구성 실행
Write-Host "CMake 구성 실행 중..." -ForegroundColor Cyan
cmake .. -G "Visual Studio 17 2022" -A x64 -T v142 -DCMAKE_TOOLCHAIN_FILE=C:/Users/woose/vcpkg/scripts/buildsystems/vcpkg.cmake

# 빌드 실행
Write-Host "프로젝트 빌드 중..." -ForegroundColor Cyan
cmake --build . --config Release

# 원래 디렉토리로 돌아가기
Set-Location -Path $projectRoot

Write-Host "빌드 완료!" -ForegroundColor Green