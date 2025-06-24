
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <stdio.h> // Apenas para sprintf

// Substituição para FD_SETSIZE
const int MAX_SUPPORTED_FDS = 1024; 
// Substituição para SOMAXCONN
const int LISTEN_BACKLOG = 128;

// --- Estruturas ---
typedef struct s_client {
    int id;
    char *write_buf;
    char read_buf[40961]; // 40KB + null
    int read_len;
} t_client;

// --- Variáveis Globais ---
t_client *g_clients[1024]; 
char g_msg_buffer[42000];

int g_server_fd = -1;
int g_next_client_id = 0;
fd_set g_active_fds, g_read_fds, g_write_fds;
int g_max_fd = 0;

// --- Funções Auxiliares ---
void ft_putstr_fd(const char *str, int fd) {
    if (str) {
        write(fd, str, strlen(str));
    }
}

char *ft_strdup(const char *s) {
    if (!s) return NULL;
    size_t len = strlen(s);
    char *new_s = (char *)malloc(len + 1);
    if (!new_s) return NULL;
    strcpy(new_s, s);
    return new_s;
}

void fatal_error() {
    ft_putstr_fd("Fatal error\n", 2);
    if (g_server_fd != -1) {
        close(g_server_fd);
    }
    for (int i = 0; i <= g_max_fd; ++i) {
        if (g_clients[i]) {
            if (g_clients[i]->write_buf) {
                free(g_clients[i]->write_buf);
            }
            free(g_clients[i]);
            g_clients[i] = NULL;
            close(i);
        }
    }
    exit(1);
}

void broadcast_message(int sender_fd, const char *message) {
    for (int fd = 0; fd <= g_max_fd; ++fd) {
        if (FD_ISSET(fd, &g_active_fds) && g_clients[fd] && fd != sender_fd && fd != g_server_fd) { // Cliente existe, não é remetente, não é server
            if (!g_clients[fd]->write_buf) {
                g_clients[fd]->write_buf = ft_strdup(message);
                if (!g_clients[fd]->write_buf) fatal_error();
            } else {
                char *old_buf = g_clients[fd]->write_buf;
                char *new_buf = (char *)malloc(strlen(old_buf) + strlen(message) + 1);
                if (!new_buf) fatal_error();
                strcpy(new_buf, old_buf);
                strcat(new_buf, message);
                free(old_buf);
                g_clients[fd]->write_buf = new_buf;
            }
        }
    }
}

void remove_client(int fd_to_remove) {
    // A verificação `g_clients[fd_to_remove]` já garante que é um FD de cliente válido
    if (fd_to_remove < 0 || fd_to_remove >= MAX_SUPPORTED_FDS || !g_clients[fd_to_remove]) return;

    sprintf(g_msg_buffer, "server: client %d just left\n", g_clients[fd_to_remove]->id);
    broadcast_message(fd_to_remove, g_msg_buffer);

    if (g_clients[fd_to_remove]->write_buf) {
        free(g_clients[fd_to_remove]->write_buf);
    }
    free(g_clients[fd_to_remove]);
    g_clients[fd_to_remove] = NULL;

    close(fd_to_remove);
    FD_CLR(fd_to_remove, &g_active_fds);

    if (fd_to_remove == g_max_fd) {
        g_max_fd = g_server_fd; 
        for (int i = 0; i < fd_to_remove; ++i) { 
            if (FD_ISSET(i, &g_active_fds) && i > g_max_fd) {
                g_max_fd = i;
            }
        }
    }
}

void add_new_client() {
    struct sockaddr_in client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    int client_fd;

    client_fd = accept(g_server_fd, (struct sockaddr *)&client_addr, &client_addr_len);
    if (client_fd < 0) return;

    if (client_fd >= MAX_SUPPORTED_FDS) { 
        close(client_fd);
        return;
    }

    g_clients[client_fd] = (t_client *)calloc(1, sizeof(t_client));
    if (!g_clients[client_fd]) {
        close(client_fd); 
        fatal_error();
    }

    g_clients[client_fd]->id = g_next_client_id++;
    g_clients[client_fd]->write_buf = NULL;
    g_clients[client_fd]->read_len = 0;
    

    FD_SET(client_fd, &g_active_fds);
    if (client_fd > g_max_fd) {
        g_max_fd = client_fd;
    }

    sprintf(g_msg_buffer, "server: client %d just arrived\n", g_clients[client_fd]->id);
    broadcast_message(client_fd, g_msg_buffer); // client_fd é o "sender" para não receber a própria msg
}

void handle_client_read(int fd) {
    t_client *client = g_clients[fd]; // Assumimos que client existe se chegamos aqui
    int bytes_received;

    int space_in_buf = sizeof(client->read_buf) - 1 - client->read_len;
    if (space_in_buf <= 0) { 
        return;
    }

    bytes_received = recv(fd, client->read_buf + client->read_len, space_in_buf, 0);

    if (bytes_received <= 0) {
        remove_client(fd);
        return;
    }

    client->read_len += bytes_received;
    client->read_buf[client->read_len] = '\0';

    char *current_line_start = client->read_buf;
    char *newline_char;
 
    while ((newline_char = strstr(current_line_start, "\n")) != NULL) {
        *newline_char = '\0';

        sprintf(g_msg_buffer, "client %d: %s\n", client->id, current_line_start);
        broadcast_message(fd, g_msg_buffer);

        current_line_start = newline_char + 1;
    }

   
    int processed_len = current_line_start - client->read_buf;
    int remaining_len = client->read_len - processed_len;

    if (remaining_len > 0) {
        memmove(client->read_buf, current_line_start, remaining_len);
        client->read_len = remaining_len;
        client->read_buf[client->read_len] = '\0';
    } else { 
        client->read_len = 0;
        client->read_buf[0] = '\0';
    }
}

void handle_client_write(int fd) {
    t_client *client = g_clients[fd]; 

    if (!client->write_buf || client->write_buf[0] == '\0') {
        return;
    }

    int to_send_len = strlen(client->write_buf);
    int bytes_sent = send(fd, client->write_buf, to_send_len, 0);

    if (bytes_sent < 0) { 
        remove_client(fd);
        return;
    }
    if (bytes_sent == 0 && to_send_len > 0) { 
        remove_client(fd);
        return;
    }

    if (bytes_sent < to_send_len) {
        memmove(client->write_buf, client->write_buf + bytes_sent, to_send_len - bytes_sent + 1); 
    } else {
        free(client->write_buf);
        client->write_buf = NULL;
    }
}


int main(int argc, char **argv) {
    if (argc != 2) {
        ft_putstr_fd("Wrong number of arguments\n", 2);
        exit(1);
    }

    int port = atoi(argv[1]);

    for (int i = 0; i < MAX_SUPPORTED_FDS; ++i) { 
        g_clients[i] = NULL;
    }

    g_server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (g_server_fd < 0) fatal_error();

    
    if (g_server_fd >= MAX_SUPPORTED_FDS) {
        close(g_server_fd);
        fatal_error();
    }


    g_max_fd = g_server_fd;

    struct sockaddr_in servaddr;
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); // 127.0.0.1
    servaddr.sin_port = htons(port);

    if (bind(g_server_fd, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) fatal_error();
    if (listen(g_server_fd, LISTEN_BACKLOG) < 0) fatal_error(); 

    FD_ZERO(&g_active_fds);
    FD_SET(g_server_fd, &g_active_fds);

    while (1) {
        g_read_fds = g_active_fds;
        FD_ZERO(&g_write_fds);

        for (int fd = 0; fd <= g_max_fd; ++fd) {
            if (FD_ISSET(fd, &g_active_fds) && g_clients[fd] && g_clients[fd]->write_buf && g_clients[fd]->write_buf[0] != '\0') {
                FD_SET(fd, &g_write_fds);
            }
        }

        if (select(g_max_fd + 1, &g_read_fds, &g_write_fds, NULL, NULL) < 0) {
            fatal_error(); 
        }

        for (int current_fd = 0; current_fd <= g_max_fd; ++current_fd) {
            if (FD_ISSET(current_fd, &g_read_fds)) {
                if (current_fd == g_server_fd) {
                    add_new_client();
                } else {
                    if (g_clients[current_fd]) { 
                        handle_client_read(current_fd);
                    }
                }
            }
             
            if (FD_ISSET(current_fd, &g_write_fds) && g_clients[current_fd]) {
                handle_client_write(current_fd);
            }
        }
    }
    close(g_server_fd); 
    return 0; // Inalcançável
}