#!/bin/bash

release="$1"

if [ "$release" = "-h" ] ; then
  echo "Usage: build-release.sh <version>"
  exit 1
fi

if [ "$release" ] ; then
  sed -i~ "s%\(AC_INIT(\[[^[]*\[\)[^]]*%\1${release}%" configure.in
fi

autoreconf --force --install --warnings=all
./configure
make dist
