#include <stdio.h>
#include <string.h>
#include <mpi.h>

#include "master.h"
#include "worker.h"
#include "pov_driver_lib.h"

/*
Archivo: src/mpi.c
Descripción:
Archivo principal que contiene la función main para el programa MPI. Esta función se encarga de inicializar MPI, determinar el rol de cada proceso (maestro o trabajador), y llamar a la función correspondiente para ejecutar la lógica del maestro o del trabajador. Además, maneja la finalización de MPI y el retorno del código de salida adecuado según el resultado de la ejecución.
*/


/*
main
Función principal del programa MPI. Se encarga de inicializar MPI, determinar el rol de cada proceso (maestro o trabajador), y llamar a la función correspondiente para ejecutar la lógica del maestro o del trabajador. Además, maneja la finalización de MPI y el retorno del código de salida adecuado según el resultado de la ejecución.
Recibe:
- argc: número de argumentos de línea de comandos.
- argv: arreglo de cadenas de caracteres que contiene los argumentos de línea de comandos.
Retorna: 0 si la ejecución fue exitosa, 1 si hubo un error.
*/
int main(int argc, char **argv) {

    int rank;
    int size;

    // Inicializar MPI y obtener el rank y el tamaño del comunicador.
    MPI_Init(&argc, &argv);

    // Obtener el rank del proceso actual y el número total de procesos en el comunicador.
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    int fatal_error = 0;

    int send_to_driver = 0;
    const char *device_path = NULL;

    // El proceso maestro (rank 0) se encarga de leer el archivo de entrada, distribuir el trabajo entre los procesos trabajadores, recopilar los resultados, construir el frame final para el driver, y generar la salida para el driver.
    // Si el rango es 0 es el maestro, de lo contrario es un trabajador.
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

    // Informar a todos los procesos sobre cualquier error fatal detectado en el proceso maestro para que puedan finalizar adecuadamente.
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

    // Si el rango es 0, ejecutar la lógica del maestro; de lo contrario, ejecutar la lógica del trabajador.
    if (rank == 0) {
        result =
            run_master(
                size,
                argv[1],
                device_path,
                send_to_driver
            );
    } else {
        // si es trabajador, ejecutar la función run_worker que se encarga de recibir el segmento cifrado, procesarlo, y enviar los resultados de vuelta al maestro.
        result =
            run_worker(rank);
    }

    MPI_Finalize();

    return result == 0 ? 0 : 1;
}