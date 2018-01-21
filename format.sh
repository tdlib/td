#!/bin/sh
./src.sh | grep -v CxCli.h | xargs -n 1 clang-format -verbose -style=file -i
