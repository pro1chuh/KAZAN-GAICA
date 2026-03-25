// socket_server.cpp
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include "socket_server.h"
#include <iostream>
#include <cstring>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
#else
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <fcntl.h>
#endif

BotConnection::BotConnection(socket_t sock, int bot_id) : socket(sock), bot_id(bot_id) {}

BotConnection::~BotConnection() {
    if (socket != INVALID_SOCKET && socket >= 0) {
        CLOSE_SOCKET(socket);
    }
}

bool BotConnection::send_message(const std::string& msg) {
    std::lock_guard<std::mutex> lock(socket_mutex);
    std::string msg_str = msg;
    if (!msg_str.empty() && msg_str.back() != '\n') {
        msg_str += "\n";
    }
    int sent = send(socket, msg_str.c_str(), (int)msg_str.size(), 0);
    if (sent <= 0) connected = false;
    return sent > 0;
}

std::string BotConnection::receive_message() {
    std::lock_guard<std::mutex> lock(socket_mutex);
    std::string line = read_line();
    if (line.empty()) connected = false;
    return line;
}

std::string BotConnection::read_line() {
    std::string line;
    char buffer[1] = {0};
    while (true) {
        int received = recv(socket, buffer, 1, 0);
        if (received <= 0) break;
        if (buffer[0] == '\n') break;
        if (buffer[0] != '\r') line += buffer[0];
    }
    return line;
}

SocketServer::SocketServer(int port) : port(port) {}

SocketServer::~SocketServer() { stop(); }

bool SocketServer::start() {
#ifdef _WIN32
    WSADATA wsa_data;
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) return false;
#endif
    if (!initialize_socket()) return false;
    running = true;
    return true;
}

void SocketServer::stop() {
    running = false;
    if (server_socket != INVALID_SOCKET && server_socket >= 0) {
        CLOSE_SOCKET(server_socket);
        server_socket = INVALID_SOCKET;
    }
#ifdef _WIN32
    WSACleanup();
#endif
}

bool SocketServer::initialize_socket() {
    server_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (INVALID_SOCKET_CHECK(server_socket)) return false;
    
    int opt = 1;
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));
    
    sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    server_addr.sin_port = htons(port);
    
    if (bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        CLOSE_SOCKET(server_socket);
        return false;
    }
    
    if (listen(server_socket, 2) < 0) {
        CLOSE_SOCKET(server_socket);
        return false;
    }
    
    return true;
}

std::shared_ptr<BotConnection> SocketServer::accept_connection(int timeout_seconds) {
    if (!running || server_socket == INVALID_SOCKET) return nullptr;
    
#ifdef _WIN32
    DWORD timeout = timeout_seconds * 1000;
    setsockopt(server_socket, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));
#else
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(server_socket, &readfds);
    struct timeval tv = {timeout_seconds, 0};
    if (select(server_socket + 1, &readfds, NULL, NULL, &tv) <= 0) return nullptr;
#endif
    
    sockaddr_in client_addr;
#ifdef _WIN32
    int addr_len = sizeof(client_addr);
#else
    socklen_t addr_len = sizeof(client_addr);
#endif
    socket_t client_socket = accept(server_socket, (struct sockaddr*)&client_addr, &addr_len);
    if (INVALID_SOCKET_CHECK(client_socket)) return nullptr;
    
    return std::make_shared<BotConnection>(client_socket, 0);
}