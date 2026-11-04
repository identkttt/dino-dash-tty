#!/bin/bash
# Dino Dash TTY Launcher
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"
cd "$DIR"

# Check if binary exists, compile if needed
if [ ! -f "dino" ]; then
    echo "Compiling Dino Dash TTY..."
    clang++ -O3 -std=c++17 -Wall -Wextra -framework ApplicationServices dino.cpp -o dino
fi

# Launch in foreground
exec ./dino
