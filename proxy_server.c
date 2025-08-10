#include "proxy_parse.h"
#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

/* --- Default Configuration --- */
#define DEFAULT_PORT 8080
#define DEFAULT_THREADS 8
#define DEFAULT_CACHE_SIZE (200 * 1024 * 1024)
#define DEFAULT_ELEMENT_SIZE (10 * 1024 * 1024)

#define MAX_CLIENTS 100
#define MAX_REQUEST_LEN 8192
#define MAX_BLACKLIST_DOMAINS 100
#define CACHE_HASHTABLE_SIZE 1024

/* --- Global Configuration Variables --- */
int g_port = DEFAULT_PORT;
int g_thread_pool_size = DEFAULT_THREADS;
size_t g_max_cache_size = DEFAULT_CACHE_SIZE;
size_t g_max_element_size = DEFAULT_ELEMENT_SIZE;

/* --- Global Variables --- */
FILE *log_file;
pthread_mutex_t log_mutex;
char *blacklist[MAX_BLACKLIST_DOMAINS];
int blacklist_count = 0;
volatile sig_atomic_t server_running = 1;

/* --- Forward Declarations --- */
void handle_request(int client_socket);
void* worker_thread(void *arg);
void handle_http_request(int client_socket, struct ParsedRequest *req, const char* original_request, int request_len);
void handle_connect_request(int client_socket, struct ParsedRequest *req);

/* --- Robust Logging --- */
void log_message(const char* level, const char* format, ...) {
    pthread_mutex_lock(&log_mutex);
    time_t now = time(NULL);
    char time_buf[25];
    strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", localtime(&now));
    fprintf(log_file, "[%s] [%s] ", time_buf, level);
    va_list args;
    va_start(args, format);
    vfprintf(log_file, format, args);
    va_end(args);
    fprintf(log_file, "\n");
    fflush(log_file);
    pthread_mutex_unlock(&log_mutex);
}

/* --- Configuration and Blacklist Loading --- */
void load_configuration(const char *filename) {
    FILE *file = fopen(filename, "r");
    if (!file) {
        printf("WARN: Configuration file '%s' not found. Using defaults.\n", filename);
        return;
    }
    char line[256];
    while (fgets(line, sizeof(line), file)) {
        char key[64], value[128];
        if (sscanf(line, "%63s = %127s", key, value) == 2) {
            if (strcmp(key, "port") == 0) g_port = atoi(value);
            else if (strcmp(key, "threads") == 0) g_thread_pool_size = atoi(value);
            else if (strcmp(key, "cache_size_mb") == 0) g_max_cache_size = (size_t)atoi(value) * 1024 * 1024;
            else if (strcmp(key, "element_size_mb") == 0) g_max_element_size = (size_t)atoi(value) * 1024 * 1024;
        }
    }
    fclose(file);
    printf("INFO: Configuration loaded from '%s'.\n", filename);
}

void load_blacklist(const char *filename) {
    FILE *file = fopen(filename, "r");
    if (!file) {
        printf("WARN: Blacklist file '%s' not found. No domains will be blocked.\n", filename);
        return;
    }
    char domain[256];
    while (blacklist_count < MAX_BLACKLIST_DOMAINS && fgets(domain, sizeof(domain), file)) {
        domain[strcspn(domain, "\r\n")] = 0; // Remove newline
        if (strlen(domain) > 0) {
            blacklist[blacklist_count++] = strdup(domain);
        }
    }
    fclose(file);
    if (blacklist_count > 0) {
        printf("INFO: Loaded %d domains into the blacklist from '%s'.\n", blacklist_count, filename);
    }
}

int is_blacklisted(const char *host) {
    if (!host) return 0;
    for (int i = 0; i < blacklist_count; i++) {
        if (strstr(host, blacklist[i])) {
            return 1;
        }
    }
    return 0;
}

/* --- HIGH-PERFORMANCE LRU CACHE --- */
typedef struct CacheNode {
    char *key; char *data; size_t data_size;
    struct CacheNode *prev, *next; struct CacheNode *h_next;
} CacheNode;

typedef struct {
    size_t capacity; size_t size; int table_size;
    CacheNode **table; CacheNode *head, *tail; pthread_mutex_t lock;
} LRUCache;

LRUCache *cache;
static unsigned long hash(const char *str) {
    unsigned long hash = 5381; int c;
    while ((c = *str++)) hash = ((hash << 5) + hash) + c;
    return hash;
}
void detach_node(LRUCache *c, CacheNode *node) {
    if (node->prev) node->prev->next = node->next; else c->head = node->next;
    if (node->next) node->next->prev = node->prev; else c->tail = node->prev;
}
void attach_to_front(LRUCache *c, CacheNode *node) {
    node->next = c->head; node->prev = NULL;
    if (c->head) c->head->prev = node;
    c->head = node; if (c->tail == NULL) c->tail = node;
}
LRUCache* create_cache(size_t capacity, int table_size) {
    LRUCache *c = (LRUCache*)malloc(sizeof(LRUCache));
    c->capacity = capacity; c->size = 0; c->table_size = table_size;
    c->head = c->tail = NULL;
    c->table = (CacheNode**)calloc(table_size, sizeof(CacheNode*));
    pthread_mutex_init(&c->lock, NULL); return c;
}
CacheNode* get_from_cache(const char *key) {
    pthread_mutex_lock(&cache->lock);
    unsigned long h = hash(key) % cache->table_size;
    CacheNode *node = cache->table[h];
    while (node) {
        if (strcmp(node->key, key) == 0) {
            detach_node(cache, node); attach_to_front(cache, node);
            pthread_mutex_unlock(&cache->lock);
            log_message("INFO", "Cache HIT for request key.");
            return node;
        }
        node = node->h_next;
    }
    pthread_mutex_unlock(&cache->lock);
    log_message("INFO", "Cache MISS for request key.");
    return NULL;
}
void evict_lru() {
    CacheNode *lru_node = cache->tail; if (!lru_node) return;
    detach_node(cache, lru_node);
    unsigned long h = hash(lru_node->key) % cache->table_size;
    CacheNode *curr = cache->table[h], *prev = NULL;
    while (curr) {
        if (curr == lru_node) {
            if (prev) prev->h_next = curr->h_next; else cache->table[h] = curr->h_next;
            break;
        }
        prev = curr; curr = curr->h_next;
    }
    cache->size -= lru_node->data_size;
    log_message("INFO", "Evicting item. Cache size: %zu bytes", cache->size);
    free(lru_node->key); free(lru_node->data); free(lru_node);
}
void put_in_cache(const char *key, const char *data, size_t data_size) {
    if (data_size > g_max_element_size) {
        log_message("WARN", "Item too large to cache (%zu bytes)", data_size); return;
    }
    pthread_mutex_lock(&cache->lock);
    while (cache->size + data_size > cache->capacity) { evict_lru(); }
    CacheNode *new_node = (CacheNode*)malloc(sizeof(CacheNode));
    new_node->key = strdup(key);
    new_node->data = (char*)malloc(data_size);
    memcpy(new_node->data, data, data_size);
    new_node->data_size = data_size;
    attach_to_front(cache, new_node);
    cache->size += data_size;
    unsigned long h = hash(key) % cache->table_size;
    new_node->h_next = cache->table[h];
    cache->table[h] = new_node;
    log_message("INFO", "Stored new item. Cache size: %zu bytes", cache->size);
    pthread_mutex_unlock(&cache->lock);
}

/* --- THREAD POOL IMPLEMENTATION --- */
typedef struct {
    int *sockets; int capacity; int size; int head; int tail;
    pthread_mutex_t lock; pthread_cond_t not_empty; pthread_cond_t not_full;
} TaskQueue;
TaskQueue task_queue;
void init_task_queue(int capacity) {
    task_queue.sockets = (int*)malloc(sizeof(int) * capacity);
    task_queue.capacity = capacity; task_queue.size = 0; task_queue.head = 0; task_queue.tail = 0;
    pthread_mutex_init(&task_queue.lock, NULL);
    pthread_cond_init(&task_queue.not_empty, NULL);
    pthread_cond_init(&task_queue.not_full, NULL);
}
void enqueue_task(int client_socket) {
    pthread_mutex_lock(&task_queue.lock);
    while (task_queue.size == task_queue.capacity) { pthread_cond_wait(&task_queue.not_full, &task_queue.lock); }
    task_queue.sockets[task_queue.tail] = client_socket;
    task_queue.tail = (task_queue.tail + 1) % task_queue.capacity;
    task_queue.size++;
    pthread_cond_signal(&task_queue.not_empty);
    pthread_mutex_unlock(&task_queue.lock);
}
int dequeue_task() {
    pthread_mutex_lock(&task_queue.lock);
    while (task_queue.size == 0 && server_running) { pthread_cond_wait(&task_queue.not_empty, &task_queue.lock); }
    if (!server_running && task_queue.size == 0) { pthread_mutex_unlock(&task_queue.lock); return -1; }
    int client_socket = task_queue.sockets[task_queue.head];
    task_queue.head = (task_queue.head + 1) % task_queue.capacity;
    task_queue.size--;
    pthread_cond_signal(&task_queue.not_full);
    pthread_mutex_unlock(&task_queue.lock);
    return client_socket;
}

/* --- SIGNAL HANDLING for Graceful Shutdown --- */
void signal_handler(int sig) {
    if (sig == SIGINT || sig == SIGTERM) {
        server_running = 0;
    }
}

/* --- MAIN SERVER LOGIC --- */
int main(void) {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGPIPE, SIG_IGN); // Ignore broken pipe signals

    load_configuration("proxy.conf");
    load_blacklist("blacklist.txt");

    log_file = fopen("proxy.log", "a");
    if (!log_file) { perror("fopen log file"); exit(EXIT_FAILURE); }
    pthread_mutex_init(&log_mutex, NULL);
    
    log_message("INFO", "Server starting with configuration: Port=%d, Threads=%d, CacheSize=%zuMB", 
                g_port, g_thread_pool_size, g_max_cache_size / (1024*1024));

    cache = create_cache(g_max_cache_size, CACHE_HASHTABLE_SIZE);
    init_task_queue(MAX_CLIENTS);

    pthread_t threads[g_thread_pool_size];
    for (int i = 0; i < g_thread_pool_size; i++) {
        pthread_create(&threads[i], NULL, worker_thread, NULL);
    }

    int server_fd;
    struct sockaddr_in address;
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(g_port);
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        log_message("FATAL", "bind failed: %s", strerror(errno)); exit(EXIT_FAILURE);
    }
    if (listen(server_fd, MAX_CLIENTS) < 0) {
        log_message("FATAL", "listen failed: %s", strerror(errno)); exit(EXIT_FAILURE);
    }

    printf("Proxy server listening on port %d...\n", g_port);

    while (server_running) {
        int client_socket = accept(server_fd, NULL, NULL);
        if (client_socket < 0) {
            if (errno == EINTR && !server_running) break;
            log_message("ERROR", "accept failed: %s", strerror(errno));
            continue;
        }
        enqueue_task(client_socket);
    }

    log_message("INFO", "Shutting down server...");
    pthread_cond_broadcast(&task_queue.not_empty);
    for (int i = 0; i < g_thread_pool_size; i++) {
        pthread_join(threads[i], NULL);
    }
    
    close(server_fd);
    log_message("INFO", "Server shut down cleanly.");
    
    fclose(log_file);
    pthread_mutex_destroy(&log_mutex);
    for(int i = 0; i < blacklist_count; i++) free(blacklist[i]);
    // Free cache memory (can be added here)
    return 0;
}

void* worker_thread(void *arg) {
    while (1) {
        int client_socket = dequeue_task();
        if (client_socket == -1) break;
        handle_request(client_socket);
        close(client_socket);
    }
    return NULL;
}

void handle_request(int client_socket) {
    char *buffer = (char*)malloc(MAX_REQUEST_LEN);
    if (!buffer) return;
    bzero(buffer, MAX_REQUEST_LEN);

    ssize_t bytes_read = recv(client_socket, buffer, MAX_REQUEST_LEN - 1, 0);
    if (bytes_read <= 0) {
        free(buffer);
        return;
    }
    
    struct ParsedRequest *req = ParsedRequest_create();
    if (ParsedRequest_parse(req, buffer, bytes_read) < 0) {
        log_message("ERROR", "Failed to parse request.");
    } else {
        if (is_blacklisted(req->host)) {
            log_message("WARN", "Blocked blacklisted host: %s", req->host);
            const char *forbidden_req = "HTTP/1.1 403 Forbidden\r\nContent-Length: 0\r\n\r\n";
            send(client_socket, forbidden_req, strlen(forbidden_req), 0);
        } else if (req->method && strcmp(req->method, "CONNECT") == 0) {
            handle_connect_request(client_socket, req);
        } else {
            handle_http_request(client_socket, req, buffer, bytes_read);
        }
    }
    ParsedRequest_destroy(req);
    free(buffer);
}

void handle_http_request(int client_socket, struct ParsedRequest *req, const char* original_request, int request_len) {
    // Correctly generate the cache key
    if (req->host == NULL || req->path == NULL) {
        log_message("ERROR", "Cannot generate cache key from incomplete request.");
        return;
    }
    size_t key_len = strlen(req->host) + strlen(req->path) + 1;
    char *cache_key = (char *)malloc(key_len);
    if (!cache_key) { log_message("ERROR", "malloc for cache_key failed"); return; }
    snprintf(cache_key, key_len, "%s%s", req->host, req->path);
    
    CacheNode *cached_item = get_from_cache(cache_key);
    if (cached_item) {
        send(client_socket, cached_item->data, cached_item->data_size, 0);
        free(cache_key);
        return;
    }

    int remote_port = req->port ? atoi(req->port) : 80;
    struct hostent *host = gethostbyname(req->host);
    if (!host) {
        log_message("ERROR", "Cannot resolve hostname for HTTP: %s", req->host);
        free(cache_key);
        return;
    }

    int remote_socket = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in remote_addr;
    remote_addr.sin_family = AF_INET;
    remote_addr.sin_port = htons(remote_port);
    bcopy((char*)host->h_addr, (char*)&remote_addr.sin_addr.s_addr, host->h_length);
    
    if (connect(remote_socket, (struct sockaddr*)&remote_addr, sizeof(remote_addr)) == 0) {
        char new_request[MAX_REQUEST_LEN];
        snprintf(new_request, sizeof(new_request), 
                 "GET %s %s\r\nHost: %s\r\nConnection: close\r\n\r\n",
                 req->path, req->version, req->host);
        
        log_message("INFO", "Forwarding new HTTP request for %s", req->host);
        send(remote_socket, new_request, strlen(new_request), 0);

        char *response_buffer = (char*)malloc(g_max_element_size);
        if (response_buffer) {
            ssize_t total_response_size = 0;
            ssize_t response_bytes;
            while ((response_bytes = recv(remote_socket, response_buffer + total_response_size, g_max_element_size - total_response_size, 0)) > 0) {
                send(client_socket, response_buffer + total_response_size, response_bytes, 0);
                total_response_size += response_bytes;
            }
            if (total_response_size > 0) {
                put_in_cache(cache_key, response_buffer, total_response_size);
            }
            free(response_buffer);
        }
    } else {
        log_message("ERROR", "Failed to connect to remote host for HTTP: %s", req->host);
    }
    close(remote_socket);
    free(cache_key);
}

void handle_connect_request(int client_socket, struct ParsedRequest *req) {
    log_message("INFO", "CONNECT request for %s:%s", req->host, req->port);
    int remote_port = req->port ? atoi(req->port) : 443;
    struct hostent *host = gethostbyname(req->host);
    if (!host) {
        log_message("ERROR", "Cannot resolve hostname for CONNECT: %s", req->host);
        return;
    }

    int remote_socket = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in remote_addr;
    remote_addr.sin_family = AF_INET;
    remote_addr.sin_port = htons(remote_port);
    bcopy((char*)host->h_addr, (char*)&remote_addr.sin_addr.s_addr, host->h_length);

    if (connect(remote_socket, (struct sockaddr*)&remote_addr, sizeof(remote_addr)) < 0) {
        log_message("ERROR", "Failed to connect to remote host for CONNECT: %s", req->host);
        close(remote_socket);
        return;
    }

    const char *ok_response = "HTTP/1.1 200 Connection established\r\n\r\n";
    if (send(client_socket, ok_response, strlen(ok_response), 0) < 0) {
        log_message("ERROR", "Failed to send 200 OK to client: %s", strerror(errno));
        close(remote_socket);
        return;
    }

    log_message("INFO", "Tunnel established for %s:%d. Forwarding data.", req->host, remote_port);

    fd_set read_fds;
    int max_fd = (client_socket > remote_socket) ? client_socket : remote_socket;
    
    while (server_running) {
        FD_ZERO(&read_fds);
        FD_SET(client_socket, &read_fds);
        FD_SET(remote_socket, &read_fds);

        struct timeval tv = { .tv_sec = 60, .tv_usec = 0 }; // Increased timeout
        int activity = select(max_fd + 1, &read_fds, NULL, NULL, &tv);
        if (activity < 0) {
            log_message("ERROR", "select() failed in tunnel: %s", strerror(errno));
            break;
        }
        if (activity == 0) continue;

        char buffer[MAX_REQUEST_LEN];
        ssize_t bytes;

        if (FD_ISSET(client_socket, &read_fds)) {
            if ((bytes = recv(client_socket, buffer, sizeof(buffer), 0)) <= 0) break;
            if (send(remote_socket, buffer, bytes, 0) <= 0) break;
        }

        if (FD_ISSET(remote_socket, &read_fds)) {
            if ((bytes = recv(remote_socket, buffer, sizeof(buffer), 0)) <= 0) break;
            if (send(client_socket, buffer, bytes, 0) <= 0) break;
        }
    }
    
    log_message("INFO", "Tunnel closed for %s:%d", req->host, remote_port);
    close(remote_socket);
}
