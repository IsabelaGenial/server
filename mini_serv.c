#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h> // Para sprintf
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>

// --- Variáveis Globais ---
int             clients_id[65536];
char*           clients_msg[65536];
int             server_fd, max_fd, next_id = 0;
fd_set          active_fds, read_fds;
char            send_buffer[1024 * 42]; // Tamanho generoso
char            recv_buffer[1024 * 42];

// --- Funções Auxiliares ---

void fatal_error() {
    write(2, "Fatal error\n", 12);
    close(server_fd); // Boa prática fechar o socket principal antes de sair
    exit(1);
}

void send_to_all(int sender_fd) {
    for (int fd = 0; fd <= max_fd; fd++) {
        // Envia apenas para clientes ativos, que não sejam o remetente nem o servidor
        if (FD_ISSET(fd, &active_fds) && fd != sender_fd && fd != server_fd) {
            if (send(fd, send_buffer, strlen(send_buffer), 0) < 0) {
                // Em caso de erro, podemos ignorar para este exercício
            }
        }
    }
}

void accept_new_client() {
    struct sockaddr_in  client_addr;
    socklen_t           len = sizeof(client_addr);
    int                 client_fd;

    client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &len);
    if (client_fd < 0)
        return;

    // Atualiza o descritor de arquivo máximo, se necessário
    if (client_fd > max_fd)
        max_fd = client_fd;

    clients_id[client_fd] = next_id++;
    clients_msg[client_fd] = NULL; // Importante inicializar como NULL

    FD_SET(client_fd, &active_fds);

    sprintf(send_buffer, "server: client %d just arrived\n", clients_id[client_fd]);
    send_to_all(client_fd);
}

void process_client_data(int fd) {
    int bytes_read = recv(fd, recv_buffer, sizeof(recv_buffer) - 1, 0);

    // Cliente desconectou
    if (bytes_read <= 0) {
        sprintf(send_buffer, "server: client %d just left\n", clients_id[fd]);
        send_to_all(fd);

        // Libera recursos do cliente
        if (clients_msg[fd]) {
            free(clients_msg[fd]);
            clients_msg[fd] = NULL;
        }
        close(fd);
        FD_CLR(fd, &active_fds);
        return;
    }

    recv_buffer[bytes_read] = '\0';

    // Anexa os dados recebidos ao buffer do cliente usando realloc
    if (clients_msg[fd] == NULL) {
        // Primeira vez que recebemos dados, usamos malloc (ou strdup se permitido)
        clients_msg[fd] = malloc(strlen(recv_buffer) + 1);
        if (clients_msg[fd] == NULL) fatal_error();
        strcpy(clients_msg[fd], recv_buffer);
    } else {
        // Anexa novos dados usando realloc, que é mais eficiente
        char *temp_ptr = realloc(clients_msg[fd], strlen(clients_msg[fd]) + strlen(recv_buffer) + 1);
        if (temp_ptr == NULL) fatal_error();
        clients_msg[fd] = temp_ptr;
        strcat(clients_msg[fd], recv_buffer);
    }

    // Processa todas as mensagens completas (terminadas em '\n') no buffer
    char *line_start = clients_msg[fd];
    char *newline_pos;
    while ((newline_pos = strstr(line_start, "\n"))) {
        *newline_pos = '\0'; // Separa a linha

        sprintf(send_buffer, "client %d: %s\n", clients_id[fd], line_start);
        send_to_all(fd);

        line_start = newline_pos + 1; // Aponta para o início da próxima linha (ou do resto)
    }

    // Guarda a parte restante (mensagem incompleta)
    if (*line_start) {
        // Há uma mensagem incompleta. Usamos a abordagem segura de malloc/strcpy/free
        // pois memmove não é permitido e strcpy com sobreposição é UB.
        char *remaining_msg = malloc(strlen(line_start) + 1);
        if (remaining_msg == NULL) fatal_error();
        strcpy(remaining_msg, line_start);
        free(clients_msg[fd]);
        clients_msg[fd] = remaining_msg;
    } else {
        // Nenhuma parte incompleta, tudo foi enviado. Libera o buffer.
        free(clients_msg[fd]);
        clients_msg[fd] = NULL;
    }
}


// --- Função Principal ---

int main(int argc, char *argv[]) {
    if (argc != 2 ) {
        write(2, "Wrong number of arguments\n", 26);
        exit(1);
    }

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0)
        fatal_error();

    // Inicialização dos conjuntos de descritores e arrays
    FD_ZERO(&active_fds);
    memset(clients_id, 0, sizeof(clients_id)); // memset é preferível a bzero
    memset(clients_msg, 0, sizeof(clients_msg));

    FD_SET(server_fd, &active_fds);
    max_fd = server_fd;

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(2130706433); // 127.0.0.1
    serv_addr.sin_port = htons(atoi(argv[1]));

    if (bind(server_fd, (const struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
        fatal_error();

    if (listen(server_fd, 128) < 0)
        fatal_error();

    while (1) {
        read_fds = active_fds;

        if (select(max_fd + 1, &read_fds, NULL, NULL, NULL) < 0)
            continue; // Em caso de erro no select, apenas continuamos o loop

        for (int fd = 0; fd <= max_fd; fd++) {
            if (FD_ISSET(fd, &read_fds)) {
                if (fd == server_fd) {
                    accept_new_client();
                    break; // Uma nova conexão pode alterar max_fd, então é bom reavaliar
                } else {
                    process_client_data(fd);
                }
            }
        }
    }
    return 0; // Inalcançável, mas bom para a completude
}