#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
c++ -std=c++17 -O2 -Wall -Wextra server.cpp accounts.cpp -o jengchat_server \
    -lssl -lcrypto -lsodium -lsqlite3 -pthread
