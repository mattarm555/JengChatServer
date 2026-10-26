param([string]$Toolchain='C:/msys64/ucrt64/bin')
$ErrorActionPreference='Stop'
Set-Location $PSScriptRoot
$env:PATH=$Toolchain+';'+$env:PATH
New-Item -ItemType Directory build -Force | Out-Null
& "$Toolchain/g++.exe" -std=c++17 -O1 -Wall -Wextra tests/recovery.cpp accounts.cpp -o build/recovery_tests.exe -lssl -lcrypto -lsodium -lsqlite3 -lws2_32 -pthread
if($LASTEXITCODE){throw 'Recovery fixture compilation failed'}
./build/recovery_tests.exe
if($LASTEXITCODE){throw 'Recovery checks failed'}
./Test-Roulette.ps1 -Toolchain $Toolchain
