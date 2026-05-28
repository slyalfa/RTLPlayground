#!/bin/bash
screen /dev/ttyUSB0 115200
cd output
cp rtlplayground.bin paded.bin
truncate -s 2097152 paded.bin
flashprog -p serprog:dev=/dev/ttyUSB0:921600 --layout rom.layout --image normal -N -w  padied.bin
flashprog -p serprog:dev=/dev/ttyUSB0:3000000,spispeed=30M --layout rom.layout --image normal -N -w  paded.bin
#flashprog -p serprog:dev=/dev/ttyUSB0:3000000,spispeed=30M --layout rom.layout --image normal -N -w  pad.bin
cd ..
screen /dev/ttyUSB0 115200
