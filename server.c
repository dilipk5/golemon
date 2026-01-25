#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#define BUFFER_SIZE 1024
#define PORT 8080

int main(){
    int server_fd, client_fd;
    struct sockaddr_in address;
    int addrlen = sizeof(address);
    char buffer[BUFFER_SIZE] = {0};
    char *message = "Hello World from Server!";
    char command[1024];
    char output[4096];

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("Socket failed");
        exit(EXIT_FAILURE);
    }
    
    // Set socket options
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        perror("setsockopt failed");
        exit(EXIT_FAILURE);
    }

    
    address.sin_family = AF_INET;
    address.sin_port = htons(PORT);
    address.sin_addr.s_addr = INADDR_ANY;

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("Bind failed");
        exit(EXIT_FAILURE);
    }
    
    // Listen for connections
    if (listen(server_fd, 3) < 0) {
        perror("Listen failed");
        exit(EXIT_FAILURE);
    }
    
    printf("Server listening on port %d...\n", PORT);
    if ((client_fd = accept(server_fd, (struct sockaddr *)&address, 
                           (socklen_t*)&addrlen)) < 0) {
        perror("Accept failed");
        exit(EXIT_FAILURE);
    }
    
    // Read message from client
    read(client_fd, buffer, BUFFER_SIZE);
    printf("%s\n", buffer);
    
    // Send response to client
    while (1)
    {
        printf("Enter command to run: ");
        fgets(command,sizeof(command),stdin);
        send(client_fd, command, strlen(message), 0);
        read(client_fd, buffer, BUFFER_SIZE);
        printf("\n--------OUTPUT--------\n");
        printf("%s",buffer);
    }
    
    
    
    // Close connections
    close(client_fd);
    close(server_fd);
    
    return 0;

}