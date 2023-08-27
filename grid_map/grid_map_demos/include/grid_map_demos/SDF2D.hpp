#include <ros/ros.h>
#include <grid_map_ros/grid_map_ros.hpp>
#include <sensor_msgs/PointCloud.h>
#include <sensor_msgs/PointCloud2.h>
#include <grid_map_ros/GridMapRosConverter.hpp>
#include <grid_map_sdf/SignedDistanceField.hpp>
#include "grid_map_sdf/SignedDistance2d.hpp"
#include <vector>
#include <string>
#include <fstream>
#include <image_transport/image_transport.h>
#include <opencv2/highgui/highgui.hpp>
#include <cv_bridge/cv_bridge.h>
#include <opencv2/core/eigen.hpp>
#include <opencv2/core.hpp>
#include <opencv2/opencv.hpp>
#include <pcl_conversions/pcl_conversions.h>
#include "grid_map_demos/sdfDetect.h"
#include "grid_map_demos/img2PointCloud.h"
#include "grid_map_demos/PointCloud17.h"
#include "grid_map_demos/Point17.h"

class SingleMap;
class SDF2D{
public:
    SDF2D(ros::NodeHandle& nh);
    void mapFromImage();

    double minHeight = 0.0;
    double maxHeight = 1.0;
    ros::NodeHandle nh_;
    ros::Publisher publisher;
    ros::ServiceClient client_img2PC;
    ros::ServiceClient client_sdf;
    grid_map_demos::img2PointCloud srv_img2PC;
    grid_map_demos::sdfDetect srv_sdf;
    std::vector<SingleMap> maps_;
};

class SingleMap{
public:
    SingleMap():elevationLayer_("elevation"){}
    int rows_{0}, cols_{0};
    float map_resolution_{0.05};
    grid_map::Length map_length_{80.0, 60.0};
    grid_map::Position map_position_{0.0, 0.0};
    std::string elevationLayer_;
    grid_map::GridMap map;
};