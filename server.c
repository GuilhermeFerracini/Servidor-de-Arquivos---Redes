#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <sys/stat.h>
#include <time.h>
#include <errno.h>


#define MAX_REQ 65536 
#define MAX_FILENAME 256
#define BUFFER_SIZE 4096
#define SERVER_DIR "./server_files"

struct targs {
    pthread_t tid;
    int cfd;
    struct sockaddr_in caddr;
};
typedef struct targs targs;

// Mutex para garantir acesso exclusivo ao arquivo durante operação por thread
pthread_mutex_t file_mutex = PTHREAD_MUTEX_INITIALIZER;

// void init(targs *tclients, int n) {
//     int i;
//     for (i = 0; i < MAX_CONN + 3; i++) {
//         tclients[i].cfd = -1;
//     }
// }

// respostas de status
void send_response(int cfd, int status_code, const char *message, const char *data, size_t data_len) {
    char header[512];
    int header_len;
    
    header_len = snprintf(header, sizeof(header), "%d %s\n", status_code, message);
    send(cfd, header, header_len, 0);
    
    if (data && data_len > 0) {
        send(cfd, data, data_len, 0);
    }
}

void handle_get(int cfd, const char *filename, time_t client_timestamp) {
    char filepath[512];
    struct stat file_stat;
    FILE *file;
    char buffer[BUFFER_SIZE];
    size_t bytes_read;

    // Abre o diretorio
    snprintf(filepath, sizeof(filepath), "%s/%s", SERVER_DIR, filename);
    // Trava o mutex
    pthread_mutex_lock(&file_mutex);
    // Verifica se o arquivo existe
    if (stat(filepath, &file_stat) != 0) {
        pthread_mutex_unlock(&file_mutex);
        send_response(cfd, 404, "File not found", NULL, 0);
        printf("GET %s: 404 File not found\n", filename);
        return;
    }
    // Verifica se o arquivo foi modificado 
    if (client_timestamp > 0 && file_stat.st_mtime <= client_timestamp) {
        pthread_mutex_unlock(&file_mutex);
        send_response(cfd, 304, "Not Modified", NULL, 0);
        printf("GET %s: 304 Not Modified\n", filename);
        return;
    }
    file = fopen(filepath, "rb");
    // Verifica se o arquivo foi aberto
    if (!file) {
        pthread_mutex_unlock(&file_mutex);
        send_response(cfd, 500, "Error opening file", NULL, 0);
        printf("GET %s: 500 Error opening file\n", filename); 
        return;
    }
    // Envia o cabeçalho da resposta
    char response_header[512];
    int header_len = snprintf(response_header, sizeof(response_header), 
                              "200 OK\n%ld\n%ld\n", // 200 OK tamanho e data 
                              file_stat.st_size, 
                              file_stat.st_mtime);
    send(cfd, response_header, header_len, 0);
    // Loop de envio do conteúdo do arquivo
    while ((bytes_read = fread(buffer, 1, BUFFER_SIZE, file)) > 0) {
        send(cfd, buffer, bytes_read, 0);
    }
    
    fclose(file);
    pthread_mutex_unlock(&file_mutex);
    
    printf("GET %s: 200 OK (size: %ld bytes, mtime: %ld)\n", 
           filename, file_stat.st_size, file_stat.st_mtime);
}

void handle_put(int cfd, const char *filename, size_t expected_size, const char *initial_data, size_t initial_len) {
    char filepath[512];
    FILE *file;
    // Abre o diretorio
    snprintf(filepath, sizeof(filepath), "%s/%s", SERVER_DIR, filename);
    // Trava o mutex
    pthread_mutex_lock(&file_mutex);
    
    file = fopen(filepath, "wb"); // "wb" = Write Binary (sobrescreve o arquivo)
    // Verifica se o arquivo foi aberto
    if (!file) {
        pthread_mutex_unlock(&file_mutex);
        send_response(cfd, 500, "Error creating file", NULL, 0);
        printf("PUT %s: 500 Error creating file\n", filename);
        return;
    }
    
    size_t total_received = 0;
    // Escreve os dados iniciais recebidos na requisição
    if (initial_len > 0) {
        fwrite(initial_data, 1, initial_len, file);
        total_received += initial_len;
    }
    // Loop de recebimento do conteúdo do arquivo
    char buffer[BUFFER_SIZE];
    while (total_received < expected_size) {
        int nr = recv(cfd, buffer, BUFFER_SIZE, 0);
        if (nr <= 0) break;
        fwrite(buffer, 1, nr, file);
        total_received += nr;
    }
    
    fclose(file);
    // VERIFICAÇÃ DE INTEGRIDADE com expected_size
    if (total_received != expected_size) {
        unlink(filepath);
        pthread_mutex_unlock(&file_mutex);
        send_response(cfd, 500, "Incomplete file transfer", NULL, 0);
        printf("PUT %s: 500 Incomplete transfer (%zu/%zu bytes)\n", filename, total_received, expected_size);
        return;
    }
    
    pthread_mutex_unlock(&file_mutex);
    
    send_response(cfd, 200, "File uploaded successfully", NULL, 0);
    printf("PUT %s: 200 OK (%zu bytes written)\n", filename, total_received);
}

void handle_delete(int cfd, const char *filename) {
    char filepath[512];
    // Abre o diretorio
    snprintf(filepath, sizeof(filepath), "%s/%s", SERVER_DIR, filename);
    // Trava o mutex
    pthread_mutex_lock(&file_mutex);
    // Verifica se o arquivo existe
    if (access(filepath, F_OK) != 0) { // access é outra forma de checar se existe
        pthread_mutex_unlock(&file_mutex);
        send_response(cfd, 404, "File not found", NULL, 0);
        printf("DELETE %s: 404 File not found\n", filename);
        return;
    }
    // Deleta o arquivo
    if (unlink(filepath) != 0) { // unlink é a chamada de sistema para deletar
        pthread_mutex_unlock(&file_mutex);
        send_response(cfd, 500, "Error deleting file", NULL, 0);
        printf("DELETE %s: 500 Error deleting file\n", filename);
        return;
    }
    
    pthread_mutex_unlock(&file_mutex);
    
    send_response(cfd, 200, "File deleted successfully", NULL, 0);
    printf("DELETE %s: 200 OK\n", filename);
}

void *handle_client(void *args) {
    int my_cfd = *(int *)args; // 1. Recebe o ponteiro
    free(args); // 2. Libera a memória alocada para o ponteiro
    char requisicao[MAX_REQ];
    int nr;
    // RECEBE A REQUISIÇÃO
    bzero(requisicao, MAX_REQ);
    nr = recv(my_cfd, requisicao, MAX_REQ, 0);
    
    if (nr <= 0) {
        close(my_cfd);
        return NULL;
    }
    
    printf("\nRecebeu requisição: %d bytes\n", nr);
    // PARSER
    char command[16], filename[MAX_FILENAME];
    time_t timestamp = 0;
    char *line_end = strchr(requisicao, '\n'); // Transforma o '\n' em fim de string
    
    if (!line_end) {
        send_response(my_cfd, 400, "Bad request", NULL, 0);
        close(my_cfd);
        return NULL;
    }
    
    *line_end = '\0';
    // Lê a primeira linha (ex: "GET arquivo.txt 1234567")
    if (sscanf(requisicao, "%15s %255s %ld", command, filename, &timestamp) < 2) {
        send_response(my_cfd, 400, "Bad request", NULL, 0);
        close(my_cfd);
        return NULL;
    }
    
    if (strcmp(command, "GET") == 0) {
        handle_get(my_cfd, filename, timestamp);
    } 
    else if (strcmp(command, "PUT") == 0) {
        size_t file_size = 0;
        char *size_line = line_end + 1; // Onde a 2ª linha começa
        char *size_end = strchr(size_line, '\n'); // Acha o fim da 2ª linha
        // Ler tamanho do arquivo
        if (size_end) { 
            *size_end = '\0';
            file_size = atol(size_line); 
            char *file_data = size_end + 1; 
            size_t header_size = file_data - requisicao;
            size_t data_received = nr - header_size;
            
            handle_put(my_cfd, filename, file_size, file_data, data_received);
        } else {
            send_response(my_cfd, 400, "Bad request - missing file size", NULL, 0);
        }
    }
    else if (strcmp(command, "DELETE") == 0) {
        handle_delete(my_cfd, filename);
    }
    else {
        send_response(my_cfd, 400, "Unknown command", NULL, 0);
        printf("Unknown command: %s\n", command);
    }
    
    close(my_cfd);
    return NULL;
}

int main(int argc, char **argv) {
    if (argc != 2) {
        printf("Uso: %s <porta>\n", argv[0]);
        return 0;
    }
    // CRIA O DIRETÓRIO 
    struct stat st = {0};
    if (stat(SERVER_DIR, &st) == -1) {
        mkdir(SERVER_DIR, 0700);
    }
    

    // Cria o socket
    struct sockaddr_in saddr;
    int sl = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    
    if (sl < 0) {
        perror("erro no socket()");
        return -1;
    }
    // REINÍCIO RÁPIDO
    int opt = 1;
    setsockopt(sl, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    saddr.sin_family = AF_INET;
    saddr.sin_port = htons(atoi(argv[1]));
    saddr.sin_addr.s_addr = INADDR_ANY;
    
    if (bind(sl, (const struct sockaddr *)&saddr, sizeof(struct sockaddr_in)) < 0) {
        perror("erro no bind()");
        return -1;
    }
    
    if (listen(sl, 1000) < 0) {
        perror("erro no listen()");
        return -1;
    }
    
    printf("Servidor de arquivos iniciado na porta %s\n", argv[1]);
    printf("Diretório de arquivos: %s\n", SERVER_DIR);
    printf("Aguardando conexões...\n\n");
    
    int cfd, addr_len;
    struct sockaddr_in caddr;
    addr_len = sizeof(struct sockaddr_in);
    // Loop infinito para aceitar conexões
    while(1) {
        cfd = accept(sl, (struct sockaddr *)&caddr, (socklen_t *)&addr_len); 
        
        if (cfd < 0) {
            perror("erro no accept()");
            continue;
        }
        
        int *cfd_ptr = malloc(sizeof(int)); // 1. Aloca memória para o ponteiro
        if (cfd_ptr == NULL) {
            close(cfd);
            continue;
        }
        *cfd_ptr = cfd; // 2. Armazena o descritor de arquivo no ponteiro
        
        pthread_t tid; // 3. Cria a thread
        if (pthread_create(&tid, NULL, handle_client, (void*)cfd_ptr) != 0) {
            free(cfd_ptr); // 4. Libera a memória alocada para o ponteiro
            close(cfd); // 5. Fecha o descritor de arquivo
            continue;
        }
        pthread_detach(tid); // 6. Thread libera recursos automaticamente
    }
    
    return 0;
}
