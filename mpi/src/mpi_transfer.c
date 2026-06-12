#include "mpi_transfer.h"

#include <mpi.h>

void mpi_send_bytes(
    const uint8_t *buffer,
    uint64_t size,
    int destination,
    int tag
) {
    const uint64_t CHUNK_SIZE = 64ull * 1024ull * 1024ull;

    uint64_t offset = 0;

    while (offset < size) {

        uint64_t remaining = size - offset;

        int chunk =
            (int)(remaining > CHUNK_SIZE
                  ? CHUNK_SIZE
                  : remaining);

        MPI_Send(
            (void *)(buffer + offset),
            chunk,
            MPI_BYTE,
            destination,
            tag,
            MPI_COMM_WORLD
        );

        offset += (uint64_t)chunk;
    }
}

void mpi_recv_bytes(
    uint8_t *buffer,
    uint64_t size,
    int source,
    int tag
) {
    const uint64_t CHUNK_SIZE = 64ull * 1024ull * 1024ull;

    uint64_t offset = 0;

    while (offset < size) {

        uint64_t remaining = size - offset;

        int chunk =
            (int)(remaining > CHUNK_SIZE
                  ? CHUNK_SIZE
                  : remaining);

        MPI_Recv(
            buffer + offset,
            chunk,
            MPI_BYTE,
            source,
            tag,
            MPI_COMM_WORLD,
            MPI_STATUS_IGNORE
        );

        offset += (uint64_t)chunk;
    }
}