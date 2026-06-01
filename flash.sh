#!/bin/bash
#maybee use a CRL-P DLE (data link escape)
#to enter flash mode
#flashmode needs a timeout
#
#screen /dev/ttyUSB0 115200
stty -F /dev/ttyUSB0 115200 ; sleep 1 ; echo -ne '\x10' > /dev/ttyUSB0
cd output
cp rtlplayground.bin paded.bin
truncate -s 2097152 paded.bin
flashprog -p serprog:dev=/dev/ttyUSB0:3000000,spispeed=30M --progress --layout ../rom.layout --image normal -N -w  paded.bin
cd ..
screen /dev/ttyUSB0 115200
