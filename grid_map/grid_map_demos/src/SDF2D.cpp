#include "grid_map_demos/SDF2D.hpp"


SDF2D::SDF2D(ros::NodeHandle& nh): nh_(nh), elevationLayer_("elevation"), pointcloudTopic("pointcloud_topic"){
    mapFromImage();

    plainMapInit();

    generateSampleGridMap(map, elevationLayer_);
    
    grid_map::Matrix signedDistance_;
    to2DSignedDistanceMap(signedDistance_);

    // publishSignedDistanceMsg(signedDistance_);
    callServer(signedDistance_);
}

void SDF2D::mapFromImage(){
    ros::service::waitForService("/img2PC");
    ros::ServiceClient client = nh_.serviceClient<grid_map_demos::img2PointCloud>("/img2PC");
    grid_map_demos::img2PointCloud srv;
    srv.request.type = 1.0;
    client.call(srv);
    sensor_msgs::Image msg = srv.response.img;
    ROS_INFO("|Received Image message:");
    ROS_INFO("|  Header:");
    ROS_INFO("|      Frame ID: %s", msg.header.frame_id.c_str());
    ROS_INFO("|  Image:");
    ROS_INFO("|      Width: %d", msg.width);
    ROS_INFO("|      Height: %d", msg.height);
    ROS_INFO("|      Encoding: %s", msg.encoding.c_str());
    // TODO: convert sensor_msgs::Image to grid_map
}

void SDF2D::callServer(const grid_map::Matrix& signedDistance_){
    cv::Mat signedDistanceMat_;
    cv::eigen2cv(signedDistance_, signedDistanceMat_);

    ros::service::waitForService("/sdf_service");
    ros::ServiceClient client = nh_.serviceClient<grid_map_demos::sdfDetect>("/sdf_service");
    sensor_msgs::ImagePtr msg_sdf = cv_bridge::CvImage(std_msgs::Header(), 
        sensor_msgs::image_encodings::TYPE_32FC1, signedDistanceMat_).toImageMsg();
    grid_map_demos::sdfDetect srv;
    srv.request.sdf_map = *msg_sdf;
    client.call(srv);
    sensor_msgs::PointCloud keypoints = srv.response.keypoints;

    int offset_ = 1;
    cv::Mat data_extrema_max = cv::Mat::zeros(rows_, cols_, CV_32FC1);
    cv::Mat data_extrema_min = cv::Mat::zeros(rows_, cols_, CV_32FC1);
    cv::Mat data_extrema_saddle = cv::Mat::zeros(rows_, cols_, CV_32FC1);
    cv::Mat data_extrema_critical = cv::Mat::zeros(rows_, cols_, CV_32FC1);

    for(const auto& pt : keypoints.points){
        float value = map.at("sdf2d", grid_map::Index(pt.x, pt.y));
        if(pt.z - offset_ == 0){
            data_extrema_max.at<float>(pt.x, pt.y) = value;
        }
        if(pt.z - offset_ == 1){
            data_extrema_min.at<float>(pt.x, pt.y) = value;
        }
        if(pt.z - offset_ == 2){
            data_extrema_saddle.at<float>(pt.x, pt.y) = value;
        }
        if(pt.z - offset_ == 3){
            data_extrema_critical.at<float>(pt.x, pt.y) = value;
        }
    }
    Eigen::Matrix<float, -1, -1> layer_extrema_max, layer_extrema_min, layer_extrema_saddle, layer_extrema_critical;
    cv::cv2eigen(data_extrema_max, layer_extrema_max);
    cv::cv2eigen(data_extrema_min, layer_extrema_min);
    cv::cv2eigen(data_extrema_saddle, layer_extrema_saddle);
    cv::cv2eigen(data_extrema_critical, layer_extrema_critical);

    map.add("extrema_max", layer_extrema_max);
    map.add("extrema_min", layer_extrema_min);
    map.add("extrema_saddle", layer_extrema_saddle);
    map.add("extrema_critical", layer_extrema_critical);

}

void SDF2D::publishSignedDistanceMsg(const grid_map::Matrix& signedDistance_){
    // publish to image_converter with topic: /my_sdf_demo/signed_distance/image_topics
    cv::Mat signedDistanceMat_;
    cv::eigen2cv(signedDistance_, signedDistanceMat_);
    signedDistanceMsg_ = cv_bridge::CvImage(std_msgs::Header(), 
        sensor_msgs::image_encodings::TYPE_32FC1, signedDistanceMat_).toImageMsg();
    image_transport::ImageTransport imgTrans(nh_);
    imgTransPub = imgTrans.advertise("signed_distance", 1);
    // imgTransSub = imgTrans.subscribe("extrema_points", 1, boost::bind(&SDF2D::callback, this, _1));
    sub_extrema_points_ = nh_.subscribe<sensor_msgs::PointCloud>("extrema_points", 10, boost::bind(&SDF2D::callback, this, _1));
}

void SDF2D::plainMapInit(){
    publisher = nh_.advertise<grid_map_msgs::GridMap>("grid_map", 1, true);
    // map init.
    map.add(elevationLayer_);
    map.setFrameId("map");
    map.setGeometry(map_length_, map_resolution_, map_position_);
    ROS_INFO("Create map with size %f x %f m (%i x %i cells).\n The center of the map is located at (%f, %f) in the %s frame.", 
    map.getLength().x(), map.getLength().y(),
    map.getSize()(0), map.getSize()(1),
    map.getPosition().x(), map.getPosition().y(), map.getFrameId().c_str());
    rows_ = map.getSize()(0);
    cols_ = map.getSize()(1);
}

void SDF2D::to2DSignedDistanceMap(grid_map::Matrix& signedDistance){
    // elevationData is Matrix
    auto& elevationData = map.get(elevationLayer_);

    // Inpaint if needed.
    if (elevationData.hasNaN()) {
        const float inpaint{elevationData.minCoeffOfFinites()};
        ROS_WARN("[SdfDemo] Map contains NaN values. Will apply inpainting with min value.");
        elevationData = elevationData.unaryExpr([=](float v) { return std::isfinite(v)? v : inpaint; });
    }

    // Generate 2D SDF.
    Eigen::Matrix<bool, -1, -1> occupancy = elevationData.unaryExpr([=](float val) { return val > 0.5; });
    signedDistance = grid_map::signed_distance_field::signedDistanceFromOccupancy(occupancy, map_resolution_);
    map.add("sdf2d", signedDistance);
}

void SDF2D::callback(const sensor_msgs::PointCloud::ConstPtr& msg){
    ROS_INFO("[Callback] SDF2D...");
    int offset_ = 1;
    cv::Mat data_extrema_max = cv::Mat::zeros(rows_, cols_, CV_32FC1);
    cv::Mat data_extrema_min = cv::Mat::zeros(rows_, cols_, CV_32FC1);
    cv::Mat data_extrema_saddle = cv::Mat::zeros(rows_, cols_, CV_32FC1);
    for(const auto& pt : msg->points){
        float value = map.at("sdf2d", grid_map::Index(pt.x, pt.y));
        if(pt.z - offset_ == 0){
            data_extrema_max.at<float>(pt.x, pt.y) = value;
        }
        if(pt.z - offset_ == 1){
            data_extrema_min.at<float>(pt.x, pt.y) = value;
        }
        if(pt.z - offset_ == 2){
            data_extrema_saddle.at<float>(pt.x, pt.y) = value;
        }
    }
    Eigen::Matrix<float, -1, -1> layer_extrema_max, layer_extrema_min, layer_extrema_saddle;
    cv::cv2eigen(data_extrema_max, layer_extrema_max);
    cv::cv2eigen(data_extrema_min, layer_extrema_min);
    cv::cv2eigen(data_extrema_saddle, layer_extrema_saddle);
    map.add("extrema_max", layer_extrema_max);
    map.add("extrema_min", layer_extrema_min);
    map.add("extrema_saddle", layer_extrema_saddle);
}
void SDF2D::generateSampleGridMap(grid_map::GridMap& map, std::string& elevationLayer_){
    // generate grid_map
    for(grid_map::GridMapIterator it(map); !it.isPastEnd(); ++it){
        map.at(elevationLayer_, *it) = 0;
    }
    // // submap
    // Index submapStartIndex(3, 5);
    // Index submapBufferSize(12, 7);
    // for (grid_map::SubmapIterator it(map, submapStartIndex, submapBufferSize);
    //     !it.isPastEnd(); ++it) {
    //     map.at(elevationLayer_, *it) = 1.0;
    // }
    // circle
    grid_map::Position center1(0, 0);
    double radius1 = 3.5;
    for (grid_map::CircleIterator it(map, center1, radius1);
        !it.isPastEnd(); ++it) {
        map.at(elevationLayer_, *it) = 1.0;
    }
    // Ellipse      
    grid_map::Position center2(-20, 10);
    grid_map::Length length(2.5, 4.5);
    for (grid_map::EllipseIterator it(map, center2, length, M_PI_4);
        !it.isPastEnd(); ++it) {
        map.at(elevationLayer_, *it) = 1.0;
    }
    // line
    // Index start1(-18, 2);
    // Index end1(-2, 13);
    // for (grid_map::LineIterator it(map, start1, end1);
    //     !it.isPastEnd(); ++it) {
    //     map.at(elevationLayer_, *it) = 1.0;
    // }
    // Index start2(-18, 1);
    // Index end2(-2, 12);
    // for (grid_map::LineIterator it(map, start2, end2);
    //     !it.isPastEnd(); ++it) {
    //     map.at(elevationLayer_, *it) = 1.0;
    // }
    // box
    grid_map::Index lt(5, 5), rt(5, 115), lb(155, 5), rb(155, 115);
    for(grid_map::LineIterator it(map, lt, rt); !it.isPastEnd(); ++it){
        map.at(elevationLayer_, *it) = 1.0;
    }
    for(grid_map::LineIterator it(map, lb, rb); !it.isPastEnd(); ++it){
        map.at(elevationLayer_, *it) = 1.0;
    } 
    for(grid_map::LineIterator it(map, lt, lb); !it.isPastEnd(); ++it){
        map.at(elevationLayer_, *it) = 1.0;
    } 
    for(grid_map::LineIterator it(map, rt, rb); !it.isPastEnd(); ++it){
        map.at(elevationLayer_, *it) = 1.0;
    }
    // submap
    grid_map::Index submapStartIndex(29, 20);
    grid_map::Index submapBufferSize(2, 10);
    for (grid_map::SubmapIterator it(map, submapStartIndex, submapBufferSize);
        !it.isPastEnd(); ++it) {
        map.at(elevationLayer_, *it) = 1.0;
    }
    grid_map::Index submapStartIndex2(17, 25);
    grid_map::Index submapBufferSize2(1, 1);
    for (grid_map::SubmapIterator it(map, submapStartIndex2, submapBufferSize2);
        !it.isPastEnd(); ++it) {
        map.at(elevationLayer_, *it) = 1.0;
    }
    grid_map::Index submapStartIndex3(50, 5);
    grid_map::Index submapBufferSize3(6, 3);
    for (grid_map::SubmapIterator it(map, submapStartIndex3, submapBufferSize3);
        !it.isPastEnd(); ++it) {
        map.at(elevationLayer_, *it) = 1.0;
    }
    grid_map::Index submapStartIndex4(50, 43);
    grid_map::Index submapBufferSize4(6, 2);
    for (grid_map::SubmapIterator it(map, submapStartIndex4, submapBufferSize4);
        !it.isPastEnd(); ++it) {
        map.at(elevationLayer_, *it) = 1.0;
    }
}