#!/bin/ksh
# IDEmu - agente de dados do guest: recebe do host (UART1 = /dev/ser5) o estado do CAN e do GPS virtuais.
#   C <ID-hex> <dados-hex>   ->  /var/idemu/can/<ID>      (último quadro de cada ID)
#   G <sentença NMEA>        ->  /var/idemu/gps/<TIPO>    (ex.: GPGGA, GPRMC)
mkdir -p /var/idemu/can /var/idemu/gps
devc-sernuke -N -u5 -E -F -b921600 -c48000000/13 0x4806A000^2,104 > /dev/null 2>&1 &
waitfor /dev/ser5 10
while read -r t a b; do
  case "$t" in
    C) echo "$b" > /var/idemu/can/$a ;;
    G) s=${a#\$}; echo "$a" > /var/idemu/gps/${s%%,*} ;;
  esac
done < /dev/ser5
