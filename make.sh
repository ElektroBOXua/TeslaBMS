BOARD=esp32:esp32:nodemcu-32s
#BOARD=esp8266:esp8266:nodemcu

UPLOADSPEED=:UploadSpeed=921600

AUTOFLASH_PORT=COM5

MONITOR_PORT=COM5
MONITOR_BAUD=115200

if [[ "$BOARD" == "esp8266:esp8266:nodemcu" ]]; then
	UPLOADSPEED=
fi

# Function to compile and upload
compile() {
	while ! arduino-cli compile -b ${BOARD} --warnings "all" -e --libraries "libraries/"; do
		read -p "Press any key to continue "
		exit
	done
}

upload() {
	if [ -n "${AUTOFLASH_PORT+x}" ]; then
		while ! arduino-cli upload -b ${BOARD}${UPLOADSPEED} -p ${AUTOFLASH_PORT}; do
			sleep 1
		done
	fi
}

# Function to monitor
monitor() {
	if [ -n "${MONITOR_PORT+x}" ]; then
		while true; do
			arduino-cli monitor -p ${MONITOR_PORT} --config baudrate=${MONITOR_BAUD};
			sleep 1
		done
	fi
}

# Check if the argument is "monitor"
if [ "$1" == "monitor" ]; then
	monitor
elif [ "$1" == "upload" ]; then
	upload
elif [ "$1" == "setup" ]; then
	./setup.sh
elif [ "$1" == "clean" ]; then
	rm -rf build
	rm -rf libraries
	rm -rf tools
else
	compile
	upload
	monitor
fi

read -p "Press any key to continue "
