#include "pch.h"
#include <iostream>
#include <opencv2/opencv.hpp>
#include "HTTPStreamer.h"
#include <thread>

int main() {
    cv::VideoCapture cap(0); // Open default camera (index 0)

    if (!cap.isOpened()) {
        std::cerr << "Error opening video capture." << std::endl;
        return -1;
    }

    MjpegStreamer& streamer = MjpegStreamer::get_instance();
    cv::Mat frame;

    while (true) {
        cap >> frame; // Capture a frame from the camera

        if (frame.empty()) {
            std::cerr << "Error capturing frame." << std::endl;
            break;
        }

        streamer.send_frame(frame); // Send the captured frame to the MJPEG streamer
        //cv::waitKey(1);

        //cv::imshow("Camera", frame); // Display the frame in a window named "Camera"
        
        //std::this_thread::sleep_for(std::chrono::milliseconds(33)); // Limit the frame rate to about 30 FPS

        /*if (cv::waitKey(1) == 27) { // Break the loop if the 'ESC' key is pressed
            break;
        }*/
    }

    cap.release();
    cv::destroyAllWindows();

    return 0;
}
