#!/bin/bash
# Deploy extension packages to bin/clang/<config>/extensions/.
# Usage: deploy_extensions.sh [release|debug]
# When called without arguments, deploys to both release and debug.
cd "$(dirname "${BASH_SOURCE[0]}")"

EXTENSIONS_SRC="../../../extensions"

deploy_extensions_for_config() {
    local config="$1"
    local bin_dir="../../../bin/clang/${config}"
    if [ -d "$bin_dir" ]; then
        if [ -d "$EXTENSIONS_SRC" ]; then
            rm -rf "$bin_dir/extensions"
            mkdir -p "$bin_dir/extensions"
            cp -rf "$EXTENSIONS_SRC/." "$bin_dir/extensions/"
            echo "Deployed extensions to bin/clang/${config}/extensions/"
        else
            echo "Warning: extensions source directory not found"
        fi
    else
        echo "Skipping ${config} (bin/clang/${config}/ does not exist)"
    fi
}

if [ -n "$1" ]; then
    echo "**************** deploy_extensions ($1) ****************"
    deploy_extensions_for_config "$1"
else
    echo "**************** deploy_extensions ****************"
    deploy_extensions_for_config release
    deploy_extensions_for_config debug
fi
