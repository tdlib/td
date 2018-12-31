#!/bin/sh
./src.sh | grep -v CxCli.h | xargs -n 1 clang-format -style=file -i
