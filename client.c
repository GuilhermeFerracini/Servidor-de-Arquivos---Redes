#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <strings.h>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <fcntl.h>

#define BUFFER_SIZE 4096
#define MAX_FILENAME 256
#define CLIENT_DIR "./client_files"
// Help
void show_usage(const char *program_name) {
    printf("Uso:\n");
    printf("  %s <ip> <porta> GET <arquivo>\n", program_name);
    printf("  %s <ip> <porta> PUT <arquivo>\n", program_name);
    printf("  %s <ip> <porta> DELETE <arquivo>\n", program_name);
    printf("\nExemplos:\n");
    printf("  %s 127.0.0.1 8080 GET test.txt\n", program_name);
    printf("  %s 127.0.0.1 8080 PUT myfile.txt\n", program_name);
    printf("  %s 127.0.0.1 8080 DELETE oldfile.txt\n", program_name);
}
// Encapsula a conexão com o servidor
int connect_to_server(const char *ip, const char *port) {
    int cfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (cfd == -1) {
        perror("socket()");
        return -1;
    }
    
    struct sockaddr_in saddr;
    saddr.sin_port = htons(atoi(port));
    saddr.sin_family = AF_INET;
    saddr.sin_addr.s_addr = inet_addr(ip);
    
    if (connect(cfd, (struct sockaddr*) &saddr, sizeof(struct sockaddr_in)) == -1) {
        perror("connect()");
        close(cfd);
        return -1;
    }
    
    return cfd;
}

void handle_get(const char *ip, const char *port, const char *filename) {
    // 1. Lógica do GET Condicional
    char filepath[512];
    struct stat file_stat;
    time_t client_timestamp = 0;
    
    snprintf(filepath, sizeof(filepath), "%s/%s", CLIENT_DIR, filename);
    
    if (stat(filepath, &file_stat) == 0) {
        client_timestamp = file_stat.st_mtime; // Pega a data do arquivo local
        printf("Arquivo existe localmente (modificado em: %s", ctime(&client_timestamp));
    }
    
    int cfd = connect_to_server(ip, port);
    if (cfd < 0) return;
    
    char request[512];
    int request_len;
    // 2. Formata a Requisição
    if (client_timestamp > 0) {
        request_len = snprintf(request, sizeof(request), "GET %s %ld\n", filename, client_timestamp);
    } else {
        request_len = snprintf(request, sizeof(request), "GET %s 0\n", filename);
    }
    
    if (send(cfd, request, request_len, 0) < 0) {
        perror("send()");
        close(cfd);
        return;
    }
    // 3. Recebe a Resposta
    char response[512];
    int nr = recv(cfd, response, sizeof(response) - 1, 0);
    if (nr <= 0) {
        printf("Erro ao receber resposta\n");
        close(cfd);
        return;
    }
    
    response[nr] = '\0';
    
    int status_code;
    char status_msg[256];
    char *line_end = strchr(response, '\n');
    if (!line_end) {
        printf("Resposta inválida\n");
        close(cfd);
        return;
    }
    
    *line_end = '\0';
    sscanf(response, "%d %[^\n]", &status_code, status_msg);
    
    printf("Status: %d %s\n", status_code, status_msg);
    // 4. Trata a Resposta
    if (status_code == 200) {
        char *size_line = line_end + 1;
        char *size_end = strchr(size_line, '\n');
        if (!size_end) {
            printf("Resposta inválida\n");
            close(cfd);
            return;
        }
        
        *size_end = '\0';
        size_t file_size = atol(size_line);
        
        char *mtime_line = size_end + 1;
        char *mtime_end = strchr(mtime_line, '\n');
        if (!mtime_end) {
            printf("Resposta inválida\n");
            close(cfd);
            return;
        }
        
        *mtime_end = '\0';
        time_t mtime = atol(mtime_line);
        
        printf("Tamanho do arquivo: %zu bytes\n", file_size);
        printf("Data de modificação: %s", ctime(&mtime));
        
        FILE *file = fopen(filepath, "wb");
        if (!file) {
            perror("Erro ao criar arquivo local");
            close(cfd);
            return;
        }
        
        size_t total_received = 0;
        char buffer[BUFFER_SIZE];
        // 5. Escreve os dados iniciais recebidos na resposta
        size_t header_size = (mtime_end + 1) - response;
        size_t data_in_first_packet = nr - header_size;
        
        if (data_in_first_packet > 0) {
            fwrite(mtime_end + 1, 1, data_in_first_packet, file);
            total_received += data_in_first_packet;
        }
        // 7. Loop de Recebimento
        while (total_received < file_size) {
            nr = recv(cfd, buffer, BUFFER_SIZE, 0);
            if (nr <= 0) break;
            fwrite(buffer, 1, nr, file);
            total_received += nr;
        }
        
        fclose(file);
        
        struct timespec times[2];
        times[0].tv_sec = mtime;
        times[0].tv_nsec = 0;
        times[1].tv_sec = mtime;
        times[1].tv_nsec = 0;
        utimensat(AT_FDCWD, filepath, times, 0); // 8. Atualiza a data do arquivo local
        
        printf("Arquivo baixado com sucesso: %s (%zu bytes)\n", filepath, total_received);
    } else if (status_code == 304) {
        printf("Arquivo local está atualizado\n");
    }
    
    close(cfd);
}

void handle_put(const char *ip, const char *port, const char *filename) {
    char filepath[512];
    struct stat file_stat;
    // 1. Verifica se o arquivo local existe
    snprintf(filepath, sizeof(filepath), "%s/%s", CLIENT_DIR, filename);
    
    if (stat(filepath, &file_stat) != 0) {
        printf("Erro: arquivo local não encontrado: %s\n", filepath);
        return;
    }
    
    FILE *file = fopen(filepath, "rb");
    if (!file) {
        perror("Erro ao abrir arquivo local");
        return;
    }
    
    int cfd = connect_to_server(ip, port);
    if (cfd < 0) {
        fclose(file);
        return;
    }
    
    char request[512];
    // 2. Formata o cabeçalho PUT
    int request_len = snprintf(request, sizeof(request), "PUT %s\n%ld\n", 
                               filename, file_stat.st_size);
    
    if (send(cfd, request, request_len, 0) < 0) {
        perror("send()");
        fclose(file);
        close(cfd);
        return;
    }
    
    char buffer[BUFFER_SIZE];
    size_t bytes_read;
    size_t total_sent = 0;
    // 3. Loop de envio do arquivo
    while ((bytes_read = fread(buffer, 1, BUFFER_SIZE, file)) > 0) {
        if (send(cfd, buffer, bytes_read, 0) < 0) {
            perror("send()");
            fclose(file);
            close(cfd);
            return;
        }
        total_sent += bytes_read;
    }
    
    fclose(file);
    
    printf("Enviados %zu bytes\n", total_sent);
    
    char response[512];
    // 4. Espera a confirmação
    int nr = recv(cfd, response, sizeof(response) - 1, 0);
    if (nr > 0) {
        response[nr] = '\0';
        int status_code;
        char status_msg[256];
        sscanf(response, "%d %[^\n]", &status_code, status_msg);
        printf("Status: %d %s\n", status_code, status_msg);
    }
    
    close(cfd);
}

void handle_delete(const char *ip, const char *port, const char *filename) {
    int cfd = connect_to_server(ip, port);
    if (cfd < 0) return;
    
    char request[512];
    int request_len = snprintf(request, sizeof(request), "DELETE %s\n", filename);
    
    if (send(cfd, request, request_len, 0) < 0) {
        perror("send()");
        close(cfd);
        return;
    }
    
    char response[512];
    int nr = recv(cfd, response, sizeof(response) - 1, 0);
    if (nr > 0) {
        response[nr] = '\0';
        int status_code;
        char status_msg[256];
        sscanf(response, "%d %[^\n]", &status_code, status_msg);
        printf("Status: %d %s\n", status_code, status_msg);
    }
    
    close(cfd);
}

int main(int argc, char **argv) {
    if (argc < 5) {
        show_usage(argv[0]);
        return 0;
    }
    
    struct stat st = {0};
    if (stat(CLIENT_DIR, &st) == -1) {
        mkdir(CLIENT_DIR, 0700);
    }
    
    const char *ip = argv[1];
    const char *port = argv[2];
    const char *command = argv[3];
    const char *filename = argv[4];
    
    if (strcmp(command, "GET") == 0) {
        handle_get(ip, port, filename);
    } else if (strcmp(command, "PUT") == 0) {
        handle_put(ip, port, filename);
    } else if (strcmp(command, "DELETE") == 0) {
        handle_delete(ip, port, filename);
    } else {
        printf("Comando desconhecido: %s\n", command);
        show_usage(argv[0]);
        return 1;
    }
    
    return 0;
}
