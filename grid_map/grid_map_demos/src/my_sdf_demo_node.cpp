#include <ros/ros.h>
#include <grid_map_ros/grid_map_ros.hpp>
#include <sensor_msgs/PointCloud2.h>
#include <grid_map_ros/GridMapRosConverter.hpp>
#include <grid_map_sdf/SignedDistanceField.hpp>
#include <vector>
#include <string>

using namespace grid_map;
void generateSampleGridMap(grid_map::GridMap&, std::string&);

int main(int argc, char **argv){
    ros::init(argc, argv, "my_sdf_demo");
    ros::NodeHandle nh("~");
    ros::Publisher publisher = nh.advertise<grid_map_msgs::GridMap>("grid_map", 1, true);
    
    std::string elevationLayer_("elevation");
    GridMap map({elevationLayer_});
    map.setFrameId("map");
    map.setGeometry(Length(3.0, 3.0), 0.05, Position(0.0, 0.0));
    ROS_INFO("Create map with size %f x %f m (%i x %i cells).\n The center of the map is located at (%f, %f) in the %s frame.", 
      map.getLength().x(), map.getLength().y(),
      map.getSize()(0), map.getSize()(1),
      map.getPosition().x(), map.getPosition().y(), map.getFrameId().c_str());
    
    // sdf init
    std::string pointcloudTopic("pointcloud_topic");
    ros::Publisher pointcloudPublisher_;
    ros::Publisher freespacePublisher_;
    ros::Publisher occupiedPublisher_;
    pointcloudPublisher_ = nh.advertise<sensor_msgs::PointCloud2>(pointcloudTopic + "/full_sdf", 1);
    freespacePublisher_ = nh.advertise<sensor_msgs::PointCloud2>(pointcloudTopic + "/free_space", 1);
    occupiedPublisher_ = nh.advertise<sensor_msgs::PointCloud2>(pointcloudTopic + "/occupied_space", 1);    

    generateSampleGridMap(map, elevationLayer_);

    auto& elevationData = map.get(elevationLayer_);
    // Inpaint if needed.
    if (elevationData.hasNaN()) {
        const float inpaint{elevationData.minCoeffOfFinites()};
        ROS_WARN("[SdfDemo] Map contains NaN values. Will apply inpainting with min value.");
        elevationData = elevationData.unaryExpr([=](float v) { return std::isfinite(v)? v : inpaint; });
    }
    // Generate SDF.
    const float heightMargin{0.1};
    const float minValue{elevationData.minCoeffOfFinites() - heightMargin};
    const float maxValue{elevationData.maxCoeffOfFinites() + heightMargin};
    grid_map::SignedDistanceField sdf(map, elevationLayer_, minValue, maxValue);  

    ros::Rate rate(30.0);
    while(nh.ok()){
        // elevation
        ros::Time time = ros::Time::now();   
        map.setTimestamp(time.toNSec());

        // Extract as point clouds. puhlish sdf
        sensor_msgs::PointCloud2 pointCloud2Msg;
        grid_map::GridMapRosConverter::toPointCloud(sdf, pointCloud2Msg);
        pointcloudPublisher_.publish(pointCloud2Msg);

        grid_map::GridMapRosConverter::toPointCloud(sdf, pointCloud2Msg, 1, [](float sdfValue) { return sdfValue > 0.0; });
        freespacePublisher_.publish(pointCloud2Msg);

        grid_map::GridMapRosConverter::toPointCloud(sdf, pointCloud2Msg, 1, [](float sdfValue) { return sdfValue <= 0.0; });
        occupiedPublisher_.publish(pointCloud2Msg);      

        // publish
        grid_map_msgs::GridMap message;
        grid_map::GridMapRosConverter::toMessage(map, message);
        publisher.publish(message);
        ROS_INFO_THROTTLE(1.0, "Grid map (timestamp %f) published.", message.info.header.stamp.toSec());
        rate.sleep();
    }
    
    return 0;
}

void generateSampleGridMap(grid_map::GridMap& map, std::string& elevationLayer_){
    // generate grid_map
    for(GridMapIterator it(map); !it.isPastEnd(); ++it){
        map.at(elevationLayer_, *it) = 0;
    }
    // submap
    Index submapStartIndex(3, 5);
    Index submapBufferSize(12, 7);
    for (grid_map::SubmapIterator it(map, submapStartIndex, submapBufferSize);
        !it.isPastEnd(); ++it) {
        map.at(elevationLayer_, *it) = 1.0;
    }
    // circle
    Position center1(0.0, 0.0);
    double radius1 = 0.4;
    for (grid_map::CircleIterator it(map, center1, radius1);
        !it.isPastEnd(); ++it) {
        map.at(elevationLayer_, *it) = 1.0;
    }
    // Ellipse      
    Position center2(-1.0, -1.0);
    Length length(0.45, 0.9);
    for (grid_map::EllipseIterator it(map, center2, length, M_PI_4);
        !it.isPastEnd(); ++it) {
        map.at(elevationLayer_, *it) = 1.0;
    }
    // line
    Index start1(-18, 2);
    Index end1(-2, 13);
    for (grid_map::LineIterator it(map, start1, end1);
        !it.isPastEnd(); ++it) {
        map.at(elevationLayer_, *it) = 1.0;
    }
    Index start2(-18, 1);
    Index end2(-2, 12);
    for (grid_map::LineIterator it(map, start2, end2);
        !it.isPastEnd(); ++it) {
        map.at(elevationLayer_, *it) = 1.0;
    }
}