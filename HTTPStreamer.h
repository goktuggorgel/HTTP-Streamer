#pragma once

#include <opencv2/opencv.hpp>
#include <WS2tcpip.h>
#include <queue>
#include <msclr\lock.h>
#include <vcclr.h>

using namespace System;
using namespace System::Threading;

class MjpegStreamer {
public:
    static MjpegStreamer& get_instance(int port = 8080, int frame_width = 640, int frame_height = 480, int jpeg_quality = 60);
    ~MjpegStreamer();
    void send_frame(const cv::Mat& frame);

    MjpegStreamer(int port, int frame_width, int frame_height, int jpeg_quality);
    MjpegStreamer(const MjpegStreamer&) = delete;
    MjpegStreamer& operator=(const MjpegStreamer&) = delete;

    SOCKET create_socket(int port);

    int send_mjpeg_frame(SOCKET client_socket, const cv::Mat& frame);

    static void accept_loop(Object^ obj);
    static void encode_and_send_loop(Object^ obj);

    SOCKET server_socket;
    std::vector<SOCKET> client_sockets;
    int frame_width;
    int frame_height;
    int jpeg_quality;
    bool stop;

    std::queue<cv::Mat> frame_queue;
    gcroot<Object^> queue_monitor;
    gcroot<System::Threading::Mutex^> client_sockets_mutex;
    gcroot<Thread^> accept_thread;
    gcroot<Thread^> encode_thread;
    gcroot<Object^> client_sockets_monitor;

};
