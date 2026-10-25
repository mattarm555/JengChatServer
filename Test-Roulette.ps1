param([string]$Toolchain = 'C:/msys64/ucrt64/bin')
$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot
$env:PATH = $Toolchain + ';' + $env:PATH
New-Item -ItemType Directory -Path build -Force | Out-Null
# Link the production account implementation, but never start the listener or account store.
& "$Toolchain/g++.exe" -std=c++17 -O2 tests/roulette_broadcast.cpp accounts.cpp -o build/roulette_broadcast.exe -lssl -lcrypto -lsodium -lsqlite3 -lws2_32 -pthread
if ($LASTEXITCODE -ne 0) { throw 'Roulette broadcast fixture compilation failed' }
& ./build/roulette_broadcast.exe
if ($LASTEXITCODE -ne 0) { throw 'Roulette broadcast checks failed' }
