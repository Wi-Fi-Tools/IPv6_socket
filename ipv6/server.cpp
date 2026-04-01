#include <iostream>
#include <cstring>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netinet/in.h>
#include <net/if.h>

#define SERVER_PORT 50001
#define BUFFER_SIZE 2048

int main() {
    // 创建 socket
    int server_socket = socket(AF_INET6, SOCK_STREAM, 0);
    if (server_socket == -1) {
        std::cerr << "Socket creation failed." << std::endl;
        return -1;
    }

    // Bind socket to the specified interface (e.g. awdl0)
    // On Apple platforms, AWDL is a special P2P interface that requires explicit binding
    #ifdef __APPLE__
    std::cout << "bind to interface awdl0" << std::endl;
    std::string interface_name = "awdl0";
    unsigned int ifindex = if_nametoindex(interface_name.c_str());
    if (ifindex == 0) {
        perror("[TcpServer] if_nametoindex() failed");
        close(server_socket);
        return -1;
    }
    if (setsockopt(server_socket, IPPROTO_IPV6, IPV6_BOUND_IF, &ifindex, sizeof(ifindex)) < 0) {
        perror("[TcpServer] setsockopt(IPV6_BOUND_IF) failed");
        close(server_socket);
        return -1;
    }
    printf("[TcpServer] Bound to interface %s (index %u)\n", interface_name.c_str(), ifindex);
    #endif

    // 设置服务器地址结构体
    struct sockaddr_in6 server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin6_family = AF_INET6;
    server_addr.sin6_addr = in6addr_any; // 本地任意 IPv6 地址
    server_addr.sin6_port = htons(SERVER_PORT); // 端口号

    // 绑定 socket
    if (bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        std::cerr << "Bind failed." << std::endl;
        return -1;
    }

    // 开始监听
    if (listen(server_socket, 1) < 0) {
        std::cerr << "Listen failed." << std::endl;
        return -1;
    }

    std::cout << "Server is listening on port " << SERVER_PORT << "..." << std::endl;

    // 接受客户端连接
    struct sockaddr_in6 client_addr;
    socklen_t client_len = sizeof(client_addr);
    int connect_socket = accept(server_socket, (struct sockaddr*)&client_addr, &client_len);
    if (connect_socket < 0) {
        std::cerr << "Accept failed." << std::endl;
        return -1;
    }

    std::cout << "Client connected." << std::endl;
    char client_ip_str[INET6_ADDRSTRLEN];
    inet_ntop(AF_INET6, &client_addr.sin6_addr, client_ip_str, sizeof(client_ip_str));
    std::cout << "Client IP: " << client_ip_str << std::endl;

    char recv_buffer[BUFFER_SIZE];
    char send_buffer[BUFFER_SIZE];

    while (true) {
        // 接收客户端消息
        int bytes_received = recv(connect_socket, recv_buffer, BUFFER_SIZE - 1, 0);
        if (bytes_received > 0) {
            recv_buffer[bytes_received] = '\0'; // 字符串结尾
            std::cout << "Client: " << recv_buffer << std::endl;
        } else if (bytes_received == 0) {
            std::cout << "Client disconnected." << std::endl;
            break;
        } else {
            std::cerr << "Receive failed." << std::endl;
            break;
        }

        // 输入回复消息
        std::cout << "You: ";
        std::cin.getline(send_buffer, BUFFER_SIZE);

        // 发送消息给客户端
        if (send(connect_socket, send_buffer, strlen(send_buffer), 0) < 0) {
            std::cerr << "Send failed." << std::endl;
            break;
        }
    }

    // 关闭连接
    close(connect_socket);
    close(server_socket);

    return 0;
}
