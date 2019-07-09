
#include "ftdi_functions.h"

#if defined(_WIN32) | defined(__WIN32__) | defined(__WIN32) | defined(_WIN64) | defined(__WIN64)
#define _CRT_SECURE_NO_WARNINGS					

#ifndef _WIN32_WINNT		// Allow use of features specific to Windows XP or later.                   
#define _WIN32_WINNT 0x0501	// Change this to the appropriate value to target other versions of Windows.
#endif		

#endif

// C++ Includes
#include <cstdio>
#include <cstdint>
#include <ctime>
#include <map>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <fstream>
#include <string>
#include <vector>
#include <chrono>
#include <tuple>

// OPENCV Includes
#include <opencv2/core/core.hpp>           
#include <opencv2/highgui/highgui.hpp>     
#include <opencv2/imgproc/imgproc.hpp>  

// Point Grey Includes
#include "FlyCapture2.h"
#include "chameleon_utilities.h"
//#include "image_capture.h"
namespace FC2 = FlyCapture2;

// Lens Driver Includes
#include "lens_driver.h"

// Custom Includes
#include "dfd_dnn_cam.h"
#include "num2string.h"
#include "get_current_time.h"
#include "file_parser.h"
//#include "make_dir.h"

// net definitions
#include "dfd_net_v14.h"

// dlib includes
#include <dlib/dnn.h>
#include <dlib/image_io.h>
#include <dlib/data_io.h>
//#include <dlib/gui_widgets.h>
#include <dlib/image_transforms.h>
#include <dlib/opencv.h>

// ----------------------------------------------------------------------------------------

FC2::Error get_image(FC2::Camera &cam, FT_HANDLE &lens_driver_handle, lens_packet_struct &lens_packet, cv::Mat &image)
{
    FC2::Error error;
    FC2::Image rawImage, convertedImageCV;
    uint32_t image_rows, image_cols;
    unsigned char *image_data = NULL;
    lens_driver ld;
    
    ld.send_lens_packet(lens_packet, lens_driver_handle);
    sleep_ms(20);
    
    // wait for the camera to be ready for a software trigger
    poll_trigger_ready(cam);

    error = fire_software_trigger(cam);
    if (error != FC2::PGRERROR_OK)
    {
        print_error(error);
        std::cout << "Error firing software trigger" << std::endl;
    }
    // get the images from the camera
    
    error = cam.RetrieveBuffer(&rawImage);
    if (error != FC2::PGRERROR_OK)
    {
        return error;
    }

    image_cols = rawImage.GetCols();
    image_rows = rawImage.GetRows();
    //rawImage.GetDimensions(&, &, &image_stride);    // , &pixFormat, &btFormat);
    error = rawImage.Convert(FC2::PIXEL_FORMAT_BGR, &convertedImageCV);
    //image_data = convertedImageCV.GetData();
    //image_data = rawImage.GetData();

    cv::Mat tmp_img = cv::Mat(cv::Size(image_cols, image_rows), CV_8UC3, convertedImageCV.GetData(), image_cols * 3);
    image = tmp_img.clone();    // do this because the pointer goes out of scope when the function exits

    return error;
}

// ----------------------------------------------------------------------------------------

template <typename net_type>
void get_depth_map(net_type &dfd_net, cv::Mat &focus_image, cv::Mat &defocus_image, dlib::matrix<uint16_t> &dm)
{
    uint32_t idx, jdx;
    

    // resize the require dnn input image to the same size as the captured images
    std::array<dlib::matrix<uint16_t>, img_depth> input_img;
    // for (idx = 0; idx < img_depth; ++idx)
    // {
        // input_img[idx].set_size(focus_image.rows, focus_image.cols);
    // }
    
    // split the opencv images into its channels
    std::vector<cv::Mat> f, d;
    cv::split(focus_image, f);
    cv::split(defocus_image, d);
    
    dlib::cv_image<uint16_t> input_img[0](f[2]);
    dlib::cv_image<uint16_t> input_img[1](f[1]);
    dlib::cv_image<uint16_t> input_img[2](f[0]);
    dlib::cv_image<uint16_t> input_img[3](d[2]);
    dlib::cv_image<uint16_t> input_img[4](d[1]);
    dlib::cv_image<uint16_t> input_img[5](d[0]);    
    

    // loop through and place each pixel in the correct location - opencv -> BGR pixel
    // for (idx = 0; idx<focus_image.rows; ++idx)
    // {
        // for (jdx = 0; jdx < focus_image.cols; ++jdx)
        // {
            // cv::Vec3w f1 = focus_image.at<cv::Vec3b>(idx, jdx);
            // dlib::assign_pixel(input_img[0](idx, jdx), f1[2]);
            // dlib::assign_pixel(input_img[1](idx, jdx), f1[1]);
            // dlib::assign_pixel(input_img[2](idx, jdx), f1[0]);
            // cv::Vec3w d1 = focus_image.at<cv::Vec3b>(idx, jdx);
            // dlib::assign_pixel(input_img[3](idx, jdx), d1[2]);
            // dlib::assign_pixel(input_img[4](idx, jdx), d1[1]);
            // dlib::assign_pixel(input_img[5](idx, jdx), d1[0]);
        // }
    // }

    // run the network
    dm = dfd_net(input_img);
    
    

}   // end of get_depth_map

// ----------------------------------------------------------------------------------------

int main(int argc, char** argv)
{
    uint32_t idx=0, jdx=0;
    uint8_t status;
    std::string console_input;
    std::string net_name;

    std::vector<ftdiDeviceDetails> ftdi_devices;

    // Lens Driver Variables
    uint32_t ftdi_device_count = 0;
    ftdiDeviceDetails lens_driver_details;
    FT_HANDLE lens_driver_handle = NULL;
    uint32_t lens_driver_dev_num = 0;
    uint32_t connect_count = 0;
    lens_driver ld;
    std::vector<lens_packet_struct> focus_packets;
    std::vector<uint8_t> lens_step;

    // Camera Specific Variables
    FC2::Error error;
    FC2::Camera cam;
    FC2::FC2Config camera_config;
    FC2::CameraInfo cam_info;
    FC2::PixelFormat pixel_format = FC2::PIXEL_FORMAT_RGB8;  //  FC2::PIXEL_FORMAT_422YUV8, FC2::PIXEL_FORMAT_444YUV8;
    FC2::Property Shutter, Gain, Sharpness, Framerate, Brightness, Auto_Exposure;
    cam_properties_struct cam_properties;
    uint64_t cam_serial_number;
    uint32_t cam_index;
    uint32_t cam_number = 0;
    //uint32_t x_offset, y_offset, width, height;
    bool camera_on = true;
    //uint32_t avg_count = 19;
    //std::vector<double> shutter;
    //std::string shutter_str;
    std::vector<uint16_t> cam_image_params;
    
    // OpenCV Variables
    char key = 0;
    cv::Mat focus_image, defocus_image;
    cv::Size img_size;
    std::string image_window = "Image";
    std::string depth_window = "Depth Map";
    std::string defocus_window = "Defocus Image";
    cv::Mat jet_depth_map;
    cv::Mat cv_dm;
    
    dlib::matrix<uint16_t> depth_map;

    
    //////////////////////////////////////////////////////////////////////////////////

    if (argc == 1)
    {
        std::cout << "Enter the following as arguments into the program:" << std::endl;
        std::cout << "<input_config_file.txt>" << std::endl;
        std::cout << endl;
        std::cin.ignore();
        return 0;
    }

    std::string parse_filename = argv[1];   
    parse_dnn_cam_file(parse_filename, lens_step, cam_image_params, cam_properties, net_name);
    
    // camera properties settings (value, AutoMode, OnOff, absControl)
    // cam_properties.sharpness = std::make_tuple(2048, false, true, false);
    // cam_properties.fps = std::make_tuple(30.0, false, true, true);
    // cam_properties.shutter = std::make_tuple(33.0, false, true, true);
    // cam_properties.gain = std::make_tuple(5.0, true, true, true);
    // cam_properties.auto_exp = std::make_tuple(0.0, true, true, true);
    // cam_properties.brightness = std::make_tuple(4.0, false, true, true);
    cam_properties.auto_exp = 0.0;
    cam_properties.brightness = 4.0;    
    
    // std::tuple<uint32_t, bool, bool, bool> sharpness(2500, false, true, false);
    // std::tuple<float, bool, bool, bool> shutter(33.0f, true, true, true);
    // std::tuple<float, bool, bool, bool> gain(5.0f, true, true, true);
    // std::tuple<float, bool, bool, bool> brightness(4.0f, false, true, true);
    // std::tuple<float, bool, bool, bool> auto_exp(0.0f, true, true, true);
    // std::tuple<float, bool, bool, bool> fps(20.0f, false, true, true);

    focus_packets.clear();
    
    try
    {

        ftdi_device_count = get_device_list(ftdi_devices);
        if (ftdi_device_count == 0)
        {
            std::cout << "No ftdi devices found... Exiting!" << std::endl;
            std::cin.ignore();
            return -1;
        }

        for (idx = 0; idx < ftdi_devices.size(); ++idx)
        {
            std::cout << ftdi_devices[idx];
        }

        std::cout << "Select Lens Driver: ";
        std::getline(std::cin, console_input);
        lens_driver_dev_num = stoi(console_input);

        std::cout << std::endl << "Connecting to Lens Driver..." << std::endl;
        ftdi_devices[lens_driver_dev_num].baud_rate = 250000;
        while ((lens_driver_handle == NULL) && (connect_count < 10))
        {
            lens_driver_handle = open_com_port(ftdi_devices[lens_driver_dev_num]);
            ++connect_count;
        }

        if (lens_driver_handle == NULL)
        {
            std::cout << "No Lens Driver found... Exiting!" << std::endl;
            std::cin.ignore();
            return -1;
        }

        ld.lens_tx = lens_packet_struct(CON, 0);

        // send connection request packet and get response back
        ld.send_lens_packet(ld.lens_tx, lens_driver_handle);
        status = ld.receive_lens_packet(ld.lens_rx, lens_driver_handle, 9);

        if (status == false)
        {
            std::cout << "Error communicating with lens driver... Exiting!" << std::endl;
            std::cin.ignore();
            return -1;
        }

        ld.set_lens_driver_info(ld.lens_rx);
        std::cout << ld << std::endl;

        // reset focus packet
        focus_packets.push_back(lens_packet_struct(FAST_SET_VOLT, 1, { 126 }));
        // in-focus packet
        focus_packets.push_back(lens_packet_struct(FAST_SET_VOLT, 1, { lens_step[0] }));
        // out-of-focus packet
        focus_packets.push_back(lens_packet_struct(FAST_SET_VOLT, 1, { lens_step[1] }));
        

        ld.send_lens_packet(focus_packets[0], lens_driver_handle);
        ld.send_lens_packet(focus_packets[1], lens_driver_handle);

        error = get_camera_selection(cam_index);
        if (error != FC2::PGRERROR_OK)
        {
            print_error(error);
        }

        // Initialize the camera
        error = init_camera(cam, cam_index, camera_config, cam_info);
        if (error == FC2::PGRERROR_OK)
        {
            std::cout << "------------------------------------------------------------------" << std::endl;
            std::cout << cam_info << std::endl;
            cam_serial_number = (uint64_t)cam_info.serialNumber;
        }
        else
        {
            print_error(error);
            std::cin.ignore();
            return -1;
        }

        error = config_imager_format(cam, cam_image_params[0], cam_image_params[1], cam_image_params[2], cam_image_params[3], pixel_format);
        if (error != FC2::PGRERROR_OK)
        {
            print_error(error);
        }

        error = cam.StartCapture();
        if (error != FC2::PGRERROR_OK)
        {
            print_error(error);
        }

        // set the frame rate for the camera
        config_property(cam, Framerate, FC2::FRAME_RATE, std::get<1>(fps), std::get<2>(fps), std::get<3>(fps));
        error = set_abs_property(cam, Framerate, std::get<0>(fps));
        if (error != FC2::PGRERROR_OK)
        {
            print_error(error);
        }

        // config Sharpness to initial value and set to auto
        config_property(cam, Sharpness, FC2::SHARPNESS, std::get<1>(sharpness), std::get<2>(sharpness), std::get<3>(sharpness));
        error = set_int_property(cam, Sharpness, std::get<0>(sharpness));
        if (error != FC2::PGRERROR_OK)
        {
            print_error(error);
        }

        // configure the auto-exposure property
        config_property(cam, Auto_Exposure, FC2::AUTO_EXPOSURE, std::get<1>(auto_exp), std::get<2>(auto_exp), std::get<3>(auto_exp));
        error = set_abs_property(cam, Auto_Exposure, std::get<0>(auto_exp));
        if (error != FC2::PGRERROR_OK)
        {
            print_error(error);
        }

        // configure the brightness property
        config_property(cam, Brightness, FC2::BRIGHTNESS, std::get<1>(brightness), std::get<2>(brightness), std::get<3>(brightness));
        error = set_abs_property(cam, Brightness, std::get<0>(brightness));
        if (error != FC2::PGRERROR_OK)
        {
            print_error(error);
        }

        // config Shutter to initial value and set to auto
        config_property(cam, Shutter, FC2::SHUTTER, std::get<1>(shutter), std::get<2>(shutter), std::get<3>(shutter));
        error = set_abs_property(cam, Shutter, std::get<0>(shutter));
        if (error != FC2::PGRERROR_OK)
        {
            print_error(error);
        }

        sleep_ms(500);

        // config Gain to initial value and set to auto
        config_property(cam, Gain, FC2::GAIN, std::get<1>(gain), std::get<2>(gain), std::get<3>(gain));
        error = set_abs_property(cam, Gain, std::get<0>(gain));
        if (error != FC2::PGRERROR_OK)
        {
            print_error(error);
        }

        img_size = cv::Size(cam_image_params[2], cam_image_params[3]);
        std::cout << "------------------------------------------------------------------" << std::endl;
        std::cout << "X, Y, Width, Height: " << cam_image_params[0] << ", " << cam_image_params[1] << ", " << cam_image_params[2] << ", " << cam_image_params[3] << std::endl;
        std::cout << cam_properties;
        std::cout << "------------------------------------------------------------------" << std::endl;

        //std::cout << "Root save location: " << output_save_location << std::endl;
        //std::cout << "------------------------------------------------------------------" << std::endl;

        // set the camera to software trigger to get images
        error = set_software_trigger(cam, true);
        if (error != FC2::PGRERROR_OK)
        {
            print_error(error);
        }

        // load in the network
        dlib::set_dnn_prefer_smallest_algorithms();

        dfd_net_type dfd_net;

        std::cout << "Loading " << net_name << std::endl;
        dlib::deserialize(net_name) >> dfd_net;

        std::cout << dfd_net << std::endl;

        image_window = image_window + num2str<uint64_t>(cam_serial_number, "_%llu");

        std::cout << std::endl << "-----------------------------------------------------------------------------" << std::endl;
        std::cout << "Press c to capture an image pair." << std::endl;
        std::cout << "Press q to quit." << std::endl;
        //std::getline(std::cin, console_input);

//-------------------------------------------------------------------------------
// Main loop to capture the images and process them through the network
//-------------------------------------------------------------------------------
        cv::namedWindow(image_window, cv::WindowFlags::WINDOW_NORMAL);
        cv::namedWindow(depth_window, cv::WindowFlags::WINDOW_NORMAL);
        cv::namedWindow(defocus_window, cv::WindowFlags::WINDOW_NORMAL);

        while (key != 'q')
        {
            // send the focus packet to reset the lens driver to a known good state
            ld.send_lens_packet(focus_packets[0], lens_driver_handle);

            // get the in-focus image
            error = get_image(cam, lens_driver_handle, focus_packets[1], focus_image);
            if (error != FC2::PGRERROR_OK)
            {
                print_error(error);
            }

            //sleep_ms(10);

            // get the out-of-focus image
            error = get_image(cam, lens_driver_handle, focus_packets[2], defocus_image);
            if (error != FC2::PGRERROR_OK)
            {
                print_error(error);
            }

            // process the images to get them in the right format for network input
            get_depth_map(dfd_net, focus_image, defocus_image, depth_map);

            //jet_depth_map = dlib::toMat(depth_map);
            //jet_depth_map.convertTo(jet_depth_map, CV_8UC1, 1, 0);
            
            cv_dm = dlib::toMat(depth_map);
            cv_dm.convertTo(cv_dm, CV_8UC1, 1, 0);
            
            //cv::applyColorMap(jet_depth_map, jet_depth_map, cv::COLORMAP_JET);

            cv::imshow(image_window, focus_image);
            cv::imshow(defocus_window, defocus_image);
            cv::imshow(depth_window, cv_dm);

            key = cv::waitKey(1);

            switch (key)
            {
                case 'f':
                    std::cout << "Enter focus lens value:";
                    std::getline(std::cin, console_input);
                    try {
                        focus_packets[1].data[0] = stoi(console_input);
                    }
                    catch (...) {}
                    break;

                case 'd':
                    std::cout << "Enter defocus lens value:";
                    std::getline(std::cin, console_input);
                    try {
                        focus_packets[2].data[0] = stoi(console_input);
                    }
                    catch (...) {}
                    break;

                case 'c':

                    break;

                default:
                    break;
            }


            //std::cout << ".";
            //std::cout << "Ready..." << std::endl;
            //std::getline(std::cin, console_input);
        }

    }
    catch(std::exception e)
    {
        std::cout << "Error: " << e.what() << std::endl;
    }

    // turn off the software trigger
    error = set_software_trigger(cam, false);
    if (error != FC2::PGRERROR_OK)
    {
        print_error(error);
    }

    // Stop capturing images
    error = cam.StopCapture();
    if (error != FC2::PGRERROR_OK)
    {
        print_error(error);
    }

    // Disconnect the camera
    error = cam.Disconnect();
    if (error != FC2::PGRERROR_OK)
    {
        print_error(error);
    }

    close_com_port(lens_driver_handle);

    cv::destroyAllWindows();
    
    std::cout << std::endl << "Program Compete!" << std::endl;
    std::cin.ignore();
    
    return 0;    
    
}   // end of main