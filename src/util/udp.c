#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <stdbool.h>

#include "util/timing.h"

#ifndef __TIMING_H__
    static uint64_t timestamp_ms(void) { return 0; }
#endif

#define UDP_CLIENT_BUFSIZE 212992

void udp_client(uint16_t port, void (*_rx_callback)(uint64_t, uint8_t *, size_t), bool *exit_ptr)
{
    int n;

    /* Open UDP Socket */
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) 
    {
        /* Incoming socket failed to open */
        return;
    }

    /* Add REUSEADDR flag to allow multiple clients on the socket */
    int optval = 1;
    n = setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (const void *)&optval , sizeof(int));
    if(n < 0)
    {
        /* Failed to set SO_REUSEADDR */
        close(sockfd);
        return;
    }

    /* Set the RX buffer length */
    optval = UDP_CLIENT_BUFSIZE;
    n = setsockopt(sockfd, SOL_SOCKET, SO_RCVBUF, &optval, sizeof(optval));
    if(n < 0)
    {
        /* Failed to set buffer size */
        close(sockfd);
        return;
    }

    /* Set up server address */
    struct sockaddr_in serveraddr;
    memset(&serveraddr, 0, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
    serveraddr.sin_port = htons(port);

    /* Bind Socket to server address */
    if (bind(sockfd, (struct sockaddr *) &serveraddr, sizeof(serveraddr)) < 0)
    { 
        /* Socket failed to bind */
        close(sockfd);
        return;
    }

    /* Allocate receive buffer */
    uint8_t *udp_client_buffer = (uint8_t*)malloc(UDP_CLIENT_BUFSIZE);
    if(udp_client_buffer == NULL)
    {
        /* buffer failed to be allocated */
        close(sockfd);
        return;
    }

    bool dummy_noexit = false;
    if(exit_ptr == NULL)
    {
        exit_ptr = &dummy_noexit;
    }

    /* Infinite loop, blocks until incoming packet */
    uint64_t timestamp;
    while(!*exit_ptr)
    {   
        /* Block here until we receive a packet */
        n = recv(sockfd, udp_client_buffer, UDP_CLIENT_BUFSIZE, 0);
        if (n < 0)
        {
            /* Incoming recv failed */
            continue;
        }

        timestamp = timestamp_ms();

        _rx_callback(timestamp, udp_client_buffer, n);
    }
    
    free(udp_client_buffer);
    close(sockfd);
}

void udp_localhost_send(uint16_t port, uint8_t *buffer, size_t buffer_size)
{
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if(sock < 0)
    {
        return;
    }

    struct sockaddr_in server;
    memset(&server, 0, sizeof(struct sockaddr_in));
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    server.sin_port = htons(port);

    if(sendto(sock, buffer, buffer_size, 0, (const struct sockaddr *)&server, sizeof(struct sockaddr_in)) < 0)
    {
        fprintf(stderr, "udp_localhost_send, sendto error: %s\n", strerror(errno));
    }

    close(sock);
}