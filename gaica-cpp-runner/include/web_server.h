#pragma once

#include "types.h"
#include "game_simulator.h"
#include <string>
#include <thread>
#include <atomic>

#ifdef _WIN32
    #include <winsock2.h>
    #pragma comment(lib, "ws2_32.lib")
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <unistd.h>
#endif

class WebServer {
public:
    WebServer(int port, GameSimulator* simulator);
    ~WebServer();
    
    bool start();
    void stop();
    bool is_running() const { return running; }
    
private:
    int port;
    GameSimulator* simulator;
    std::atomic<bool> running;
    std::thread server_thread;
    
#ifdef _WIN32
    SOCKET server_socket;
#else
    int server_socket;
#endif
    
    void run();
    void handle_client(int client_socket);
    std::string get_mime_type(const std::string& path);
    std::string get_state_json();
};