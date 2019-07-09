#ifndef DFD_DNN_CAM_H_
#define DFD_DNN_CAM_H_

#include <cstdint>
#include <utility>

#include "file_parser.h"

extern const uint32_t img_depth;
extern const uint32_t secondary;

void parse_dnn_cam_file(std::string parse_filename, std::vector<uint8_t> &lens_step, std::vector<uint16_t> &cam_image_params, cam_properties_struct &cam_properties, std::string &net_name)
{

    std::vector<std::vector<std::string>> params;
    parse_csv_file(parse_filename, params);

    for (uint64_t idx = 0; idx<params.size(); ++idx)
    {
        switch (idx)
        {
            // voltage step setting for the lens driver
            case 0:
                try {
                    lens_step.clear();
                    lens_step.push_back(std::stoi(params[idx][0]));
                    lens_step.push_back(std::stoi(params[idx][1]));                    
                }
                catch (std::exception &e) {
                    std::cout << e.what() << std::endl;
                    lens_step.clear();
                    lens_step.push_back(133);
                    lens_step.push_back(130); 
                    std::cout << "Error getting lens step values.  Setting values to default." << std::endl;
                }
                break;
                
            // camera image paramerters (x_offset, y_offset, width, height)
            case 1:
                try {
                    cam_image_params.clear();
                    cam_image_params.push_back(stoi(params[idx][0]));
                    cam_image_params.push_back(stoi(params[idx][1]));
                    cam_image_params.push_back(stoi(params[idx][2]));
                    cam_image_params.push_back(stoi(params[idx][3]));

                }
                catch (std::exception &e) {
                    std::cout << e.what() << std::endl;
                    cam_image_params.clear();
                    cam_image_params.push_back(150);   // x offset
                    cam_image_params.push_back(152);   // y offset
                    cam_image_params.push_back(900);   // width
                    cam_image_params.push_back(720);   // height
                    std::cout << "Error getting image capture parameters.  Setting values to default." << std::endl;
                }
                break;
                
            case 2:
                try {
                    
                    cam_properties.sharpness = stoi(params[idx][0]);
                    cam_properties.fps = stof(params[idx][1]);
                    cam_properties.shutter = stod(params[idx][0]);
                    cam_properties.gain = stof(params[idx][0]);                    
                }
                catch (std::exception &e) {
                    std::cout << e.what() << std::endl;
                    cam_properties.sharpness = 2500;
                    cam_properties.fps = 10.0;
                    cam_properties.shutter = 50.0;
                    cam_properties.gain = 8.0;                      
                }
                break;
            // net name
            case 3:
                net_name = params[idx][0];
                break;

            default:
                break;
        }   // end of switch

    }   // end of for

}   // end of parse_dnn_cam_file

#endif  // DFD_DNN_CAM_H_