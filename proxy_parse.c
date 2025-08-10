/*
 * proxy_parse.c -- A robust and corrected HTTP Request Parsing Library.
 */
#include "proxy_parse.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define DEBUG 0 // Set to 1 to see debug messages

void debug(const char * format, ...) {
     va_list args;
     if (DEBUG) {
         va_start(args, format);
         vfprintf(stderr, format, args);
         va_end(args);
     }
}

struct ParsedRequest* ParsedRequest_create() {
    struct ParsedRequest *pr = (struct ParsedRequest *)malloc(sizeof(struct ParsedRequest));
    if (pr != NULL) {
        memset(pr, 0, sizeof(struct ParsedRequest));
    }
    return pr;
}

void ParsedRequest_destroy(struct ParsedRequest *pr) {
    if (pr == NULL) return;
    free(pr->method);
    free(pr->protocol);
    free(pr->host);
    free(pr->port);
    free(pr->path);
    free(pr->version);
    free(pr->buf);
    free(pr);
}

int ParsedRequest_parse(struct ParsedRequest *parse, const char *buf, int buflen) {
    if (parse == NULL || buf == NULL || buflen < 4) return -1;

    char *temp_buf = (char *)malloc(buflen + 1);
    if (temp_buf == NULL) return -1;
    memcpy(temp_buf, buf, buflen);
    temp_buf[buflen] = '\0';

    char *request_line = strtok(temp_buf, "\r\n");
    if (request_line == NULL) {
        free(temp_buf);
        return -1;
    }
    parse->buf = strdup(request_line);

    char *method, *uri, *version;
    method = strtok(request_line, " ");
    uri = strtok(NULL, " ");
    version = strtok(NULL, "");

    if (method == NULL || uri == NULL || version == NULL) {
        free(temp_buf);
        return -1;
    }

    parse->method = strdup(method);
    parse->version = strdup(version);

    if (strcmp(parse->method, "CONNECT") == 0) {
        char *host = strtok(uri, ":");
        char *port = strtok(NULL, "");
        if (host) parse->host = strdup(host);
        if (port) parse->port = strdup(port);
        free(temp_buf);
        return 0;
    }

    if (strcmp(parse->method, "GET") != 0) {
        free(temp_buf);
        return -1;
    }

    char *uri_copy = strdup(uri);
    char *uri_ptr = uri_copy;

    if (strstr(uri_ptr, "://") != NULL) {
        uri_ptr = strstr(uri_ptr, "://") + 3;
    }

    char *path_ptr = strchr(uri_ptr, '/');
    if (path_ptr == NULL) {
        parse->host = strdup(uri_ptr);
        parse->path = strdup("/");
    } else {
        *path_ptr = '\0';
        parse->host = strdup(uri_ptr);
        *path_ptr = '/'; // Restore for path
        parse->path = strdup(path_ptr);
    }

    char *port_ptr = strchr(parse->host, ':');
    if (port_ptr != NULL) {
        *port_ptr = '\0';
        parse->port = strdup(port_ptr + 1);
    }
    
    free(uri_copy);
    free(temp_buf);
    
    if (strlen(parse->host) == 0) {
        debug("Parse error: could not extract host\n");
        return -1;
    }

    return 0;
}

// Stub functions to satisfy the header file
int ParsedHeader_set(struct ParsedRequest *pr, const char *key, const char *value) { return 0; }
struct ParsedHeader* ParsedHeader_get(struct ParsedRequest *pr, const char *key) { return NULL; }
int ParsedHeader_remove(struct ParsedRequest *pr, const char *key) { return 0; }
int ParsedRequest_unparse(struct ParsedRequest *pr, char *buf, size_t buflen) { return 0; }
int ParsedRequest_unparse_headers(struct ParsedRequest *pr, char *buf, size_t buflen) { return 0; }
size_t ParsedRequest_totalLen(struct ParsedRequest *pr) { return 0; }
size_t ParsedHeader_headersLen(struct ParsedRequest *pr) { return 0; }
