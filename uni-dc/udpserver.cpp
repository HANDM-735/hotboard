#include <udpserver.h>

UDPServer::UDPServer(boost::asio::io_service& io_service, short port)
    : io_service_(io_service),
      socket_(io_service, boost::asio::ip::udp::endpoint(boost::asio::ip::udp::v4(), port)),
      stop_flag(false) {
}

UDPServer::~UDPServer() {
}

void UDPServer::start() {
    receive_thread_ = std::thread([this]() {
        socket_.async_receive_from(boost::asio::buffer(recv_buf), sender_endpoint_,
            std::bind(&UDPServer::handle_receive, this, std::placeholders::_1, std::placeholders::_2));
        io_service_.run(); // 运行io_service以处理异步操作
    });
}

void UDPServer::stop() {
    stop_flag = true;
    cv_.notify_one();
    io_service_.stop();

    if (receive_thread_.joinable()) {
        receive_thread_.join();
    }

    socket_.close();
}

void UDPServer::handle_receive(const boost::system::error_code& ec, std::size_t bytes_transferred) {
    std::cout << "handle_receive ++" << std::endl;
    if (!ec) {
        std::string address = sender_endpoint_.address().to_string();
        unsigned short port = sender_endpoint_.port();
        std::string message(recv_buf.data(), bytes_transferred);
        bool is_valid = true;

        {
            std::unique_lock<std::mutex> queue_lock(queue_mutex_);
            received_data_queue.push({message, address, port, is_valid});
        }
        cv_.notify_one();

        std::cout << "Remote address: " << address << " port: " << port << std::endl;

        // 继续异步接收
        socket_.async_receive_from(boost::asio::buffer(recv_buf), sender_endpoint_,
            std::bind(&UDPServer::handle_receive, this, std::placeholders::_1, std::placeholders::_2));
    } else if (ec != boost::asio::error::operation_aborted) {
        std::cerr << "Receive error: " << ec.message() << std::endl;
    }
}

#if 0
void UDPServer::do_receive() {
    try {
        boost::array<char, 1024> recv_buf;
        boost::system::error_code ec;
        while (true) {
            {
                std::lock_guard<std::mutex> lock(mtx_);
                if (!running) break;
            }

            size_t len = socket_.receive_from(boost::asio::buffer(recv_buf), sender_endpoint_, 0, ec);
            if (ec) {
                std::cerr << "Receive error: " << ec.message() << std::endl;
                break;
            }

            std::string address = sender_endpoint_.address().to_string();
            unsigned short port = sender_endpoint_.port();
            std::string message(recv_buf.data(), len);
            bool is_valid = true;

            {
                //std::lock_guard<std::mutex> queue_lock(queue_mutex_);
                std::unique_lock<std::mutex> queue_lock(queue_mutex_);
                received_data_queue.push({message, address, port, is_valid});
            }
            cv_.notify_one();

            std::cout << "Remote address: " << address << std::endl;
            std::cout << "Remote port: " << port << std::endl;
            std::cout << "Received: " << message << std::endl;
        }
    } catch (const std::exception& e) {
        std::cerr << "Exception in do_receive(): " << e.what() << std::endl;
    }
}
#endif

ReceivedData UDPServer::get_received_data() {
    std::cout << "get_received_data ++" << std::endl;
    std::unique_lock<std::mutex> queue_lock(queue_mutex_);
    cv_.wait(queue_lock, [this]() { return !received_data_queue.empty() || stop_flag; });

    if (stop_flag) {
        ReceivedData data;
        data.message = "";
        data.address = "0.0.0.0";
        data.port = 0;
        data.is_valid = false;
        return data;
    }

    ReceivedData data = received_data_queue.front();
    received_data_queue.pop();
    return data;
}

void UDPServer::handle_send(const boost::system::error_code& ec, std::size_t bytes_transferred) {
    if (!ec) {
        //std::cout << "Data sent successfully." << std::endl;
    } else {
        std::cerr << "Error sending data: " << ec.message() << std::endl;
    }
}

void UDPServer::send_data(const std::string& message, const std::string& ip, short port) {
    boost::asio::ip::udp::endpoint endpoint(boost::asio::ip::address::from_string(ip), port);
    socket_.async_send_to(boost::asio::buffer(message), endpoint, std::bind(&UDPServer::handle_send, this, std::placeholders::_1, std::placeholders::_2));
}

#if 0
void UDPServer::send_data(const std::string& message, const std::string& ip, short port) {
    boost::asio::ip::udp::endpoint endpoint(boost::asio::ip::address::from_string(ip), port);
    socket_.send_to(boost::asio::buffer(message), endpoint);
}
#endif

void UDPServer::self_test() {
    // send test data
    std::string test_message = "Hello, UDP Server!";
    std::string test_message1 = "Test, Test, Test!";
    std::string test_ip = "127.0.0.1";
    short test_port = socket_.local_endpoint().port();
    std::cout << "test_port: " << test_port << std::endl;

    boost::asio::io_service test_io_service;
    boost::asio::ip::udp::socket test_socket(test_io_service);
    test_socket.open(boost::asio::ip::udp::v4());
    test_socket.bind(boost::asio::ip::udp::endpoint(boost::asio::ip::udp::v4(), REMOTE_PORT));

    test_socket.send_to(boost::asio::buffer(test_message),
        boost::asio::ip::udp::endpoint(boost::asio::ip::address::from_string(test_ip), test_port));
    test_socket.send_to(boost::asio::buffer(test_message1),
        boost::asio::ip::udp::endpoint(boost::asio::ip::address::from_string(test_ip), test_port));

    // receive data
    ReceivedData received_data;
    {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        cv_.wait(lock, [this]() { return !received_data_queue.empty(); });
        received_data = received_data_queue.front();
        received_data_queue.pop();
    }

    // valid receive data
    std::string received_message = received_data.message;
    std::string received_address = received_data.address;

    if (test_message == received_message && test_ip == received_address) {
        std::cout << "Self-test passed!" << std::endl;
    } else {
        std::cout << "Self-test failed!" << std::endl;
        std::cout << "Expected message: " << test_message << std::endl;
        std::cout << "Received message: " << received_message << std::endl;
        std::cout << "Expected address: " << test_ip << std::endl;
        std::cout << "Received address: " << received_address << std::endl;
    }

    ReceivedData received_data1;
    {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        cv_.wait(lock, [this]() { return !received_data_queue.empty(); });
        received_data1 = received_data_queue.front();
        received_data_queue.pop();
    }

    // valid receive data
    std::string received_message1 = received_data1.message;
    std::string received_address1 = received_data1.address;

    if (test_message1 == received_message1 && test_ip == received_address1) {
        std::cout << "Self-test passed!" << std::endl;
    } else {
        std::cout << "Self-test failed!" << std::endl;
        std::cout << "Expected message: " << test_message1 << std::endl;
        std::cout << "Received message: " << received_message1 << std::endl;
        std::cout << "Expected address: " << test_ip << std::endl;
        std::cout << "Received address: " << received_address1 << std::endl;
    }
}