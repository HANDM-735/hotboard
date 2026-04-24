#ifndef UDP_SERVER_H  
#define UDP_SERVER_H
  
#include <boost/asio.hpp>  
#include <boost/bind.hpp>  
#include <boost/array.hpp>  
#include <iostream>
#include <memory> 
#include <thread>  
#include <mutex>
#include <atomic> 
#include <condition_variable> 
#include <queue>  
#include <string>
#include <functional> 

#define LOCAL_PORT 9681
#define REMOTE_PORT 8880

struct ReceivedData {  
    std::string message;  
    std::string address;  
    unsigned short port;
    bool is_valid;
};

class UDPServer {  
public: 
    UDPServer(boost::asio::io_service& io_service, short port);  
    ~UDPServer();  
  
    void start();  
    void stop();
    void self_test();
    void send_data(const std::string& message, const std::string& ip, short port); 
    ReceivedData get_received_data(); 

private: 
    void handle_receive(const boost::system::error_code& ec, std::size_t bytes_transferred);
    void handle_send(const boost::system::error_code& ec, std::size_t bytes_transferred);
  
    boost::asio::io_service& io_service_;  
    boost::asio::ip::udp::socket socket_;  
    boost::asio::ip::udp::endpoint sender_endpoint_;
    boost::array<char, 1024> recv_buf;
    std::thread receive_thread_;  
    std::condition_variable cv_;  
    std::queue<ReceivedData> received_data_queue_;  
    std::mutex queue_mutex_;
    std::atomic<bool> stop_flag;
};
  
#endif // UDP_SERVER_H