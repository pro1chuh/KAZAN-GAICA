// web_server.cpp
#include "web_server.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <cstring>

#ifdef _WIN32
    #define CLOSE_SOCKET closesocket
#else
    #define CLOSE_SOCKET close
#endif

WebServer::WebServer(int port, GameSimulator* simulator) 
    : port(port), simulator(simulator), running(false), server_socket(-1) {}

WebServer::~WebServer() { stop(); }

bool WebServer::start() {
    if (running) return true;
    
#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSAStartup failed\n";
        return false;
    }
#endif
    
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
        std::cerr << "Failed to create socket\n";
        return false;
    }
    
    int opt = 1;
#ifdef _WIN32
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));
#else
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif
    
    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    
    if (bind(server_socket, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        std::cerr << "Failed to bind to port " << port << "\n";
        CLOSE_SOCKET(server_socket);
        return false;
    }
    
    if (listen(server_socket, 5) < 0) {
        std::cerr << "Failed to listen\n";
        CLOSE_SOCKET(server_socket);
        return false;
    }
    
    running = true;
    server_thread = std::thread(&WebServer::run, this);
    return true;
}

void WebServer::stop() {
    running = false;
    if (server_socket >= 0) {
        CLOSE_SOCKET(server_socket);
        server_socket = -1;
    }
    if (server_thread.joinable()) server_thread.join();
#ifdef _WIN32
    WSACleanup();
#endif
}

void WebServer::run() {
    while (running) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(server_socket, &readfds);
        struct timeval tv = {1, 0};
        
        int activity = select(server_socket + 1, &readfds, NULL, NULL, &tv);
        if (activity < 0) break;
        if (activity == 0) continue;
        
        sockaddr_in client_addr;
#ifdef _WIN32
        int addr_len = sizeof(client_addr);
#else
        socklen_t addr_len = sizeof(client_addr);
#endif
        int client_socket = (int)accept(server_socket, (struct sockaddr*)&client_addr, &addr_len);
        if (client_socket < 0) continue;
        
        handle_client(client_socket);
        CLOSE_SOCKET(client_socket);
    }
}

void WebServer::handle_client(int client_socket) {
    char buffer[4096];
    int bytes = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
    if (bytes <= 0) return;
    buffer[bytes] = '\0';
    
    std::string request(buffer);
    std::string method, path, version;
    std::istringstream iss(request);
    iss >> method >> path >> version;
    
    std::string response;
    std::string content_type;
    
    if (path == "/") path = "/index.html";
    
    if (path == "/api/state") {
        if (simulator) {
            response = simulator->get_state_json();
        } else {
            response = "{\"status\":\"waiting\",\"tick\":0}";
        }
        content_type = "application/json";
    } else {
        std::string file_path = "web" + path;
        std::ifstream file(file_path, std::ios::binary);
        if (file.good()) {
            std::stringstream ss;
            ss << file.rdbuf();
            response = ss.str();
            
            if (path.find(".html") != std::string::npos) content_type = "text/html";
            else if (path.find(".css") != std::string::npos) content_type = "text/css";
            else if (path.find(".js") != std::string::npos) content_type = "application/javascript";
            else if (path.find(".png") != std::string::npos) content_type = "image/png";
            else content_type = "text/plain";
        } else {
            response = "<html><body><h1>404 Not Found</h1></body></html>";
            content_type = "text/html";
        }
    }
    
    std::stringstream response_ss;
    response_ss << "HTTP/1.1 200 OK\r\n"
                << "Content-Type: " << content_type << "\r\n"
                << "Content-Length: " << response.length() << "\r\n"
                << "Connection: close\r\n"
                << "\r\n"
                << response;
    
    send(client_socket, response_ss.str().c_str(), (int)response_ss.str().length(), 0);
}

std::string WebServer::get_mime_type(const std::string& path) {
    if (path.find(".html") != std::string::npos) return "text/html";
    if (path.find(".css") != std::string::npos) return "text/css";
    if (path.find(".js") != std::string::npos) return "application/javascript";
    return "text/plain";
}