#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>

int server_fd,max_fd,next_id = 0;
int client_id[60000];
char *client_msg[60000];
fd_set active_fds,read_fds;
char send_buffer[1024 * 42];
char recv_buffer[1024 * 42];

void fatal_error(){
    write(2, "Fatal error\n", 12);
    close(server_fd);
    exit(1);
}

void send_to_all(int sender_fd){
    for(int fd = 0; fd <= max_fd; fd++){
        if(FD_ISSET(fd, &active_fds) && fd != sender_fd && fd != server_fd){
            if(send(fd, send_buffer, strlen(send_buffer), 0) < 0){
                //print_aqui_para_debug
            }
        }
    }
}

void accept_new_client(){
    struct sockaddr_in client_addr;
    socklen_t len = sizeof(client_addr);
    int client_fd;
    
    client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &len);
    if(client_fd < 0){
        return;
    }

    if(client_fd > max_fd)
        max_fd = client_fd;

    client_id[client_fd] = next_id++;
    client_msg[client_fd] = NULL;

    FD_SET(client_fd, &active_fds);

    sprintf(send_buffer, "server: client %d just arrived\n", client_id[client_fd]);
    send_to_all(client_fd);

}

void process(int fd){
    int bytes_recv = recv(fd, recv_buffer, sizeof(recv_buffer) - 1, 0);

    if(bytes_recv <= 0){
        sprintf(send_buffer,  "server: client %d just left\n", client_id[fd]);
        send_to_all(fd);

        if(client_msg[fd])
        {
            free(client_msg[fd]);
            client_msg[fd] = NULL;
        }
        close(fd);
        FD_CLR(fd, &active_fds);
        return;
    }

    recv_buffer[bytes_recv] = '\0';

    if(client_msg[fd] == NULL){
        client_msg[fd] = malloc (strlen(recv_buffer) + 1);
        if(!client_msg[fd])
            fatal_error();
        strcpy(client_msg[fd], recv_buffer);
    }else{
        char *temp = realloc(client_msg[fd], strlen(client_msg[fd]) + strlen(recv_buffer) + 1);
        if(!temp)
            fatal_error();
        client_msg[fd] = temp;
        strcat(client_msg[fd], recv_buffer);

    }

    char *line_start = client_msg[fd];
    char *newline_pos;
    while((newline_pos = strstr(line_start, "\n"))){
        *newline_pos = '\0';

        sprintf(send_buffer, "client %d: %s\n", client_id[fd], line_start);
        send_to_all(fd);

        line_start = newline_pos + 1;
    }

    if(*line_start){
        char *remaining_msg = malloc(strlen(line_start) + 1);
        if(remaining_msg == NULL) fatal_error();
        free(client_msg[fd]);
        client_msg[fd] = remaining_msg;
        strcpy(client_msg[fd], line_start);     
    }else{

        free(client_msg[fd]);
        client_msg[fd] = NULL;
    }



}

int main(int argc, char **argv) {
    if(argc != 2)
    {
        write(2, "Wrong number of arguments\n", 26);
        exit(1);
    }

	
    struct sockaddr_in servaddr; 
	// socket create and verification 
	server_fd = socket(AF_INET, SOCK_STREAM, 0); 
	if (server_fd == -1) { 
        fatal_error(); 
	} 

	FD_ZERO(&active_fds);
	bzero(&servaddr, sizeof(servaddr));
    bzero(client_id, sizeof(servaddr));
    bzero(client_msg, sizeof(client_msg));

    FD_SET(server_fd, &active_fds);
    max_fd = server_fd;

	// assign IP, PORT 
	servaddr.sin_family = AF_INET; 
	servaddr.sin_addr.s_addr = htonl(2130706433); //127.0.0.1
	servaddr.sin_port = htons(atoi(argv[1])); 
  
	// Binding newly created socket to given IP and verification 
	if ((bind(server_fd, (const struct sockaddr *)&servaddr, sizeof(servaddr))) < 0) { 
		fatal_error(); 
	} 
	
	if (listen(server_fd, 128) < 0) {
		fatal_error();
	}

    while(369){
        read_fds = active_fds;
        if(select(max_fd + 1, &read_fds, NULL, NULL, 0) < 0){
            continue;
        }

        for(int fd = 0; fd <= max_fd; fd++){
            if(FD_ISSET(fd, &read_fds)){
                if(fd == server_fd){
                    accept_new_client();
                    break;
                }else{
                    process(fd);
                }
            }
        }
    }

    return 0;
}