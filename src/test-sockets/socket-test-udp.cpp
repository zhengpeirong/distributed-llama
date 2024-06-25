#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

int main() {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("Socket creation error");
        return 1;
    }

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(8081);
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    const char *message = "Hello, Server";
    sendto(sock, message, strlen(message), 0, (struct sockaddr *)&server_addr, sizeof(server_addr));
    
    char buffer[1024] = {0};
    socklen_t addr_len = sizeof(server_addr);
    recvfrom(sock, buffer, 1024, 0, (struct sockaddr *)&server_addr, &addr_len);
    printf("Server: %s\n", buffer);

    close(sock);
    return 0;
}