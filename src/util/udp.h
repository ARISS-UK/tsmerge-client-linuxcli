#ifndef __UDP_H__
#define __UDP_H__

#include <stdint.h>
#include <stdbool.h>

/* UDP Client on given port, blocks waiting for packets, runs callback function with (timestamp_ms, buffer, buffer_size) when a datagram is received */
void udp_client(uint16_t port, void (*_rx_callback)(uint64_t, uint8_t *, size_t), bool *exit_ptr);

/* Send a buffer as a UDP packet to localhost on the given port */
void udp_localhost_send(uint16_t port, uint8_t *buffer, size_t buffer_size);

#endif /* __UDP_H__ */
