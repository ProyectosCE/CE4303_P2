#!/usr/bin/env bash

set -e

DEVICE_NAME="raspberry_usb"
DEVICE_PATH="/dev/${DEVICE_NAME}"

if ! lsmod | grep -q "^mi_driver"; then
    echo "[INFO] mi_driver no parece estar cargado."
    echo "[INFO] Intentando cargar ./mi_driver.ko..."
    sudo insmod ./mi_driver.ko
fi

MAJOR=$(awk -v dev="$DEVICE_NAME" '$2 == dev {print $1}' /proc/devices)

if [ -z "$MAJOR" ]; then
    echo "[ERROR] No se encontró $DEVICE_NAME en /proc/devices."
    echo "[ERROR] Verifica con: lsmod | grep mi_driver"
    exit 1
fi

echo "[INFO] Major number encontrado: $MAJOR"

sudo rm -f "$DEVICE_PATH"
sudo mknod "$DEVICE_PATH" c "$MAJOR" 0
sudo chown root:$(id -gn) "$DEVICE_PATH"
sudo chmod 660 "$DEVICE_PATH"

echo "[OK] Driver habilitado:"
ls -l "$DEVICE_PATH"

echo
echo "Prueba:"
echo "  echo \"hola waos\" > $DEVICE_PATH"
