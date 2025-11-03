CC = gcc
CFLAGS = -Wall -pthread
TARGETS = server client

all: $(TARGETS)

server: server.c
	$(CC) $(CFLAGS) -o server server.c

client: client.c
	$(CC) $(CFLAGS) -o client client.c

clean:
	rm -f $(TARGETS)
	rm -rf server_files client_files

test_files:
	mkdir -p client_files
	echo "Hello World!" > client_files/test.txt
	echo "Este é um arquivo de teste" > client_files/teste.txt
	echo "Sample data for testing" > client_files/sample.dat

.PHONY: all clean test_files
