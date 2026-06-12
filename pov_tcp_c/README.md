# Cliente/Servidor TCP POV en C

Este módulo implementa dos binarios externos al MPI:

- `pov_tcp_client`: genera PBM P1 de tamaño NxM, cifra con XTEA-CTR, genera el `.bin` con header `PBMB` y lo envía por TCP.
- `pov_tcp_server`: recibe el `.bin`, lo guarda como `imagen_payload.bin`, ejecuta un script externo como `./run_mpi_host.sh` y puede devolver `resultado_mpi_driver.pbm` y `driver_packet.bin`.

El servidor no contiene lógica MPI interna. Solo orquesta el archivo recibido y ejecuta el script configurado.

## Compilar

```bash
make clean
make
```

## Ejecutar servidor

```bash
./pov_tcp_server \
  --port 5050 \
  --mpi-dir /home/alex/Documents/OPERATIVOS/CE4303_P2/mpi \
  --payload-name imagen_payload.bin \
  --remote-script ./run_mpi_host.sh
```

Para una sola petición:

```bash
./pov_tcp_server \
  --port 5050 \
  --mpi-dir /home/alex/Documents/OPERATIVOS/CE4303_P2/mpi \
  --payload-name imagen_payload.bin \
  --remote-script ./run_mpi_host.sh \
  --once
```

## Ejecutar cliente

```bash
./pov_tcp_client \
  --server-ip 192.168.1.139 \
  --port 5050 \
  --width 1024 \
  --height 1024 \
  --pattern waves \
  --rounds 128 \
  --speed-ms 4 \
  --output-bin payload_1024.bin \
  --output-pbm grande_1024.pbm \
  --return-pbm \
  --return-packet \
  --results-dir client_results
```

## Prueba sin ejecutar MPI

Servidor:

```bash
./pov_tcp_server --port 5050 --mpi-dir /tmp --payload-name imagen_payload.bin --once
```

Cliente:

```bash
./pov_tcp_client \
  --server-ip 127.0.0.1 \
  --port 5050 \
  --width 16 \
  --height 16 \
  --pattern checker \
  --rounds 2 \
  --no-run \
  --no-return-pbm
```

## Protocolo

Request TCP:

```txt
64 bytes header POVT/PVTQ + archivo .bin completo
```

Response TCP:

```txt
48 bytes header PVTR + mensaje + PBM opcional + driver_packet opcional
```

El `.bin` enviado por el cliente mantiene el contrato previo:

```txt
Header PBMB de 40 bytes, 10 uint32 big-endian:
magic, version, width, height, row_stride_bytes, speed_ms,
payload_size, cipher_rounds, nonce0, nonce1

Luego payload cifrado XTEA-CTR.
```
