#!/usr/bin/bash
echo "Zipping traces..."
echo "This script will zip and delete all .txt files in 5 seconds if you are sure..."
sleep 5
for i in *.txt; do
    zip -9 $i.zip $i &
done
wait
mv *.zip ../sim_traces
rm *.txt