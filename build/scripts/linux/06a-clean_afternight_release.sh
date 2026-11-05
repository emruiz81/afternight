#!/bin/bash
cd "$(dirname "${BASH_SOURCE[0]}")"

echo ****************06a-clean_afternight_release****************
pushd ../../make-linux
make config=release_x64 clean
popd

echo Done!