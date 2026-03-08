#!/bin/bash

HOST=localhost
PORT=5020

read_reg_hex() {
	local reg=$1
	local result=$(mbpoll $HOST -p $PORT -1 -0 -r $reg -q -t 4:hex)
	local hex=$(echo "$result" | grep -o '0x[0-9A-Fa-f]*')
	echo "$hex"
}

read_reg_unsigned() {
	local decimal=0
	local hex=$(read_reg_hex $1)
	if [ -n "$hex" ]; then
		decimal=$(printf "%d" "$hex")
	fi
	echo "$decimal"
}

read_reg_signed() {
	local decimal=0
	local signed_decimal=0
	local hex=$(read_reg_hex $1)
	if [ -n "$hex" ]; then
		decimal=$(printf "%d" "$hex")
	fi
	if [ "$decimal" -ge 32768 ]; then
		signed_decimal=$((decimal - 65536))
	else
		signed_decimal="$decimal"
	fi
	echo "$signed_decimal"
}

for (( ; ; )); do

	AC_POWER=$(read_reg_signed 30006)
	BATTERY_SOC=$(read_reg_unsigned 37005)
	FORCE_MODE=$(read_reg_unsigned 42010)
	TEMP_INT=$(read_reg_unsigned 35000 | LC_NUMERIC=C awk '{print $1 / 10}')
	TEMP_MOS1=$(read_reg_unsigned 35001 | LC_NUMERIC=C awk '{print $1 / 10}')
	TEMP_MOS2=$(read_reg_unsigned 35002 | LC_NUMERIC=C awk '{print $1 / 10}')
	TEMP_CELL_MAX=$(read_reg_unsigned 35010 | LC_NUMERIC=C awk '{print $1 / 10}')
	TEMP_CELL_MIN=$(read_reg_unsigned 35011 | LC_NUMERIC=C awk '{print $1 / 10}')
	VOLT_CELL_MAX=$(read_reg_unsigned 37007 | LC_NUMERIC=C awk '{print $1 / 1000}')
	VOLT_CELL_MIN=$(read_reg_unsigned 37008 | LC_NUMERIC=C awk '{print $1 / 1000}')

	case "$FORCE_MODE" in
	0)
		echo "Force none                   "
		;;
	1)
		CHARGE_POWER=$(read_reg_unsigned 42020)
		echo "Force charge:         ${CHARGE_POWER}W   "
		;;
	2)
		DISCHARGE_POWER=$(read_reg_unsigned 42021)
		echo "Force discharge:      ${DISCHARGE_POWER}W   "
		;;
	*)
		echo "Invalid force mode            "
		;;
	esac

	echo "AC power:             ${AC_POWER}W   "
	echo "Battery state:        ${BATTERY_SOC}%  "
	echo "Temperature internal: ${TEMP_INT}°C   "
	echo "Temperature MOS1:     ${TEMP_MOS1}°C   "
	echo "Temperature MOS2:     ${TEMP_MOS2}°C   "
	echo "Temperature cell min: ${TEMP_CELL_MIN}°C   "
	echo "Temperature cell max: ${TEMP_CELL_MAX}°C   "
	echo "Voltage cell min:     ${VOLT_CELL_MIN}V   "
	echo "Voltage cell max:     ${VOLT_CELL_MAX}V   "

	sleep 4

	tput cuu 10
done
