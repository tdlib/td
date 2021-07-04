#!/bin/sh
./src.sh | grep -v CxCli.h | grep -iv dotnet | grep -v /tl-parser/ | xargs -n 1 clang-format -verbose -style=file -i
