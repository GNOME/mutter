#!/bin/bash

inputfile="$1"
outputfile="$2"

nm "$inputfile" | grep '[DR] _\?unit_test_'|sed 's/.\+ [DR] _\?//' > "$outputfile"
