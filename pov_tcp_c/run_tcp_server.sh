# !/bin/bash
./pov_tcp_server \
  --port 5050 \
  --mpi-dir /home/alex/Documents/OPERATIVOS/CE4303_P2/mpi \
  --payload-name imagen_payload.bin \
  --remote-script ./run_mpi_host.sh