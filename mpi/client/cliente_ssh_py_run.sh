#!/bin/bash

python3 client/pov_client_ssh.py \
  --generate-pbm images/grande_1024.pbm \
  --width 1024 \
  --height 1024 \
  --pattern waves \
  --output-bin images/payload_1024.bin \
  --speed-ms 4 \
  --rounds 75000 \
  --ssh \
  --server alex@192.168.101.43 \
  --remote-dir /home/alex/Documents/OPERATIVOS/CE4303_P2/mpi \
  --np 6 \
  --send