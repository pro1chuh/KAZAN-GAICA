#pragma once

#include "types.h"
#include "json_utils.h"
#include <string>
#include <thread>
#include <mutex>
#include <memory>
#include <map>

#ifdef _WIN32
    #include <winsock2.h>
    #pragma comment(lib, "ws2_32.lib")
    using socket_t = SOCKET;
    #define INVALID_SOCKET_CHECK(s) ((s) == INVALID_SOCKET)
    #define CLOSE_SOCKET(s) closesocket(s)
#else
    #include <netinet/in.h>
    #include <sys/socket.h>
    #include <unistd.h>
    using socket_t = int;
    #define INVALID_SOCKET_CHECK(s) ((s) < 0)
    #define CLOSE_SOCKET(s) close(s)
#endif

class BotConnection {
public:
    BotConnection(socket_t sock, int bot_id);
    ~BotConnection();
    
    bool send_message(const std::string& msg);
    std::string receive_message();
    bool is_connected() const { return connected; }
    int get_id() const { return bot_id; }
    
private:
    socket_t socket;
    int bot_id;
    bool connected = true;
    std::mutex socket_mutex;
    std::string read_line();
};

class SocketServer {
public:
    SocketServer(int port = 9999);
    ~SocketServer();
    
    bool start();
    void stop();
    std::shared_ptr<BotConnection> accept_connection(int timeout_seconds = 30);
    
private:
    int port;
    socket_t server_socket = INVALID_SOCKET;
    volatile bool running = false;
    bool initialize_socket();
};