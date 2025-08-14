/* udppush - Relay UDP TS and MER data to tsmerger                        */
/*=======================================================================*/
/* Copyright (C)2025 Phil Crump <phil@philcrump.co.uk>                   */
/* Copyright (C)2016 Philip Heron <phil@sanslogic.co.uk>                 */
/*                                                                       */
/* This program is free software: you can redistribute it and/or modify  */
/* it under the terms of the GNU General Public License as published by  */
/* the Free Software Foundation, either version 3 of the License, or     */
/* (at your option) any later version.                                   */

#include <time.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <getopt.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <signal.h>
#include <pthread.h>

#include "ts/ts.h"
#include "util/udp.h"
#include "util/timing.h"

#define MIN(x,y) ((x) < (y) ? (x) : (y))
#define MAX(x,y) ((x) > (y) ? (x) : (y))

#define MPEGTS_UDP_PORT         (9002)
#define LONGMYNDSTATS_UDP_PORT  (9003)

#define MX_MAGICBYTES_VALUE     (0x55A2)
typedef struct {
    uint16_t magic_bytes;

    uint32_t counter;

    uint8_t snr;

    char callsign[10];
    char key[10];

} __attribute__((packed)) mx_header_t;
   
#define MX_PACKET_LEN   (sizeof(mx_header_t) + TS_PACKET_SIZE)

#define MXHB_MAGICBYTES_VALUE   (0x55A3)
typedef struct {
    uint16_t magic_bytes;

    char callsign[10];
    char key[10];

    int16_t packet_length;

} __attribute__((packed)) mxhb_header_t;

#define MXHBRESP_MAGICBYTES_VALUE   (0x55A4)
typedef struct {
    uint16_t magic_bytes;

    uint8_t auth_response;

    int16_t original_packet_length;

    int32_t ts_total;
    int32_t ts_loss;

} __attribute__((packed)) mxhbresp_header_t;

typedef struct {

    /* Output UDP Socket */
    int output_socket;

    char callsign[11]; // 1 guard byte to allow string functions
    char key[11]; // 1 guard byte to allow string functions
    
    /* Output packet counter */
    uint32_t output_counter;

    /* S/N */
    uint8_t rx_sn;
    uint64_t rx_sn_updated_ms;
    
    /* TS Packet Data buffer */
    uint8_t data[4][MX_PACKET_LEN];

    uint32_t data_cursor;
    uint32_t data_batch;

    bool app_exit;
    
} _push_t;

static _push_t tspush = {
    .callsign = {0},
    .key = {0},
    .rx_sn_updated_ms = 0,
    .data_cursor = 0,
    .data_batch = 2,
    .app_exit = false
};

static pthread_t mxhb_thread_obj;
static pthread_t statsudp_thread_obj;
static pthread_t ts_udp_thread_obj;

static int _open_socket(char *host, char *port, int ai_family)
{
    int r;
    int sock;
    struct addrinfo hints;
    struct addrinfo *re, *rp;
    char s[INET6_ADDRSTRLEN];
    
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = ai_family;
    hints.ai_socktype = SOCK_DGRAM;
    
    r = getaddrinfo(host, port, &hints, &re);
    if(r != 0)
    {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(r));
        return(-1);
    }
    
    /* Try IPv6 first */
    for(sock = -1, rp = re; sock == -1 && rp != NULL; rp = rp->ai_next)
    {
        if(rp->ai_addr->sa_family != AF_INET6) continue;
        
        inet_ntop(AF_INET6, &(((struct sockaddr_in6 *) rp->ai_addr)->sin6_addr), s, INET6_ADDRSTRLEN);
        printf("Sending to [%s]:%d\n", s, ntohs((((struct sockaddr_in6 *) rp->ai_addr)->sin6_port)));
        
        sock = socket(rp->ai_family, rp->ai_socktype | SOCK_NONBLOCK, rp->ai_protocol);
        if(sock == -1)
        {
            perror("socket");
            continue;
        }

        
        if(connect(sock, rp->ai_addr, rp->ai_addrlen) == -1)
        {
            perror("connect");
            close(sock);
            sock = -1;
        }
    }
    
    /* Try IPv4 next */
    for(rp = re; sock == -1 && rp != NULL; rp = rp->ai_next)
    {
        if(rp->ai_addr->sa_family != AF_INET) continue;
        
        inet_ntop(AF_INET, &(((struct sockaddr_in *) rp->ai_addr)->sin_addr), s, INET6_ADDRSTRLEN);
        printf("Sending to %s:%d\n", s, ntohs((((struct sockaddr_in *) rp->ai_addr)->sin_port)));
        
        sock = socket(rp->ai_family, rp->ai_socktype | SOCK_NONBLOCK, rp->ai_protocol);
        if(sock == -1)
        {
            perror("socket");
            continue;
        }
        
        if(connect(sock, rp->ai_addr, rp->ai_addrlen) == -1)
        {
            perror("connect");
            close(sock);
            sock = -1;
        }
    }
    
    freeaddrinfo(re);
    
    return(sock);
}

void _print_usage(void)
{
    printf(
        "\n"
        "Usage: tspush [options] INPUT\n"
        "\n"
        "  -h, --host <name>      Set the hostname to send data to. Default: live.ariss.org\n"
        "  -p, --port <number>    Set the port number to send data to. Default: 5678\n"
        "  -4, --ipv4             Force IPv4 only.\n"
        "  -6, --ipv6             Force IPv6 only.\n"
        "  -c, --callsign <id>    Set the station callsign, up to 10 characters.\n"
        "  -k, --key <key>        Set the station preshared key, up to 10 characters.\n"
        "\n"
    );
}

static void *mxhb_thread(void *arg)
{
  (void)arg;

  char *mxhb_buffer;
  int mxhb_buffer_len;

  int initial_maxlen_counter = 1;

  while(!tspush.app_exit)
  {
    if(initial_maxlen_counter <= 10)
    {
        mxhb_buffer = malloc(initial_maxlen_counter * MX_PACKET_LEN);
        mxhb_buffer_len = initial_maxlen_counter * MX_PACKET_LEN;
    }
    else
    {
        mxhb_buffer = malloc(1 * MX_PACKET_LEN);
        mxhb_buffer_len = 1 * MX_PACKET_LEN;
    }
    memset(mxhb_buffer, 0, mxhb_buffer_len);

    mxhb_header_t *mxhb_ptr = (mxhb_header_t *)mxhb_buffer;

    mxhb_ptr->magic_bytes = MXHB_MAGICBYTES_VALUE;

    /* Station ID (10 bytes, UTF-8) */
    memcpy(mxhb_ptr->callsign, tspush.callsign, 10);

    /* Station Key (10 bytes, UTF-8) */
    memcpy(mxhb_ptr->key, tspush.key, 10);

    mxhb_ptr->packet_length = mxhb_buffer_len;

    send(tspush.output_socket, mxhb_buffer, mxhb_buffer_len, 0);
    free(mxhb_buffer);

    if(initial_maxlen_counter <= 10)
    {
        sleep_ms(1000);
        initial_maxlen_counter++;
    }
    else
    {
        sleep(10);
    }
  }

  pthread_exit(NULL);
}

static void stats_udp_callback(uint64_t current_timestamp, uint8_t *buffer, size_t buffer_size)
{
  int32_t param_id, param_value;

  char *stringBuffer = malloc(buffer_size + 1);

  memcpy(stringBuffer, buffer, buffer_size);
  stringBuffer[buffer_size] = '\0';

  if(sscanf(stringBuffer, "$%d,%d" , &param_id, &param_value) == 2)
  {
    if(param_id == 12)
    {
      /* MER */
      tspush.rx_sn = (uint8_t)param_value; // value is already *10
      tspush.rx_sn_updated_ms = current_timestamp;
    }
  }

  free(stringBuffer);
}


static void *statsudp_thread(void *arg)
{
  (void)arg;

  udp_client(LONGMYNDSTATS_UDP_PORT, stats_udp_callback, &(tspush.app_exit));

  pthread_exit(NULL);
}

#define TS_UDP_BUFFER_SIZE  (16384)
static uint8_t ts_udp_buffer[TS_UDP_BUFFER_SIZE];
static size_t ts_udp_buffer_cursor = 0;
static void ts_udp_callback(uint64_t current_timestamp, uint8_t *buffer, size_t buffer_size)
{
    (void)current_timestamp;

    ts_header_t ts;

    uint64_t current_timestamp_ms;

    if(buffer_size <= 0)
    {
        fprintf(stderr, "ts_udp_callback: Invalid UDP buffer size: %zu\n", buffer_size);
        return;
    }

    if(ts_udp_buffer_cursor + buffer_size >= TS_UDP_BUFFER_SIZE)
    {
        fprintf(stderr, "ts_udp_callback: Warning, Input buffer size too large to fit in remaining buffer (incoming buffer size: %zu, overflow: %zu)\n", buffer_size, (ts_udp_buffer_cursor + buffer_size) - TS_UDP_BUFFER_SIZE);
        buffer_size = TS_UDP_BUFFER_SIZE - ts_udp_buffer_cursor;
    }

    memcpy(&(ts_udp_buffer[ts_udp_buffer_cursor]), buffer, buffer_size);
    ts_udp_buffer_cursor += buffer_size;

    if(ts_udp_buffer[0] != TS_HEADER_SYNC)
    {
        /* Re-align input to the TS sync byte */
        uint8_t *p = memchr(ts_udp_buffer, TS_HEADER_SYNC, ts_udp_buffer_cursor);
        if(p == NULL)
        {
            // No TS sync found, discard whole buffer
            ts_udp_buffer_cursor = 0;
            return;
        }

        int c = p - &(ts_udp_buffer[0]);

        memmove(&(ts_udp_buffer[0]), p, (ts_udp_buffer_cursor - c));
        ts_udp_buffer_cursor -= c;

        if(ts_udp_buffer_cursor < TS_PACKET_SIZE)
        {
            return;
        }
    }

    while(ts_udp_buffer_cursor >= TS_PACKET_SIZE)
    {
        /* Initialise the header */
        memset(tspush.data[tspush.data_cursor], 0, MX_PACKET_LEN);

        /* Copy in next TS packet from input buffer */
        memcpy(&tspush.data[tspush.data_cursor][sizeof(mx_header_t)], ts_udp_buffer, TS_PACKET_SIZE);

        /* Shift input buffer down */
        memmove(&(ts_udp_buffer[0]), &(ts_udp_buffer[TS_PACKET_SIZE]), ts_udp_buffer_cursor - TS_PACKET_SIZE);
        ts_udp_buffer_cursor -= TS_PACKET_SIZE;

        if(ts_parse_header(&ts, &tspush.data[tspush.data_cursor][sizeof(mx_header_t)]) != TS_OK)
        {
            /* Don't transmit packets with invalid headers */
            printf("TS_INVALID\n");
            continue;
        }

        current_timestamp_ms = timestamp_ms();

        /* We don't transmit NULL/padding packets */
        if(ts.pid == TS_NULL_PID)
        {
            continue;
        }

        mx_header_t *mx_header_ptr = (mx_header_t *)&(tspush.data[tspush.data_cursor]);

        /* Packet ID / type */
        mx_header_ptr->magic_bytes = MX_MAGICBYTES_VALUE;

        /* Counter (4 bytes little-endian) */
        mx_header_ptr->counter = tspush.output_counter++;

        /* S/N from Longmynd MER */
        if(tspush.rx_sn_updated_ms > (current_timestamp_ms - 1000))
        {
            mx_header_ptr->snr = tspush.rx_sn;
        }
        else
        {
            mx_header_ptr->snr = 0;
        }

        /* Station ID (10 bytes, UTF-8) */
        memcpy(mx_header_ptr->callsign, tspush.callsign, 10);

        /* Station Key (10 bytes, UTF-8) */
        memcpy(mx_header_ptr->key, tspush.key, 10);

        tspush.data_cursor++;

        if(tspush.data_cursor >= tspush.data_batch)
        {
            /* Send the full MX packet */
            send(tspush.output_socket, tspush.data, MX_PACKET_LEN * 4, 0);
            tspush.data_cursor = 0;
        }
    }
}

static void *ts_udp_thread(void *arg)
{
  (void)arg;

  udp_client(MPEGTS_UDP_PORT, ts_udp_callback, &(tspush.app_exit));

  pthread_exit(NULL);
}

int main(int argc, char *argv[])
{
    int opt;
    int c;
    char *host = "live.ariss.org";
    char *port = "5678";
    int ai_family = AF_UNSPEC;

    printf("========== tsmerge client ===========\n");
    printf(" * Build Version: %s\n", BUILD_VERSION);
    printf(" * Build Date:    %s\n", BUILD_DATE);
    printf("=====================================\n");
    
    tspush.output_counter = 0;
    
    static const struct option long_options[] = {
        { "host",        required_argument, 0, 'h' },
        { "port",        required_argument, 0, 'p' },
        { "ipv6",        no_argument,       0, '6' },
        { "ipv4",        no_argument,       0, '4' },
        { "callsign",    required_argument, 0, 'c' },
        { "key",         required_argument, 0, 'k' },
        { 0,             0,                 0,  0  }
    };
    
    opterr = 0;
    while((c = getopt_long(argc, argv, "h:p:64c:k:", long_options, &opt)) != -1)
    {
        switch(c)
        {
        case 'h': /* --host <name> */
            host = optarg;
            break;
        
        case 'p': /* --port <number> */
            port = optarg;
            break;
        
        case '6': /* --ipv6 */
            ai_family = AF_INET6;
            break;
        
        case '4': /* --ipv4 */
            ai_family = AF_INET;
            break;
        
        case 'c': /* --callsign <id> */
            memcpy(tspush.callsign, optarg, MIN(10, strlen(optarg)));
            tspush.callsign[10] = '\0';
            break;
        
        case 'k': /* --key <key> */
            memcpy(tspush.key, optarg, MIN(10, strlen(optarg)));
            tspush.key[10] = '\0';
            break;
        
        case '?':
            _print_usage();
            return(0);  
        }
    }
    
    if(strlen(tspush.callsign) == 0)
    {
        printf("Error: A callsign (-c) is required\n");
        _print_usage();
        return(-1);
    }

    if(strlen(tspush.key) == 0)
    {
        printf("Error: A key (-k) is required\n");
        _print_usage();
        return(-1);
    }

    printf(" * MPEG TS UDP Input Port:          %d\n", MPEGTS_UDP_PORT);
    printf(" * Longmynd Status UDP Input Port:  %d\n", LONGMYNDSTATS_UDP_PORT);

    printf(" * Server Hostname:                 %s\n", host);
    printf(" * Server Port:                     %s\n", port);
    printf(" * Server Protocol:                 %s\n", ai_family == AF_INET ? "IPv4" : "IPv6");

    printf(" * Server Callsign:                 %s\n", tspush.callsign);
    printf(" * Server Key:                      %s\n", tspush.key);

    printf("=====================================\n");

    /* Open the outgoing socket */
    tspush.output_socket = _open_socket(host, port, ai_family);
    if(tspush.output_socket == -1)
    {
        printf("Failed to resolve %s\n", host);
        return(-1);
    }

    if(pthread_create(&mxhb_thread_obj, NULL, mxhb_thread, NULL))
    {
        fprintf(stderr, "Error creating %s pthread\n", "MX Heartbeat");
        return 1;
    }
    pthread_setname_np(mxhb_thread_obj, "MX Heartbeat");

    if(pthread_create(&statsudp_thread_obj, NULL, statsudp_thread, NULL))
    {
        fprintf(stderr, "Error creating %s pthread\n", "Stats UDP");
        return 1;
    }
    pthread_setname_np(statsudp_thread_obj, "Stats UDP");

    if(pthread_create(&ts_udp_thread_obj, NULL, ts_udp_thread, NULL))
    {
        fprintf(stderr, "Error creating %s pthread\n", "TS UDP");
        return 1;
    }
    pthread_setname_np(ts_udp_thread_obj, "TS UDP");

    int n = 0;
    char udp_response_buffer[1024];
    while(!tspush.app_exit)
    {
        n = recv(tspush.output_socket, udp_response_buffer, 1024, 0);
        if(n >= (int)sizeof(mxhbresp_header_t))
        {
            mxhbresp_header_t *mxhbresp_ptr = (mxhbresp_header_t *)udp_response_buffer;

            if(mxhbresp_ptr->magic_bytes != MXHBRESP_MAGICBYTES_VALUE)
            {
                continue;
            }

            if(mxhbresp_ptr->auth_response == 0x00)
            {
                // Authentication failed
                fprintf(stderr, "Error: Server reported authentication failed. Please check callsign and key.\n");
                tspush.app_exit = true;
            }
            else
            {
                tspush.data_batch = MAX(tspush.data_batch, mxhbresp_ptr->original_packet_length / MX_PACKET_LEN);
                printf("Heartbeat response: Tested size: %dB (max batch now: %u), TS Uploaded Total: %d, TS Uploaded Loss: %d\n",
                    mxhbresp_ptr->original_packet_length,
                    tspush.data_batch,
                    mxhbresp_ptr->ts_total,
                    mxhbresp_ptr->ts_loss
                );
            }
        }
        sleep_ms(50);
    };

    tspush.app_exit = true;

    /* then close sockets */
    close(tspush.output_socket);

    return(0);
}

