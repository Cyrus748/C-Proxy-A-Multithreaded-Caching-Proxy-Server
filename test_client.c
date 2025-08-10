// test_client.c
// An improved client that sends a more complete HTTP/1.0 request.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netdb.h>

#define BUFFER_SIZE 8192

// A simple helper function to extract the hostname from a full URL
void get_hostname_from_url(const char *url, char *hostname, int len) {
    char temp_url[1024];
    strncpy(temp_url, url, sizeof(temp_url) - 1);
    temp_url[sizeof(temp_url) - 1] = '\0';

    char *host_start = strstr(temp_url, "://");
    if (host_start) {
        host_start += 3; // Move past "://"
    } else {
        host_start = temp_url; // No scheme, assume it's the host
    }

    char *host_end = strchr(host_start, '/');
    if (host_end) {
        strncpy(hostname, host_start, host_end - host_start);
        hostname[host_end - host_start] = '\0';
    } else {
        strncpy(hostname, host_start, len - 1);
        hostname[len - 1] = '\0';
    }
}

int main(int argc, char *argv[]) {
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <proxy_host> <proxy_port> <URL_to_fetch>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    char *proxy_host = argv[1];
    int proxy_port = atoi(argv[2]);
    char *url = argv[3];

    // --- Connect to the proxy server ---
    int sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    struct hostent *server = gethostbyname(proxy_host);
    if (server == NULL) {
        fprintf(stderr, "ERROR, no such host: %s\n", proxy_host);
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    memcpy(&server_addr.sin_addr.s_addr, server->h_addr, server->h_length);
    server_addr.sin_port = htons(proxy_port);

    if (connect(sock_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("connect");
        exit(EXIT_FAILURE);
    }
    printf("--- Connected to proxy at %s:%d ---\n", proxy_host, proxy_port);

    // --- NEW: Extract hostname to create the Host header ---
    char hostname[256];
    get_hostname_from_url(url, hostname, sizeof(hostname));

    // --- NEW: Construct a more complete HTTP/1.0 request ---
    char request[BUFFER_SIZE];
    snprintf(request, BUFFER_SIZE,
             "GET %s HTTP/1.0\r\n"
             "Host: %s\r\n"
             "Connection: close\r\n\r\n",
             url, hostname);

    printf("--- Sending Request ---\n%s", request);

    if (send(sock_fd, request, strlen(request), 0) < 0) {
        perror("send");
        exit(EXIT_FAILURE);
    }

    // --- Receive and print the response ---
    char response_buffer[BUFFER_SIZE];
    ssize_t bytes_received;

    printf("--- Receiving Response ---\n");
    while ((bytes_received = recv(sock_fd, response_buffer, BUFFER_SIZE - 1, 0)) > 0) {
        response_buffer[bytes_received] = '\0';
        printf("%s", response_buffer);
    }

    if (bytes_received < 0) {
        perror("recv");
    }

    printf("\n--- Connection closed ---\n");
    close(sock_fd);

    return 0;
}
