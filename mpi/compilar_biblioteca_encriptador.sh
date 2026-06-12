#!/bin/bash

# Salir inmediatamente si un comando falla
set -e

C_FILE="encoder.c"
OUTPUT_SO="libencoder.so"

echo "=== Compilando biblioteca nativa en C ==="

if [ ! -f "$C_FILE" ]; then
    echo "Error: No se encontró el archivo fuente '$C_FILE' en el directorio actual."
    exit 1
fi

# Compilación con optimización máxima (-O3) y código de posición independiente (-fPIC)
gcc -O3 -shared -o "$OUTPUT_SO" -fPIC "$C_FILE"

echo "✓ Biblioteca '$OUTPUT_SO' generada correctamente."