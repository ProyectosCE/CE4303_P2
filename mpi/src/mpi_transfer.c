#include "mpi_transfer.h"

#include <mpi.h>


/*
Archivo: src/mpi_transfer.c
Descripción:
Implementación de funciones auxiliares para el envío y recepción de datos entre procesos MPI, especialmente para
manejar grandes segmentos de datos como el payload cifrado. Estas funciones permiten enviar y recibir datos en chunks para evitar problemas de tamaño máximo en las comunicaciones MPI.
*/

/*
mpi_send_bytes
Envía un buffer de bytes a otro proceso MPI, dividiendo el envío en chunks si el tamaño del buffer excede un límite definido (CHUNK_SIZE).
Recibe:
- buffer: puntero al buffer de bytes que se desea enviar.
- size: tamaño del buffer en bytes.
- destination: rank del proceso MPI al que se desea enviar el buffer.
- tag: etiqueta MPI para identificar el mensaje.
Retorna: void. La función se encarga de enviar el buffer completo al proceso destino, manejando internamente la división en chunks si es necesario.
*/

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

        // Enviar un chunk del buffer al proceso destino utilizando MPI_Send. Se envía como MPI_BYTE para evitar problemas de tipo.
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

/*
MPI_Recv_bytes
Recibe un buffer de bytes de otro proceso MPI, dividiendo la recepción en chunks si el tamaño del buffer excede un límite definido (CHUNK_SIZE).
Recibe:
- buffer: puntero al buffer de bytes donde se almacenarán los datos recibidos. Se asume que el buffer ya ha sido asignado con suficiente espacio para recibir los datos.
- size: tamaño del buffer en bytes que se espera recibir.
- source: rank del proceso MPI del que se desea recibir el buffer.
- tag: etiqueta MPI para identificar el mensaje.
Retorna: void. La función se encarga de recibir el buffer completo del proceso fuente, manejando internamente la división en chunks si es necesario.
*/
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

        // Recibir un chunk del buffer del proceso fuente utilizando MPI_Recv. Se recibe como MPI_BYTE para evitar problemas de tipo.
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