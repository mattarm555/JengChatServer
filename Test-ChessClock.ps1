param([string]$Toolchain='C:/msys64/ucrt64/bin')
$ErrorActionPreference='Stop'
Set-Location $PSScriptRoot
$env:PATH=$Toolchain+';'+$env:PATH
& "$Toolchain/g++.exe" -std=c++17 -O1 -DJENGCHAT_TEST_PORT=54991 tests/chess_clock.cpp accounts.cpp -o build/chess_clock_tests.exe -lssl -lcrypto -lsodium -lsqlite3 -lws2_32 -pthread
if($LASTEXITCODE){throw 'Chess clock compilation failed'}
./build/chess_clock_tests.exe
if($LASTEXITCODE){throw 'Chess clock checks failed'}
