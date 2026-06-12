#include <stdio.h>
#include <string.h>
#include <mpi.h>

#include "master.h"
#include "worker.h"
#include "pov_driver_lib.h"

int main(int argc, char **argv) {

    int rank;
    int size;

    MPI_Init(&argc, &argv);

    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    int fatal_error = 0;

    int send_to_driver = 0;
    const char *device_path = NULL;

    if (rank == 0) {
        if (argc < 2) {
            printf("Uso:\n");
            printf("  mpirun -np 5 ./mpi imagen_payload.bin\n");
            printf("  mpirun -np 5 ./mpi imagen_payload.bin --send\n");
            printf("  mpirun -np 5 ./mpi imagen_payload.bin --send /dev/raspberry_usb\n");
            printf("\n");
            printf("Sin --send:\n");
            printf("  solo genera driver_packet.bin\n");
            printf("\n");
            printf("Con --send:\n");
            printf("  genera driver_packet.bin y lo envía al driver\n");
            fatal_error = 1;
        }

        if (size < 2) {
            printf("ERROR: se necesita al menos 2 procesos.\n");
            printf("rank 0 = master, rank 1..N = workers\n");
            fatal_error = 1;
        }

        if (argc >= 3) {
            if (strcmp(argv[2], "--send") == 0) {
                send_to_driver = 1;

                if (argc >= 4) {
                    device_path = argv[3];
                } else {
                    device_path = POV_DRIVER_DEFAULT_DEVICE;
                }
            } else {
                printf("ERROR: argumento desconocido: %s\n", argv[2]);
                printf("Use --send para enviar al driver.\n");
                fatal_error = 1;
            }
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
                argv[1],
                device_path,
                send_to_driver
            );
    } else {
        result =
            run_worker(rank);
    }

    MPI_Finalize();

    return result == 0 ? 0 : 1;
}