#!/usr/bin/env bash
set -euo pipefail

CON_NAME="Pi-Client"
IF_MATCH="enx*"
PI_USB_IP="10.12.194.1"

echo "=== Raspberry Pi USB Ethernet client setup ==="

if [ "$EUID" -ne 0 ]; then
    echo "Reejecutando con sudo..."
    exec sudo "$0" "$@"
fi

if ! command -v nmcli >/dev/null 2>&1; then
    echo "ERROR: nmcli no está instalado. Esta configuración requiere NetworkManager."
    exit 1
fi

echo "[1/6] Eliminando conexión previa si existe..."
nmcli con delete "$CON_NAME" 2>/dev/null || true

echo "[2/6] Creando conexión DHCP genérica..."
nmcli con add type ethernet \
    ifname "*" \
    con-name "$CON_NAME" \
    ipv4.method auto \
    ipv4.never-default yes \
    ipv4.ignore-auto-dns yes \
    ipv6.method disabled \
    connection.autoconnect yes \
    connection.autoconnect-priority 50

echo "[3/6] Restringiendo conexión a interfaces ${IF_MATCH}..."
if nmcli con modify "$CON_NAME" match.interface-name "$IF_MATCH"; then
    echo "OK: conexión limitada a interfaces ${IF_MATCH}"
else
    echo "ERROR: tu versión de NetworkManager no soporta match.interface-name."
    echo "La conexión fue creada, pero no se pudo limitar a ${IF_MATCH}."
    echo "Actualiza NetworkManager o configura la interfaz manualmente."
    exit 1
fi

echo "[4/6] Marcando interfaces enx* como managed..."
for dev in /sys/class/net/enx*; do
    [ -e "$dev" ] || continue
    iface="$(basename "$dev")"
    echo "Configurando ${iface} como managed..."
    nmcli dev set "$iface" managed yes || true
done

echo "[5/6] Intentando activar conexión en interfaces enx* detectadas..."
activated=0

for dev in /sys/class/net/enx*; do
    [ -e "$dev" ] || continue
    iface="$(basename "$dev")"

    echo "Intentando activar ${CON_NAME} en ${iface}..."
    if nmcli con up "$CON_NAME" ifname "$iface"; then
        activated=1
        break
    fi
done

if [ "$activated" -eq 0 ]; then
    echo "No se activó todavía porque no hay interfaz enx* lista o no hay DHCP."
    echo "Cuando conectes la Raspberry por USB, NetworkManager debería activar ${CON_NAME} automáticamente."
fi

echo "[6/6] Estado actual:"
echo
nmcli device status
echo

echo "IPs IPv4 en interfaces enx*:"
for dev in /sys/class/net/enx*; do
    [ -e "$dev" ] || continue
    iface="$(basename "$dev")"
    ip -4 addr show dev "$iface" || true
done

echo
echo "Ruta hacia Raspberry USB (${PI_USB_IP}):"
ip route get "$PI_USB_IP" || true

echo
echo "Setup terminado."
echo
echo "Prueba cuando la Raspberry esté conectada:"
echo "  ping -c 4 ${PI_USB_IP}"
echo "  ssh pi@${PI_USB_IP}"
