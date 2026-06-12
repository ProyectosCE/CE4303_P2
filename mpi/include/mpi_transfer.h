#ifndef MPI_TRANSFER_H
#define MPI_TRANSFER_H

#include <stdint.h>

void mpi_send_bytes(
    const uint8_t *buffer,
    uint64_t size,
    int destination,
    int tag
);

void mpi_recv_bytes(
    uint8_t *buffer,
    uint64_t size,
    int source,
    int tag
);

#endif