#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>

// --- Variáveis Globais ---
int             clients_id[65536];
char*           clients_msg[65536];
int             server_fd, max_fd, next_id = 0;
fd_set          active_fds, read_fds;
char            send_buffer[100000];
char            recv_buffer[100000];

// --- Funções Auxiliares ---

void fatal_error() {
    write(2, "Fatal error\n", 12);
    exit(1);
}

void send_to_all(int sender_fd) {
    for (int fd = 0; fd <= max_fd; fd++) {
        if (FD_ISSET(fd, &active_fds) && fd != sender_fd && fd != server_fd) {
            if (send(fd, send_buffer, strlen(send_buffer), 0) < 0) {
                // Em caso de erro, para este exercício, simplesmente ignoramos e continuamos.
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

    if (bytes_read <= 0) {
        sprintf(send_buffer, "server: client %d just left\n", clients_id[fd]);
        send_to_all(fd);

        if (clients_msg[fd]) {
            free(clients_msg[fd]);
            clients_msg[fd] = NULL;
        }
        close(fd);
        FD_CLR(fd, &active_fds);
        return;
    }

    recv_buffer[bytes_read] = '\0';

    // Anexa os dados recebidos ao buffer do cliente.
    if (clients_msg[fd] == NULL) {
        // CORREÇÃO: Removida a linha com strdup. Alocação manual direta.
        clients_msg[fd] = malloc(strlen(recv_buffer) + 1);
        if (clients_msg[fd] == NULL) fatal_error();
        strcpy(clients_msg[fd], recv_buffer);
    } else {
        char *temp_ptr = clients_msg[fd];
        clients_msg[fd] = malloc(strlen(temp_ptr) + strlen(recv_buffer) + 1);
        if (clients_msg[fd] == NULL) fatal_error();
        strcpy(clients_msg[fd], temp_ptr);
        strcat(clients_msg[fd], recv_buffer);
        free(temp_ptr);
    }

    char *line_start = clients_msg[fd];
    char *newline_pos;
    while ((newline_pos = strstr(line_start, "\n"))) {
        *newline_pos = '\0';

        sprintf(send_buffer, "client %d: %s\n", clients_id[fd], line_start);
        send_to_all(fd);

        line_start = newline_pos + 1;
    }

    // Guarda a parte restante (incompleta) da mensagem.
    if (*line_start) {
        // CORREÇÃO: Removida a linha com strdup e a variável 'temp' não utilizada.
        char *remaining_msg = malloc(strlen(line_start) + 1);
        if (remaining_msg == NULL) fatal_error();
        strcpy(remaining_msg, line_start);
        free(clients_msg[fd]);
        clients_msg[fd] = remaining_msg;
    } else {
        free(clients_msg[fd]);
        clients_msg[fd] = NULL;
    }
}


// --- Função Principal ---

int main(int argc, char *argv[]) {
    if (argc != 2) {
        write(2, "Wrong number of arguments\n", 26);
        exit(1);
    }

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0)
        fatal_error();

    FD_ZERO(&active_fds);
    bzero(clients_id, sizeof(clients_id));
    bzero(clients_msg, sizeof(clients_msg));

    FD_SET(server_fd, &active_fds);
    max_fd = server_fd;

    struct sockaddr_in serv_addr;
    bzero(&serv_addr, sizeof(serv_addr));
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
            continue;

        for (int fd = 0; fd <= max_fd; fd++) {
            if (FD_ISSET(fd, &read_fds)) {
                if (fd == server_fd) {
                    accept_new_client();
                } else {
                    process_client_data(fd);
                }
            }
        }
    }
    return 0;
}