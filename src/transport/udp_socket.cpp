#include "veil/transport/udp_socket.hpp"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

// Platform-specific includes for epoll/sendmmsg/recvmmsg
#ifdef __linux__
#include <sys/epoll.h>
#define HAVE_EPOLL 1
#define HAVE_SENDMMSG 1
#define HAVE_RECVMMSG 1
#else
#include <poll.h>
#define HAVE_EPOLL 0
#define HAVE_SENDMMSG 0
#define HAVE_RECVMMSG 0
#endif

namespace veil::transport {

UdpSocket::~UdpSocket() {
    close();
}

UdpSocket::UdpSocket(UdpSocket&& other) noexcept
    : fd_(other.fd_),
      epoll_fd_(other.epoll_fd_),
      local_addr_(std::move(other.local_addr_)),
      config_(other.config_),
      recv_callback_(std::move(other.recv_callback_)),
      error_callback_(std::move(other.error_callback_)),
      packets_sent_(other.packets_sent_),
      packets_received_(other.packets_received_),
      bytes_sent_(other.bytes_sent_),
      bytes_received_(other.bytes_received_),
      send_errors_(other.send_errors_),
      recv_errors_(other.recv_errors_) {
    other.fd_ = -1;
    other.epoll_fd_ = -1;
}

UdpSocket& UdpSocket::operator=(UdpSocket&& other) noexcept {
    if (this != &other) {
        close();
        fd_ = other.fd_;
        epoll_fd_ = other.epoll_fd_;
        local_addr_ = std::move(other.local_addr_);
        config_ = other.config_;
        recv_callback_ = std::move(other.recv_callback_);
        error_callback_ = std::move(other.error_callback_);
        packets_sent_ = other.packets_sent_;
        packets_received_ = other.packets_received_;
        bytes_sent_ = other.bytes_sent_;
        bytes_received_ = other.bytes_received_;
        send_errors_ = other.send_errors_;
        recv_errors_ = other.recv_errors_;
        other.fd_ = -1;
        other.epoll_fd_ = -1;
    }
    return *this;
}

bool UdpSocket::open(const UdpSocketConfig& config) {
    config_ = config;

    // Create socket
    fd_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd_ < 0) {
        handle_error(errno, "socket()");
        return false;
    }

    // Set socket options
    int optval = 1;

    if (config.reuse_port) {
#ifdef SO_REUSEPORT
        if (setsockopt(fd_, SOL_SOCKET, SO_REUSEPORT, &optval, sizeof(optval)) < 0) {
            handle_error(errno, "setsockopt(SO_REUSEPORT)");
        }
#endif
        if (setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) < 0) {
            handle_error(errno, "setsockopt(SO_REUSEADDR)");
        }
    }

    // Set buffer sizes
    int recv_buf = static_cast<int>(config.recv_buffer_size);
    int send_buf = static_cast<int>(config.send_buffer_size);
    setsockopt(fd_, SOL_SOCKET, SO_RCVBUF, &recv_buf, sizeof(recv_buf));
    setsockopt(fd_, SOL_SOCKET, SO_SNDBUF, &send_buf, sizeof(send_buf));

    // Set non-blocking
    if (config.nonblocking) {
        int flags = fcntl(fd_, F_GETFL, 0);
        if (flags < 0 || fcntl(fd_, F_SETFL, flags | O_NONBLOCK) < 0) {
            handle_error(errno, "fcntl(O_NONBLOCK)");
            close();
            return false;
        }
    }

    // Bind to address
    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(config.bind_address.port);

    if (config.bind_address.host.empty() || config.bind_address.host == "0.0.0.0") {
        addr.sin_addr.s_addr = INADDR_ANY;
    } else {
        if (inet_pton(AF_INET, config.bind_address.host.c_str(), &addr.sin_addr) <= 0) {
            // Try hostname resolution
            struct hostent* he = gethostbyname(config.bind_address.host.c_str());
            if (!he) {
                handle_error(errno, "gethostbyname()");
                close();
                return false;
            }
            std::memcpy(&addr.sin_addr, he->h_addr, he->h_length);
        }
    }

    if (bind(fd_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        handle_error(errno, "bind()");
        close();
        return false;
    }

    // Get actual bound address
    socklen_t addr_len = sizeof(addr);
    if (getsockname(fd_, reinterpret_cast<struct sockaddr*>(&addr), &addr_len) == 0) {
        char ip_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &addr.sin_addr, ip_str, sizeof(ip_str));
        local_addr_.host = ip_str;
        local_addr_.port = ntohs(addr.sin_port);
    }

#if HAVE_EPOLL
    // Create epoll instance
    epoll_fd_ = epoll_create1(0);
    if (epoll_fd_ < 0) {
        handle_error(errno, "epoll_create1()");
        close();
        return false;
    }

    struct epoll_event ev{};
    ev.events = EPOLLIN;
    ev.data.fd = fd_;
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd_, &ev) < 0) {
        handle_error(errno, "epoll_ctl()");
        close();
        return false;
    }
#endif

    return true;
}

void UdpSocket::close() {
#if HAVE_EPOLL
    if (epoll_fd_ >= 0) {
        ::close(epoll_fd_);
        epoll_fd_ = -1;
    }
#endif

    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

void UdpSocket::set_recv_callback(RecvCallback callback) {
    recv_callback_ = std::move(callback);
}

void UdpSocket::set_error_callback(ErrorCallback callback) {
    error_callback_ = std::move(callback);
}

bool UdpSocket::send_to(const SocketAddress& to, std::span<const uint8_t> data) {
    if (fd_ < 0) {
        return false;
    }

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(to.port);

    if (inet_pton(AF_INET, to.host.c_str(), &addr.sin_addr) <= 0) {
        struct hostent* he = gethostbyname(to.host.c_str());
        if (!he) {
            ++send_errors_;
            return false;
        }
        std::memcpy(&addr.sin_addr, he->h_addr, he->h_length);
    }

    ssize_t sent = sendto(fd_, data.data(), data.size(), 0,
                          reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));

    if (sent < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            ++send_errors_;
            handle_error(errno, "sendto()");
        }
        return false;
    }

    ++packets_sent_;
    bytes_sent_ += sent;
    return true;
}

size_t UdpSocket::send_many(const std::vector<std::pair<SocketAddress, std::vector<uint8_t>>>& packets) {
#if HAVE_SENDMMSG
    if (packets.empty()) {
        return 0;
    }

    std::vector<struct mmsghdr> msgs(packets.size());
    std::vector<struct iovec> iovecs(packets.size());
    std::vector<struct sockaddr_in> addrs(packets.size());

    for (size_t i = 0; i < packets.size(); ++i) {
        auto& addr = addrs[i];
        addr.sin_family = AF_INET;
        addr.sin_port = htons(packets[i].first.port);
        inet_pton(AF_INET, packets[i].first.host.c_str(), &addr.sin_addr);

        iovecs[i].iov_base = const_cast<uint8_t*>(packets[i].second.data());
        iovecs[i].iov_len = packets[i].second.size();

        msgs[i].msg_hdr.msg_name = &addrs[i];
        msgs[i].msg_hdr.msg_namelen = sizeof(addrs[i]);
        msgs[i].msg_hdr.msg_iov = &iovecs[i];
        msgs[i].msg_hdr.msg_iovlen = 1;
        msgs[i].msg_hdr.msg_control = nullptr;
        msgs[i].msg_hdr.msg_controllen = 0;
        msgs[i].msg_hdr.msg_flags = 0;
    }

    int sent = sendmmsg(fd_, msgs.data(), packets.size(), 0);
    if (sent < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            ++send_errors_;
            handle_error(errno, "sendmmsg()");
        }
        return 0;
    }

    for (int i = 0; i < sent; ++i) {
        ++packets_sent_;
        bytes_sent_ += msgs[i].msg_len;
    }
    return static_cast<size_t>(sent);
#else
    // Fallback to individual sends
    size_t sent = 0;
    for (const auto& [addr, data] : packets) {
        if (send_to(addr, data)) {
            ++sent;
        }
    }
    return sent;
#endif
}

std::optional<ReceivedPacket> UdpSocket::recv() {
    if (fd_ < 0) {
        return std::nullopt;
    }

    std::vector<uint8_t> buffer(65536);
    struct sockaddr_in from_addr{};
    socklen_t from_len = sizeof(from_addr);

    ssize_t received = recvfrom(fd_, buffer.data(), buffer.size(), 0,
                                 reinterpret_cast<struct sockaddr*>(&from_addr), &from_len);

    if (received < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            ++recv_errors_;
            handle_error(errno, "recvfrom()");
        }
        return std::nullopt;
    }

    ++packets_received_;
    bytes_received_ += received;

    ReceivedPacket packet;
    char ip_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &from_addr.sin_addr, ip_str, sizeof(ip_str));
    packet.from.host = ip_str;
    packet.from.port = ntohs(from_addr.sin_port);
    packet.data.assign(buffer.begin(), buffer.begin() + received);

    return packet;
}

std::vector<ReceivedPacket> UdpSocket::recv_many(size_t max_packets) {
#if HAVE_RECVMMSG
    std::vector<struct mmsghdr> msgs(max_packets);
    std::vector<struct iovec> iovecs(max_packets);
    std::vector<struct sockaddr_in> addrs(max_packets);
    std::vector<std::vector<uint8_t>> buffers(max_packets);

    for (size_t i = 0; i < max_packets; ++i) {
        buffers[i].resize(65536);
        iovecs[i].iov_base = buffers[i].data();
        iovecs[i].iov_len = buffers[i].size();

        msgs[i].msg_hdr.msg_name = &addrs[i];
        msgs[i].msg_hdr.msg_namelen = sizeof(addrs[i]);
        msgs[i].msg_hdr.msg_iov = &iovecs[i];
        msgs[i].msg_hdr.msg_iovlen = 1;
        msgs[i].msg_hdr.msg_control = nullptr;
        msgs[i].msg_hdr.msg_controllen = 0;
        msgs[i].msg_hdr.msg_flags = 0;
    }

    int received = recvmmsg(fd_, msgs.data(), max_packets, MSG_DONTWAIT, nullptr);
    if (received < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            ++recv_errors_;
            handle_error(errno, "recvmmsg()");
        }
        return {};
    }

    std::vector<ReceivedPacket> packets;
    packets.reserve(received);

    for (int i = 0; i < received; ++i) {
        ++packets_received_;
        bytes_received_ += msgs[i].msg_len;

        ReceivedPacket pkt;
        char ip_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &addrs[i].sin_addr, ip_str, sizeof(ip_str));
        pkt.from.host = ip_str;
        pkt.from.port = ntohs(addrs[i].sin_port);
        pkt.data.assign(buffers[i].begin(), buffers[i].begin() + msgs[i].msg_len);
        packets.push_back(std::move(pkt));
    }

    return packets;
#else
    // Fallback to individual receives
    std::vector<ReceivedPacket> packets;
    for (size_t i = 0; i < max_packets; ++i) {
        auto pkt = recv();
        if (!pkt) {
            break;
        }
        packets.push_back(std::move(*pkt));
    }
    return packets;
#endif
}

int UdpSocket::poll_recv(int timeout_ms) {
#if HAVE_EPOLL
    struct epoll_event events[16];
    int nfds = epoll_wait(epoll_fd_, events, 16, timeout_ms);
    return nfds;
#else
    struct pollfd pfd{};
    pfd.fd = fd_;
    pfd.events = POLLIN;
    return poll(&pfd, 1, timeout_ms);
#endif
}

void UdpSocket::run_once(int timeout_ms) {
    int ready = poll_recv(timeout_ms);
    if (ready <= 0) {
        return;
    }

    auto packets = recv_many(64);
    for (auto& pkt : packets) {
        if (recv_callback_) {
            recv_callback_(std::move(pkt));
        }
    }
}

void UdpSocket::handle_error(int error_code, const char* context) {
    if (error_callback_) {
        std::string msg = std::string(context) + ": " + std::strerror(error_code);
        error_callback_(error_code, msg);
    }
}

}  // namespace veil::transport
