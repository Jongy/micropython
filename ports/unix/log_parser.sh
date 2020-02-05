#!/bin/bash
set -e -u -o pipefail

if [ "$#" -ne 2 ]; then
    echo "usage: $0 progmem.log exe"
    exit 1
fi

for l in $(cat "$1" | grep "^pg" | cut -d" " -f2 | sort -u | xargs addr2line -e "$2" | cut -d" " -f1) ; do
    echo -n "$l "
    line_n=$(echo "$l" | cut -d":" -f2)
    f=$(echo "$l" | cut -d":" -f1)
    head -n "$line_n" "$f" | tail -n 1
done
