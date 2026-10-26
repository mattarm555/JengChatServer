param([string]$Toolchain='C:/msys64/ucrt64/bin')
$ErrorActionPreference='Stop'
Set-Location $PSScriptRoot
$env:PATH=$Toolchain+';'+$env:PATH
& "$Toolchain/g++.exe" -std=c++17 -O1 tests/hub.cpp accounts.cpp -o build/hub_tests.exe -lssl -lcrypto -lsodium -lsqlite3 -lws2_32 -pthread
if($LASTEXITCODE){throw 'Hub compilation failed'}
./build/hub_tests.exe
if($LASTEXITCODE){throw 'Hub checks failed'}
