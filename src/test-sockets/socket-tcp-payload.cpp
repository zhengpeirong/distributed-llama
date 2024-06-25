#include <iostream>
#include <cstring>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>

// 定义SwitchML头部数据结构
struct SwitchMLHeader {
    uint32_t field1;
    uint32_t field2;
    // 添加其他必要的字段
};

int main() {
    // 创建套接字
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == -1) {
        perror("Failed to create socket");
        return 1;
    }

    // 定义服务器地址
    struct sockaddr_in server;
    server.sin_family = AF_INET;
    server.sin_port = htons(8082); // 目标端口
    server.sin_addr.s_addr = inet_addr("127.0.0.1"); // 目标IP地址

    // 连接服务器
    if (connect(sock, (struct sockaddr *)&server, sizeof(server)) < 0) {
        perror("Connection failed");
        close(sock);
        return 1;
    }

    // 创建SwitchML头部
    SwitchMLHeader switchMLHeader;
    switchMLHeader.field1 = htonl(12345); // 示例数据
    switchMLHeader.field2 = htonl(67890); // 示例数据

    // 创建有效载荷
    const char payload[] = "This is the payload data";

    // 计算总数据长度
    size_t total_length = sizeof(SwitchMLHeader) + sizeof(payload);

    // 分配缓冲区
    char *buffer = new char[total_length];
    memset(buffer, 0, total_length);

    // 复制SwitchML头部和有效载荷到缓冲区
    memcpy(buffer, &switchMLHeader, sizeof(SwitchMLHeader));
    memcpy(buffer + sizeof(SwitchMLHeader), payload, sizeof(payload));

    // 发送数据
    if (send(sock, buffer, total_length, 0) < 0) {
        perror("Send failed");
    } else {
        std::cout << "Data sent successfully!" << std::endl;
    }

    // 释放资源
    delete[] buffer;
    close(sock);

    return 0;
}