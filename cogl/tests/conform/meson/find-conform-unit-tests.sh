#!/bin/sh

inputfile="$1"
outputfile="$2"

echo > "$outputfile"

sed -n -e 's/^ \{1,\}ADD_TEST *( *\([a-zA-Z0-9_]\{1,\}\).*/\1/p' "$1" | while read test; do
  echo $test >> $outputfile
done
