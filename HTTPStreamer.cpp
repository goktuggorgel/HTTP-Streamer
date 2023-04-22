#include "pch.h"
#include "HTTPStreamer.h"

#pragma comment(lib, "ws2_32.lib")

MjpegStreamer& MjpegStreamer::get_instance(int port, int frame_width, int frame_height, int jpeg_quality) {
    static MjpegStreamer instance(port, frame_width, frame_height, jpeg_quality);
    return instance;
}

MjpegStreamer::MjpegStreamer(int port, int frame_width, int frame_height, int jpeg_quality)
    : frame_width(frame_width), frame_height(frame_height), jpeg_quality(jpeg_quality), stop(false) {

    queue_monitor = gcnew Object();
    client_sockets_monitor = gcnew Object();
    server_socket = create_socket(port);
    queue_monitor = gcnew Object();
    client_sockets_mutex = gcnew Mutex();
    accept_thread = gcnew Thread(gcnew ParameterizedThreadStart(&MjpegStreamer::accept_loop));
    accept_thread->Start(IntPtr(this));
    encode_thread = gcnew Thread(gcnew ParameterizedThreadStart(&MjpegStreamer::encode_and_send_loop));
    encode_thread->Start(IntPtr(this));
}

MjpegStreamer::~MjpegStreamer() {
    stop = true;
    {
        msclr::lock lock(queue_monitor);
        Monitor::PulseAll(queue_monitor);
    }
    accept_thread->Join();
    encode_thread->Join();
    for (SOCKET client_socket : client_sockets) {
        closesocket(client_socket);
    }
    closesocket(server_socket);
    WSACleanup();
}

void MjpegStreamer::send_frame(const cv::Mat& frame) {
    cv::Mat resized_frame;
    cv::resize(frame, resized_frame, cv::Size(frame_width, frame_height));
    {
        msclr::lock lock(queue_monitor);
        if (frame_queue.size() > 10) { // Keep only the most recent 3 frames
            frame_queue.pop();
        }
        frame_queue.push(resized_frame);
        Monitor::Pulse(queue_monitor);
    }
}


SOCKET MjpegStreamer::create_socket(int port) {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSAStartup failed." << std::endl;
        exit(1);
    }

    SOCKET server_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server_socket == INVALID_SOCKET) {
        std::cerr << "Error creating server socket." << std::endl;
        WSACleanup();
        exit(1);
    }

    sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(server_socket, reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr)) == SOCKET_ERROR) {
        std::cerr << "Error binding server socket." << std::endl;
        closesocket(server_socket);
        WSACleanup();
        exit(1);
    }

    if (listen(server_socket, SOMAXCONN) == SOCKET_ERROR) {
        std::cerr << "Error listening on server socket." << std::endl;
        closesocket(server_socket);
        WSACleanup();
        exit(1);
    }

    return server_socket;
}

void MjpegStreamer::accept_loop(Object^ obj) {
    MjpegStreamer* instance = static_cast<MjpegStreamer*>(static_cast<IntPtr>(obj).ToPointer());
    while (!instance->stop) {
        SOCKET client_socket = accept(instance->server_socket, NULL, NULL);
        if (client_socket == INVALID_SOCKET) {
            std::cerr << "Error accepting client." << std::endl;
            continue;
        }

        // Check if the maximum number of clients has been reached
        {
            msclr::lock lock(instance->client_sockets_mutex);
            if (instance->client_sockets.size() >= 1) {
                std::cerr << "Max number of clients reached." << std::endl;
                closesocket(client_socket);
                continue;
            }

            // Add the client socket to the list
            instance->client_sockets.push_back(client_socket);
            std::cout << "New client connected: " << client_socket << std::endl;
        }

        // Send the HTTP header to the client
        std::string header = "HTTP/1.1 200 OK\r\n"
            "Content-Type: multipart/x-mixed-replace;boundary=frame\r\n"
            "Connection: close\r\n"
            "\r\n";
        send(client_socket, header.c_str(), header.length(), 0);
    }
}

void MjpegStreamer::encode_and_send_loop(Object^ obj) {
    MjpegStreamer* instance = static_cast<MjpegStreamer*>(static_cast<IntPtr>(obj).ToPointer());
    while (!instance->stop) {
        cv::Mat frame;
        {
            msclr::lock lock(instance->queue_monitor);
            while (instance->frame_queue.empty() && !instance->stop) {
                Monitor::Wait(instance->queue_monitor);
            }
            if (instance->stop) {
                break;
            }

            frame = instance->frame_queue.front();
            instance->frame_queue.pop();
        }

        {
            msclr::lock lock(instance->client_sockets_monitor);
            auto it = instance->client_sockets.begin();
            while (it != instance->client_sockets.end()) {
                SOCKET client_socket = *it;
                int result = instance->send_mjpeg_frame(client_socket, frame); // call send_mjpeg_frame on the instance pointer
                if (result == -1) { // Check if the connection is closed
                    closesocket(client_socket);
                    it = instance->client_sockets.erase(it); // Remove the closed connection
                }
                else {
                    ++it;
                }
            }
        }
    }
}


int MjpegStreamer::send_mjpeg_frame(SOCKET client_socket, const cv::Mat& frame) {
    std::string boundary = "--frame\r\n";
    std::string header = "Content-Type: image/jpeg\r\n"
        "Content-Length: ";
    std::string endline = "\r\n";
    std::vector<uchar> buffer;
    std::vector<int> params{ cv::IMWRITE_JPEG_QUALITY,95 };

    cv::imencode(".jpg", frame, buffer, params);
    header += std::to_string(buffer.size()) + endline + endline; // Add an extra endline after the Content-Length header

    send(client_socket, boundary.c_str(), boundary.length(), 0);
    send(client_socket, header.c_str(), header.length(), 0);
    send(client_socket, reinterpret_cast<const char*>(buffer.data()), buffer.size(), 0);
    send(client_socket, endline.c_str(), endline.length(), 0);
    int result = send(client_socket, endline.c_str(), endline.length(), 0);
    if (result == SOCKET_ERROR) {
        return -1;
    }
    return 0;
}


