#!/bin/bash
set -e

rm -f ../output/*.wav

./Mid2Wav --device SC-8850 -i ../input_midis/ -o ../output/

smbclient //192.168.0.104/output -A ~/.smbcredentials-xeon \
  -c "lcd ../output; prompt OFF; mput *.wav"

