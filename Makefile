# Makefile for a Multi-threaded Web Proxy Server and a Test Client

# Compiler and flags
CC = gcc
CFLAGS = -Wall -g -pthread

# Executable names
SERVER_TARGET = proxy_server
CLIENT_TARGET = test_client

# Rename your proxy.c to proxy_server.c for consistency
# Or change the line below to: SERVER_SRCS = proxy.c proxy_parse.c
SERVER_SRCS = proxy_server.c proxy_parse.c
CLIENT_SRCS = test_client.c

# Object files
SERVER_OBJS = $(SERVER_SRCS:.c=.o)
CLIENT_OBJS = $(CLIENT_SRCS:.c=.o)

# Default target builds both
all: $(SERVER_TARGET) $(CLIENT_TARGET)

# Rule for the server
$(SERVER_TARGET): $(SERVER_OBJS)
	$(CC) $(CFLAGS) -o $(SERVER_TARGET) $(SERVER_OBJS)

# Rule for the client
$(CLIENT_TARGET): $(CLIENT_OBJS)
	$(CC) $(CFLAGS) -o $(CLIENT_TARGET) $(CLIENT_OBJS)

# Generic rule to compile any .c file into a .o file
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Clean up rule
clean:
	rm -f $(SERVER_TARGET) $(CLIENT_TARGET) $(SERVER_OBJS) $(CLIENT_OBJS)

# Phony targets
.PHONY: all clean
