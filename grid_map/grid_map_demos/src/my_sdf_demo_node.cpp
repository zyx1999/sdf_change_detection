#include <ros/ros.h>
#include "grid_map_demos/SDF2D.hpp"

using namespace grid_map;

int main(int argc, char **argv){
    ros::init(argc, argv, "my_sdf_demo");

    SDF2D sdf2d;

    ros::Rate rate(10.0);
    while(sdf2d.nh.ok()){
        // elevation
        ros::Time time = ros::Time::now();   
        sdf2d.map.setTimestamp(time.toNSec());

        // publish sdf to Converter, then wait until subscribe the return message
        sdf2d.imgTransPub.publish(sdf2d.signedDistanceMsg_);
        ros::spinOnce();
        
        // publish map message to visualizer
        grid_map_msgs::GridMap message;
        grid_map::GridMapRosConverter::toMessage(sdf2d.map, message);
        sdf2d.publisher.publish(message);

        ROS_INFO_THROTTLE(1.0, "Grid map (timestamp %f) published.", message.info.header.stamp.toSec());
        rate.sleep();
    }
    
    return 0;
}
