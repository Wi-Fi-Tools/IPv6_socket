#include <iostream>
#include <cstring>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netdb.h>
#include <net/if.h>

#define SERVER_PORT 50001
#define BUFFER_SIZE 2048

int main() {
    const char* server_ip = "fe80::3881:79ff:fe1f:544f"; // awdl ipv6

    // 创建 socket
    int client_socket = socket(AF_INET6, SOCK_STREAM, 0);
    if (client_socket == -1) {
        std::cerr << "Socket creation failed." << std::endl;
        return -1;
    }

    // 设置服务器地址结构体
    struct sockaddr_in6 server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin6_family = AF_INET6;
    server_addr.sin6_port = htons(SERVER_PORT);

    if (inet_pton(AF_INET6, server_ip, &server_addr.sin6_addr) <= 0) {
        std::cerr << "Invalid address/ Address not supported" << std::endl;
        return -1;
    }

    // 链路本地地址（fe80::）需要指定 scope_id（接口索引），否则系统无法确定从哪个接口发送
    server_addr.sin6_scope_id = if_nametoindex("awdl0");

    // 连接服务器
    if (connect(client_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        std::cerr << "Connection Failed" << std::endl;
        return -1;
    }

    std::cout << "Connected to server at [" << server_ip << "]:" << SERVER_PORT << std::endl;

    char send_buffer[BUFFER_SIZE];
    char recv_buffer[BUFFER_SIZE];

    while (true) {
        std::cout << "You: ";
        std::cin.getline(send_buffer, BUFFER_SIZE);

        // 发送消息给服务器
        if (send(client_socket, send_buffer, strlen(send_buffer), 0) < 0) {
            std::cerr << "Send failed" << std::endl;
            break;
        }

        // 接收服务器回复
        int bytes_received = recv(client_socket, recv_buffer, BUFFER_SIZE - 1, 0);
        if (bytes_received > 0) {
            recv_buffer[bytes_received] = '\0'; // 添加字符串结束符
            std::cout << "Server: " << recv_buffer << std::endl;
        } else if (bytes_received == 0) {
            std::cout << "Server disconnected." << std::endl;
            break;
        } else {
            std::cerr << "Receive failed" << std::endl;
            break;
        }
    }

    // 关闭连接
    close(client_socket);

    return 0;
}
