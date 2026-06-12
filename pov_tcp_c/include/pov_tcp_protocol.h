#ifndef POV_TCP_PROTOCOL_H
#define POV_TCP_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#define POV_TCP_REQUEST_MAGIC   0x50565451u  /* "PVTQ" */
#define POV_TCP_RESPONSE_MAGIC  0x50565452u  /* "PVTR" */
#define POV_TCP_VERSION         1u

#define POV_TCP_REQUEST_HEADER_SIZE   64u
#define POV_TCP_RESPONSE_HEADER_SIZE  48u

#define POV_TCP_FLAG_RUN_MPI        0x00000001u
#define POV_TCP_FLAG_RETURN_PBM     0x00000002u
#define POV_TCP_FLAG_RETURN_PACKET  0x00000004u

#define POV_BIN_MAGIC    0x50424D42u  /* "PBMB" */
#define POV_BIN_VERSION  1u

#define POV_PATTERN_CIRCLE    1u
#define POV_PATTERN_CHECKER   2u
#define POV_PATTERN_DIAGONAL  3u
#define POV_PATTERN_WAVES     4u

#define POV_DEFAULT_ROUNDS    64u
#define POV_DEFAULT_SPEED_MS  4u
#define POV_DEFAULT_NONCE0    0x13579BDFu
#define POV_DEFAULT_NONCE1    0x2468ACE0u

#define POV_XTEA_KEY0 0xA56BABCDu
#define POV_XTEA_KEY1 0x0000FACEu
#define POV_XTEA_KEY2 0xDEADBEEFu
#define POV_XTEA_KEY3 0x01234567u

#define POV_RESULT_PBM_NAME     "resultado_mpi_driver.pbm"
#define POV_RESULT_PACKET_NAME  "driver_packet.bin"

uint32_t pov_read_u32_be(const uint8_t *buffer);
uint64_t pov_read_u64_be(const uint8_t *buffer);

void pov_write_u32_be(uint8_t *buffer, uint32_t value);
void pov_write_u64_be(uint8_t *buffer, uint64_t value);

int send_all(int fd, const void *buffer, size_t length);
int recv_all(int fd, void *buffer, size_t length);

#endif
