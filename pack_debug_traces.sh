#!/usr/bin/bash
echo "Zipping traces..."
for i in ./debug_trace_*.txt; do
    zip -9 $i.zip $i &
done
wait
mv *.zip debug_traces
rm trace_*.txt