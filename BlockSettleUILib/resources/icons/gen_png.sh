#/bin/bash

for SIZE in 32 48 64 128 256
do
  inkscape -z -e blocksettle_signer_$SIZE.png -w $SIZE -h $SIZE blocksettle_signer.svg
done
