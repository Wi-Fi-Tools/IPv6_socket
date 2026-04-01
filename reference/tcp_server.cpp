//
// Created by 陈钊燚 on 2023/8/28.
//

#include "tcp_server.h"
#include "tcp_channel.h"
#include "owl/coroutine.h"

#ifdef __APPLE__
#include <ifaddrs.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <net/if.h>
#include <fcntl.h>
#endif

namespace zrpc {

TcpServerFactory::TcpServerFactory(uint16_t port, bool ipv6)
    : port_(port), ipv6_(ipv6) {
    zdebug_function(port, ipv6);
}

TcpServerFactory::~TcpServerFactory() {
    zdebug_function(port_);
    co_non_cancellable() {
        if (tcp_server_) {
            tcp_server_->close();
            tcp_server_ = nullptr;
        }
        if (custom_socket_fd_ != -1) {
            close(custom_socket_fd_);
            custom_socket_fd_ = -1;
        }
    };
}

std::shared_ptr<Channel> TcpServerFactory::Accept() {
    zdebug_function(port_);
    if (!tcp_server_) {
        zwarn("accept tcp_server_ null");
        return nullptr;
    }
    auto socket = tcp_server_->accept();
    if (!socket) {
        errno_ = errno;
        zwarn("accept fail: ")(errno_, strerror(errno_));
        return nullptr;
    }
    zdebug("accept success fd %_", socket->fd());
    return std::make_shared<TcpChannel>(socket);
}

bool TcpServerFactory::Listen() {
    zdebug_function(port_);
    if (tcp_server_) {
        zwarn("tcp_server_ not null, stop first");
        tcp_server_->close();
    }
    if (custom_socket_fd_ != -1) {
        close(custom_socket_fd_);
        custom_socket_fd_ = -1;
    }

#ifdef __APPLE__
    // 优先使用IPV6_BOUND_IF方式进行接口绑定（仅限apple和IPv6）
    if (!bind_interface_.empty() && ipv6_) {
        int fd = bind_to_interface_with_ipv6_bound_if();
        if (fd >= 0) {
            custom_socket_fd_ = fd;
            zinfo("Successfully bound socket to interface %_ using IPV6_BOUND_IF", bind_interface_);

            // 创建owl::tcp_server对象并接管我们创建的socket
            tcp_server_ = std::make_shared<owl::tcp_server>();

            // 使用take_over方法接管我们创建的socket
            owl::tcp_server::config conf;
            conf.bind_on = owl::kBindIpv6;

            auto err = tcp_server_->listen_fd(custom_socket_fd_, conf);
            if (err != 0) {
                errno_ = errno;
                zerror("tcp server listen error: ")(errno_, strerror(errno_));
                tcp_server_ = nullptr;
                return false;
            }
            zinfo("tcp server listen success %_", port_);
            return true;
        } else {
            errno_ = errno;
            zerror("bind to interface %_ fail", bind_interface_)(errno_, strerror(errno_));
            return false;
        }
    }
#endif

    // 标准监听流程
    std::string bind_addr = resolve_bind_address();
    bool bind_to_specific = !bind_addr.empty();

    // 创建owl::tcp_server对象
    tcp_server_ = std::make_shared<owl::tcp_server>();
    owl::tcp_server::config conf;
    conf.bind_on = ipv6_ ? owl::kBindIpv6 : owl::kBindIpv4;
    auto err = tcp_server_->listen(port_, conf);
    if (err != 0) {
        errno_ = errno;
        zerror("tcp server listen error: ")(errno_, strerror(errno_));
        tcp_server_ = nullptr;
        return false;
    }
    zinfo("tcp server listen success %_", port_);
    return true;
}

std::string TcpServerFactory::Ip() {
    if (tcp_server_) {
        return tcp_server_->local_address().ip();
    }
    return "";
}

uint16_t TcpServerFactory::Port() {
    if (tcp_server_) {
        return tcp_server_->local_address().port();
    }
    return 0;
}

void TcpServerFactory::set_bind_interface(const std::string& ifname) {
    zinfo_function(ifname);
    bind_interface_ = ifname;
}

int TcpServerFactory::bind_to_interface_with_ipv6_bound_if() {
#ifdef __APPLE__
    // 创建IPv6 socket
    int sockfd = socket(AF_INET6, SOCK_STREAM, 0);
    if (sockfd < 0) {
        zerror("socket creation failed: %_", strerror(errno));
        return -1;
    }

    // 设置socket选项
    int opt = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        zerror("setsockopt SO_REUSEADDR failed: %_", strerror(errno));
        close(sockfd);
        return -1;
    }

    // 设置SO_REUSEPORT（可选，但推荐用于服务器）
    #ifdef SO_REUSEPORT
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt)) < 0) {
        zerror("setsockopt SO_REUSEPORT failed: %_", strerror(errno));
        // 这不是致命错误，继续
    }
    #endif

    // 获取接口索引
    unsigned int if_index = if_nametoindex(bind_interface_.c_str());
    if (if_index == 0) {
        zerror("if_nametoindex failed: %_", strerror(errno));
        close(sockfd);
        return -1;
    }

    // 使用IPV6_BOUND_IF将socket绑定到指定接口
    if (setsockopt(sockfd, IPPROTO_IPV6, IPV6_BOUND_IF, &if_index, sizeof(if_index)) < 0) {
        zerror("setsockopt IPV6_BOUND_IF failed: %_", strerror(errno));
        close(sockfd);
        return -1;
    }

    // 绑定到IPv6通配地址
    struct sockaddr_in6 addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin6_family = AF_INET6;
    addr.sin6_addr = in6addr_any;
    addr.sin6_port = htons(port_);

    if (bind(sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        zerror("bind failed: %_", strerror(errno));
        close(sockfd);
        return -1;
    }

    zinfo("Successfully bound socket to interface %_ using IPV6_BOUND_IF (index: %_)", bind_interface_, if_index);
    return sockfd;
#else
    // 非Apple平台不支持IPV6_BOUND_IF
    zinfo("Current platform does not support IPV6_BOUND_IF");
    return -1;
#endif
}


// 将接口名解析为完整地址
std::string TcpServerFactory::resolve_bind_address() const {
    // 尝试把接口名解析为对应的 IPv6 link-local 地址
    if (!bind_interface_.empty()) {
#ifdef __APPLE__
        struct ifaddrs *ifaddr, *ifa;
        if (getifaddrs(&ifaddr) == -1) {
            zdebug("getifaddrs failed: %_", strerror(errno));
            return "";
        }

        char ip_str[INET6_ADDRSTRLEN];
        std::string result;

        for (ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
            if (ifa->ifa_addr == nullptr) continue;
            if (ifa->ifa_addr->sa_family != AF_INET6) continue;

            if (strcmp(ifa->ifa_name, bind_interface_.c_str()) == 0) {
                struct sockaddr_in6 *sa6 = reinterpret_cast<struct sockaddr_in6*>(ifa->ifa_addr);

                // 检查是否是link-local地址
                if (IN6_IS_ADDR_LINKLOCAL(&sa6->sin6_addr)) {
                    inet_ntop(AF_INET6, &sa6->sin6_addr, ip_str, sizeof(ip_str));
                    result = std::string(ip_str) + "%" + bind_interface_;
                    zinfo("Found link-local address for interface %_: %_", bind_interface_, result);
                    break;
                }
            }
        }

        freeifaddrs(ifaddr);
        return result;
#else
        // 非 Apple 平台直接返回空串
        zdebug("Interface %_ cannot be resolved on current platform", bind_interface_);
#endif
    }

    // 3) 均未设置，返回空串 → 默认所有接口
    return "";
}

} // namespace zrpc