#!/bin/bash
# usage: ./flash.sh low|high
#
# The ESP32 passthrough (REMs-8x_serial_passthru) starts its serprog flasher on
#   CTL-P (DLE, 0x10) for the Low switch, CTL-X (0x18) for the High switch.
# The High switch's flash has longer traces and needs a slower SPI clock.
# Each switch gets the image built for it: output/rtlplayground.bin only points
# at whichever board was built last, so it is not used here.
#
#maybee use a CRL-P DLE (data link escape)
#to enter flash mode
#flashmode needs a timeout
#
#screen /dev/ttyUSB0 115200
set -e

case "$1" in
low)	KEY='\x10'; MACHINE=REMS_8X_LOW_V0_0_0; SPISPEED=30M ;;
high)	KEY='\x18'; MACHINE=REMS_8X_HIGH_V0_0_0; SPISPEED=20M ;;	# longer flash traces
*)	echo "usage: $0 low|high"; exit 1 ;;
esac

cd "$(dirname "$0")"
IMG=$(ls -t output/$MACHINE/rtlplayground-*-$MACHINE.bin 2>/dev/null | head -1)
if [ -z "$IMG" ]; then
	echo "No image for $MACHINE, run: make MACHINE=$MACHINE"
	exit 1
fi
echo "Flashing $1 switch with $IMG"

stty -F /dev/ttyUSB0 115200 ; sleep 1 ; echo -ne "$KEY" > /dev/ttyUSB0
cp "$IMG" output/paded.bin
truncate -s 2097152 output/paded.bin
flashprog -p serprog:dev=/dev/ttyUSB0:3000000,spispeed=$SPISPEED --progress --layout rom.layout --image normal -N -w output/paded.bin
screen /dev/ttyUSB0 115200
