#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <stdio.h> // Apenas para sprintf

// Se FD_SETSIZE não for permitido como macro, substitua por um literal (ex: 1024)
// Se SOMAXCONN não for permitido, substitua por um literal (ex: 128)

// --- Estruturas ---
typedef struct s_client {
    int id;
    char *write_buf;      // Buffer para mensagens a serem ENVIADAS para este cliente
    char read_buf[40961]; // Buffer para mensagens RECEBIDAS deste cliente (40KB + null)
    int read_len;         // Quantidade de dados atualmente no read_buf
} t_client;

// --- Variáveis Globais ---
t_client *g_clients[FD_SETSIZE]; // Array de ponteiros para clientes, indexado por FD
char g_msg_buffer[42000];    // Buffer global para formatar mensagens antes de enfileirar (suficiente para msg longa + prefixo)

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

// strdup não está na lista, então implementamos:
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
    for (int i = 0; i <= g_max_fd; ++i) { // Iterar até g_max_fd é suficiente
        if (g_clients[i]) {
            if (g_clients[i]->write_buf) {
                free(g_clients[i]->write_buf);
            }
            free(g_clients[i]);
            g_clients[i] = NULL;
            if (i > 0 && i != g_server_fd) { // Não fechar stdin/out/err nem o server_fd de novo
                 close(i);
            }
        }
    }
    exit(1);
}

// Adiciona uma mensagem à fila de escrita de todos os clientes, exceto o remetente (ou -1 se for do servidor).
void broadcast_message(int sender_fd, const char *message) {
    for (int fd = 0; fd <= g_max_fd; ++fd) {
        if (g_clients[fd] && fd != sender_fd) { // Cliente existe e não é o remetente
            if (!g_clients[fd]->write_buf) {
                g_clients[fd]->write_buf = ft_strdup(message);
                if (!g_clients[fd]->write_buf) fatal_error();
            } else {
                char *new_buf = (char *)malloc(strlen(g_clients[fd]->write_buf) + strlen(message) + 1);
                if (!new_buf) fatal_error();
                strcpy(new_buf, g_clients[fd]->write_buf);
                strcat(new_buf, message);
                free(g_clients[fd]->write_buf);
                g_clients[fd]->write_buf = new_buf;
            }
        }
    }
}

void remove_client(int fd_to_remove) {
    if (fd_to_remove < 0 || fd_to_remove >= FD_SETSIZE || !g_clients[fd_to_remove]) return;

    // Notificar outros clientes sobre a saída
    sprintf(g_msg_buffer, "server: client %d just left\n", g_clients[fd_to_remove]->id);
    broadcast_message(fd_to_remove, g_msg_buffer); // Envia para todos MENOS o que está saindo

    // Limpar recursos do cliente
    if (g_clients[fd_to_remove]->write_buf) {
        free(g_clients[fd_to_remove]->write_buf);
    }
    free(g_clients[fd_to_remove]);
    g_clients[fd_to_remove] = NULL;

    close(fd_to_remove);
    FD_CLR(fd_to_remove, &g_active_fds);
    // FD_CLR(fd_to_remove, &g_write_fds); // select() cuida de não setar mais este fd

    // Recalcular g_max_fd se o FD removido era o maior
    if (fd_to_remove == g_max_fd) {
        g_max_fd = g_server_fd; // Começa pelo server_fd
        for (int i = g_server_fd + 1; i < fd_to_remove; ++i) {
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
    if (client_fd < 0) return; // Erro no accept, mas servidor continua

    if (client_fd >= FD_SETSIZE) {
        ft_putstr_fd("Server: Too many clients, connection rejected.\n", 1); // Informativo
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
    g_clients[client_fd]->read_buf[0] = '\0';

    FD_SET(client_fd, &g_active_fds);
    if (client_fd > g_max_fd) {
        g_max_fd = client_fd;
    }

    sprintf(g_msg_buffer, "server: client %d just arrived\n", g_clients[client_fd]->id);
    broadcast_message(client_fd, g_msg_buffer);
}

void handle_client_read(int fd) {
    t_client *client = g_clients[fd];
    int bytes_received;

    // Tenta ler o máximo possível no espaço restante do buffer
    int space_in_buf = sizeof(client->read_buf) - 1 - client->read_len;
    if (space_in_buf <= 0) { // Buffer de leitura cheio, e nenhuma linha foi processada
                              // Isso pode levar a um problema se o cliente só envia sem \n
                              // e enche o buffer. O enunciado não especifica como tratar.
                              // Vamos assumir que as linhas não são maiores que o buffer.
                              // Uma opção seria desconectar, mas não é pedido.
        return; // Não podemos ler mais até processar o que temos.
    }

    bytes_received = recv(fd, client->read_buf + client->read_len, space_in_buf, 0);

    if (bytes_received <= 0) { // Cliente desconectou (0) ou erro (<0)
        remove_client(fd);
        return;
    }

    client->read_len += bytes_received;
    client->read_buf[client->read_len] = '\0'; // Garante terminação nula

    // Processa todas as linhas completas no buffer
    char *current_line_start = client->read_buf;
    char *newline_char;

    while ((newline_char = strstr(current_line_start, "\n")) != NULL) {
        *newline_char = '\0'; // Termina a linha atual temporariamente

        sprintf(g_msg_buffer, "client %d: %s\n", client->id, current_line_start);
        broadcast_message(fd, g_msg_buffer); // Envia para todos MENOS o remetente original

        current_line_start = newline_char + 1; // Avança para o início da próxima linha potencial
    }

    // Move qualquer parte restante (linha incompleta) para o início do buffer
    if (current_line_start != client->read_buf && *current_line_start != '\0') {
        int remaining_len = strlen(current_line_start);
        memmove(client->read_buf, current_line_start, remaining_len);
        client->read_len = remaining_len;
        client->read_buf[client->read_len] = '\0';
    } else if (*current_line_start == '\0' && current_line_start > client->read_buf) {
        // Todas as linhas foram processadas, e current_line_start está no final do buffer processado
        client->read_len = 0;
        client->read_buf[0] = '\0';
    }
    // Se current_line_start == client->read_buf, nenhuma nova linha foi encontrada.
    // Os dados permanecem no buffer para a próxima leitura. read_len está correto.
}

void handle_client_write(int fd) {
    t_client *client = g_clients[fd];

    if (!client || !client->write_buf || client->write_buf[0] == '\0') {
        return; // Nada para enviar, ou cliente não existe mais
    }

    int to_send_len = strlen(client->write_buf);
    int bytes_sent = send(fd, client->write_buf, to_send_len, 0); // MSG_NOSIGNAL é implícito se não tratado SIGPIPE

    if (bytes_sent < 0) { // Erro no send (cliente provavelmente desconectou ou outro erro)
        remove_client(fd); // Assumimos que o erro é irrecuperável
        return;
    }
    
    // send pode retornar 0 se o peer fechou a conexão para escrita.
    // Ou se to_send_len era 0, mas já verificamos isso.
    if (bytes_sent == 0 && to_send_len > 0) { 
        remove_client(fd);
        return;
    }


    if (bytes_sent < to_send_len) { // Envio parcial
        // Move o restante não enviado para o início do buffer
        memmove(client->write_buf, client->write_buf + bytes_sent, to_send_len - bytes_sent + 1); // +1 para o '\0'
    } else { // Tudo foi enviado
        free(client->write_buf);
        client->write_buf = NULL; // Marca como vazio
    }
}


int main(int argc, char **argv) {
    if (argc != 2) {
        ft_putstr_fd("Wrong number of arguments\n", 2);
        exit(1);
    }

    int port = atoi(argv[1]);
    // Nenhuma validação explícita da porta é pedida além de falha de syscall

    // Inicializa o array de clientes
    for (int i = 0; i < FD_SETSIZE; ++i) {
        g_clients[i] = NULL;
    }

    g_server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (g_server_fd < 0) fatal_error();

    g_max_fd = g_server_fd;

    struct sockaddr_in servaddr;
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); // 127.0.0.1
    servaddr.sin_port = htons(port);

    // Permitir reuso do endereço (opcional, mas útil para desenvolvimento)
    // int yes = 1;
    // if (setsockopt(g_server_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) < 0) fatal_error();


    if (bind(g_server_fd, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) fatal_error();
    if (listen(g_server_fd, SOMAXCONN) < 0) fatal_error(); // Usar SOMAXCONN ou um literal como 128

    FD_ZERO(&g_active_fds);
    FD_SET(g_server_fd, &g_active_fds);

    while (1) {
        g_read_fds = g_active_fds;
        FD_ZERO(&g_write_fds); // Sempre zerar antes de popular

        // Adiciona FDs de clientes com dados na fila de escrita ao g_write_fds
        for (int fd = 0; fd <= g_max_fd; ++fd) {
            if (g_clients[fd] && g_clients[fd]->write_buf && g_clients[fd]->write_buf[0] != '\0') {
                FD_SET(fd, &g_write_fds);
            }
        }

        if (select(g_max_fd + 1, &g_read_fds, &g_write_fds, NULL, NULL) < 0) {
            // Ignorar EINTR (interrupção por sinal) ou tratar como fatal para este exercício
            // perror("select"); // Para debug
            fatal_error(); // Simplificação para o exercício
        }

        for (int current_fd = 0; current_fd <= g_max_fd; ++current_fd) {
            if (FD_ISSET(current_fd, &g_read_fds)) {
                if (current_fd == g_server_fd) {
                    add_new_client();
                } else {
                    if (g_clients[current_fd]) { // Checa se cliente ainda existe
                        handle_client_read(current_fd);
                    }
                }
            }
            // Verificar escrita depois da leitura, pois leitura pode gerar dados para escrita
            // e o cliente pode ter sido removido na leitura.
            if (FD_ISSET(current_fd, &g_write_fds)) {
                 if (g_clients[current_fd]) { // Checa se cliente ainda existe
                    handle_client_write(current_fd);
                }
            }
        }
    }
    // close(g_server_fd); // Inalcançável
    return 0; // Inalcançável
}