#!/bin/bash

# Configuración de rutas y parámetros (Modifica según tus necesidades)
PYTHON_CLIENT="scripts/client_pbm_to_bin.py"
INPUT_PBM="result.pbm"
OUTPUT_BIN="imagen_payload.bin"
SPEED_MS=4
ROUNDS=100000

echo "=== Ejecutando Cliente Python con C Backend ==="

# 1. Validar que la biblioteca compartida exista
if [ ! -f "scripts/libencoder.so" ]; then
    echo "Error: 'libencoder.so' no existe. Ejecuta primero ./compile.sh"
    exit 1
fi

# 2. Validar que el script de Python exista
if [ ! -f "$PYTHON_CLIENT" ]; then
    echo "Error: No se encontró el script de Python '$PYTHON_CLIENT'."
    exit 1
fi

# 3. Validar que la imagen de entrada exista (opcional pero recomendado)
if [ ! -f "$INPUT_PBM" ]; then
    echo "Advertencia: El archivo de entrada '$INPUT_PBM' no existe en este directorio."
    echo "Asegúrate de colocarlo antes de que falle el script."
fi

# 4. Ejecutar el cliente
python3 "$PYTHON_CLIENT" "$INPUT_PBM" "$OUTPUT_BIN" --speed-ms "$SPEED_MS" --rounds "$ROUNDS"

echo "✓ Proceso finalizado."