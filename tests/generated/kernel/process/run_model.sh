#!/usr/bin/env sh
set -eu
dir="$(dirname "$0")"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
cc -O2 -Wall -Wextra -Werror -o "$tmp/process_tree_model" "$dir/process_tree_model.c"
"$tmp/process_tree_model" "${1:-42}" "${2:-500}"
