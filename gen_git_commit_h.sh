#!/bin/sh
commit=$(git rev-parse HEAD)
git diff-index --quiet HEAD
dirty=$?
printf "#pragma once\n#define GIT_COMMIT \"$commit\"\n#define GIT_DIRTY $dirty\n" > auto/git_info.h.new
if cmp -s auto/git_info.h.new auto/git_info.h 2>&1 > /dev/null
then
  rm -f auto/git_info.h.new
else
  mv -f auto/git_info.h.new auto/git_info.h
fi
