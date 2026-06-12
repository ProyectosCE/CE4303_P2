#include <stdio.h>
#include <mpi.h>

#include "master.h"
#include "worker.h"

int main(int argc, char **argv) {

    int rank;
    int size;

    MPI_Init(&argc, &argv);

    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    int fatal_error = 0;

    if (rank == 0) {
        if (argc < 2) {
            printf("Uso:\n");
            printf("  mpirun -np 5 ./mpi imagen_payload.bin\n");
            fatal_error = 1;
        }

        if (size < 2) {
            printf("ERROR: se necesita al menos 2 procesos.\n");
            printf("rank 0 = master, rank 1..N = workers\n");
            fatal_error = 1;
        }
    }

    MPI_Bcast(
        &fatal_error,
        1,
        MPI_INT,
        0,
        MPI_COMM_WORLD
    );

    if (fatal_error) {
        MPI_Finalize();
        return 1;
    }

    int result = 0;

    if (rank == 0) {
        result =
            run_master(
                size,
                argv[1]
            );
    } else {
        result =
            run_worker(rank);
    }

    MPI_Finalize();

    return result == 0 ? 0 : 1;
}