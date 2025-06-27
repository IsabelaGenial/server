#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>

int     server_fd, max_fd, next_id = 0;
int     clients_id[60000];
char*   clients_msg[60000];
fd_set  active_fds,read_fds;
char    send_buffer[1024 * 42];
char    recv_buffer[1024 * 42];

void fatal_error () {
    write(2, "Wrong number of arguments\n", 26);
    close(server_fd);
    exit(1);
}

void send_to_all(int sender_fd){
    for(int fd = 0; fd <= max_fd; fd++){
        if(FD_ISSET(fd,&active_fds) && fd != sender_fd && fd != server_fd){
            if(send(fd, send_buffer, strlen(send_buffer), 0)<0){

            }
        }
    }
}

void accept_new_client (){
    struct sockaddr_in servaddr;
    socklen_t   len = sizeof(servaddr);
    int         client_fd;

    client_fd = accept(server_fd, (struct sockaddr *)&client_fd, &len);
    if(client_fd < 0)
        return; 

    if(client_fd > max_fd)
        max_fd = client_fd;

    clients_id[client_fd] = next_id++;
    clients_msg[client_fd] = NULL;

    FD_SET(client_fd, &active_fds);

    sprintf(send_buffer, "server: client %d just arrived\n", clients_id[client_fd]);
    send_to_all(client_fd);
    
}

void process_client_data(int fd){
    int bytes_read = recv(fd, recv_buffer, sizeof(recv_buffer) - 1, 0);

    if(bytes_read <= 0){
        sprintf(send_buffer, "server: client %d just left\n", clients_id[fd]);
        send_to_all(fd);

        if(clients_msg[fd]){
            free(clients_msg[fd]);
            clients_msg[fd] = NULL;
        }
        close(fd);
        FD_CLR(fd, &active_fds);
        return;
    }
    
    recv_buffer[bytes_read] = '\0';

    if(clients_msg[fd] == NULL){
        clients_msg[fd] = malloc(strlen(recv_buffer) + 1);
        if(clients_msg[fd] == NULL){fatal_error();}
        strcpy(clients_msg[fd], recv_buffer);
    }else{
        char *temp_ptr = realloc(clients_msg[fd], strlen(clients_msg[fd]) + strlen(recv_buffer) + 1);
        if(temp_ptr == NULL){fatal_error();}
        clients_msg[fd] = temp_ptr;
        strcat(clients_msg[fd], recv_buffer);
    }

    char *line_start = clients_msg[fd];
    char *newline_pos;
    while((newline_pos = strstr(line_start, "\n"))){
        *newline_pos = '\0';

        sprintf(send_buffer, "client %d: %s\n", clients_id[fd], line_start);
        send_to_all(fd);

        line_start = newline_pos + 1;
    }

    if(*line_start){
        char *remaining_msg = malloc(strlen(line_start) + 1);
        if(remaining_msg == NULL){fatal_error();}
        strcpy(remaining_msg, line_start);
        free(clients_msg[fd]);
    } else{
        free(clients_msg[fd]);
        clients_msg[fd] = NULL;
    }
}

int main(int argc, char **argv) {
    if(argc != 2)
    {
        write(2,"Wrong number of arguments\n", 26);
        exit(1);
    }
	
    
	// socket create and verification 
	server_fd = socket(AF_INET, SOCK_STREAM, 0); 
	if (server_fd < 0) { 
        fatal_error();
    } 
    
    FD_ZERO(&active_fds);
    bzero(clients_id, sizeof(clients_id));
    bzero(clients_msg, sizeof(clients_msg));
    
    FD_SET(server_fd, &active_fds);
    max_fd = server_fd;
    
    struct sockaddr_in servaddr;
	bzero(&servaddr, sizeof(servaddr)); 
	
    // assign IP, PORT 
	servaddr.sin_family = AF_INET; 
	servaddr.sin_addr.s_addr = htonl(2130706433); //127.0.0.1
	servaddr.sin_port = htons(atoi(argv[1])); 
  

	// Binding newly created socket to given IP and verification 
	if ((bind(server_fd, (const struct sockaddr *)&servaddr, sizeof(servaddr))) != 0) { 
		fatal_error();
	} 
	
	if (listen(server_fd, 128) < 0) {
		fatal_error();
	}

    while(369){
        read_fds = active_fds;

        if(select(max_fd + 1, &read_fds, NULL, NULL, NULL) < 0)
            continue;
        for (int fd = 0; fd <= max_fd; fd++){
            if(FD_ISSET(fd, &read_fds)){
                if(fd == server_fd){
                    accept_new_client();
                    break;
                }else{
                    process_client_data(fd);

                }
            }
        }

    }

    return 0;
}