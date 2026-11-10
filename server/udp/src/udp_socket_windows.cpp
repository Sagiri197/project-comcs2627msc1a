#include "udp_socket_windows.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <mstcpip.h>

#include <stdexcept>
#include <system_error>

constexpr std::size_t PACKET_SIZE = 65535;

namespace {

// SIO_UDP_CONNRESET, which not every SDK header exposes.
constexpr DWORD udp_connreset_ioctl = _WSAIOW(IOC_VENDOR, 12);

std::system_error socket_error(const char* what) {
    return std::system_error(WSAGetLastError(), std::system_category(), what);
}

void ensure_winsock() {
    struct Winsock {
        Winsock() {
            WSADATA data{};
            if (const int error = WSAStartup(MAKEWORD(2, 2), &data); error != 0)
                throw std::system_error(error, std::system_category(), "WSAStartup");
        }
        ~Winsock() { WSACleanup(); }
    };
    static const Winsock winsock;
}

sockaddr_in socket_address(const UdpEndpoint& endpoint) {

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(endpoint.port);

    if (inet_pton(AF_INET, endpoint.address.c_str(), &address.sin_addr) != 1)
        throw std::invalid_argument("Invalid IPv4 address: " + endpoint.address);

    return address;
}

UdpEndpoint endpoint_from(const sockaddr_in& address) {

    char buffer[INET_ADDRSTRLEN]{};
    if (inet_ntop(AF_INET, &address.sin_addr, buffer, sizeof(buffer)) == nullptr)
        throw socket_error("inet_ntop");

    return {buffer, ntohs(address.sin_port)};
}
}

WindowsUdpSocket::WindowsUdpSocket(const UdpEndpoint& endpoint) : m_socket(INVALID_SOCKET) {

    ensure_winsock();
    const auto address = socket_address(endpoint);
    const SOCKET socket_handle = socket(AF_INET, SOCK_DGRAM, 0);
    if (socket_handle == INVALID_SOCKET)
        throw socket_error("socket");

    // Otherwise an ICMP "port unreachable" for an earlier sendto (e.g. an ACK to a node that
    // has since rebooted) makes the next recvfrom fail with WSAECONNRESET.
    BOOL report_reset = FALSE;
    DWORD returned = 0;
    WSAIoctl(socket_handle, udp_connreset_ioctl, &report_reset, sizeof(report_reset),
             nullptr, 0, &returned, nullptr, nullptr);

    if (bind(socket_handle, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR) {
        const auto error = socket_error("bind");
        closesocket(socket_handle);
        throw error;
    }
    m_socket = socket_handle;
}

WindowsUdpSocket::~WindowsUdpSocket() {
    closesocket(static_cast<SOCKET>(m_socket));
}

void WindowsUdpSocket::send(std::string_view data, const UdpEndpoint& destination) const {

    const auto address = socket_address(destination);
    if (sendto(static_cast<SOCKET>(m_socket), data.data(), static_cast<int>(data.size()), 0,
               reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR) {
        throw socket_error("sendto");
    }
}

UdpDatagram WindowsUdpSocket::recv() const {

    UdpDatagram datagram{std::string(PACKET_SIZE, 0), {}};
    sockaddr_in sender{};
    int sender_size = sizeof(sender);

    const int received = recvfrom(static_cast<SOCKET>(m_socket), datagram.data.data(),
                                  static_cast<int>(datagram.data.size()), 0,
                                  reinterpret_cast<sockaddr*>(&sender), &sender_size);

    if (received == SOCKET_ERROR)
        throw socket_error("recvfrom");

    // Resize received data to the actual number of bytes received
    datagram.data.resize(static_cast<std::size_t>(received));
    datagram.sender = endpoint_from(sender);

    return datagram;
}

std::unique_ptr<IUdpSocket> make_udp_socket(const UdpEndpoint& endpoint) {
    return std::make_unique<WindowsUdpSocket>(endpoint);
}
