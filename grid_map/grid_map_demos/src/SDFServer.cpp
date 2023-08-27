#include "grid_map_demos/SDFServer.hpp"

void SDFServer::toTXT(const std::vector<cv::Mat>& src, const std::vector<std::string>& pathnames){
    for(size_t i = 0; i <  pathnames.size(); i++){
        Eigen::Matrix<float, -1, -1> target;
        cv::cv2eigen(src[i], target);
        std::ofstream fout(pathnames[i]);
        if(fout.is_open()){
            fout<<target;
            fout.close();
            ROS_INFO("Matrix written to file [%s] successfully.", pathnames[i].c_str());
        } else{
            ROS_WARN("Unable to open file [%s]", pathnames[i].c_str());
        }
    }
}

void SDFServer::find_extrema_points(const cv::Mat& src_doh, std::vector<cv::Point>& dst_extrema_points){
    // loop through each pixel in the image
    cv::Mat extrema = cv::Mat::zeros(src_doh.size(), CV_8UC1);
    for(int i = radius_; i < src_doh.rows - radius_; i++){
        for(int j = radius_; j < src_doh.cols - radius_; j++){
            // check if the current pixel is an extremum
            float value = src_doh.at<float>(i, j);
            bool is_extremum = true;
            for(int k = -1; k <= 1; k++){
                for(int l = -1; l <= 1; l++){
                    if(value < src_doh.at<float>(i+k, j+l)){
                        is_extremum = false;
                        break;
                    }
                }
                if(!is_extremum){
                    break;
                }
            }
            if(is_extremum){
                extrema.at<uchar>(i, j) = 255;
            }
        }
    }
    cv::Mat extrema_trans;
    cv::transpose(extrema, extrema_trans);
    cv::findNonZero(extrema_trans, dst_extrema_points);
}
/// @brief Classify extrema points by the sign of eigen value.
/// @param src_extrema_points 
/// @param src_eigenvalue1 
/// @param src_eigenvalue2 
/// @param dst 
void SDFServer::classify_extrema_points(const std::vector<cv::Point>& src_extrema_points, 
                                            cv::Mat& src_eigenvalue1 , cv::Mat& src_eigenvalue2, 
                                            std::vector<std::vector<cv::Point>>& dst){
    dst = std::vector<std::vector<cv::Point>>(4);
    // 0: extrema max; 1: extrema min, 2: extrema saddle
    for(const auto& pt: src_extrema_points){
        float ev1 = src_eigenvalue1.at<float>(pt.x, pt.y);
        float ev2 = src_eigenvalue2.at<float>(pt.x, pt.y);
        // local maximal
        if(ev1 < 0 && ev2 < 0){
            dst[0].push_back(pt);
        }
        // local minimal
        if(ev1 > 0 && ev2 > 0){
            dst[1].push_back(pt);
        }
        // saddle
        if(ev1 * ev2 < 0){
            dst[2].push_back(pt);
        }
        // critical
        if(ev1 * ev2 == 0){
            dst[3].push_back(pt);
        }
    }
}

void SDFServer::detect_gaussian_curvature_and_eigen(const cv::Mat& src, int ksize, cv::Mat& dst_doh, cv::Mat& dst_eigenvalue1 , cv::Mat& dst_eigenvalue2){
    cv::Mat gaussBlur, dx, dy, dxx, dxy, dyy;
    // 1. gaussian blue
    cv::GaussianBlur(src, gaussBlur, cv::Size(5, 5), 0, 0);
    
    // 2. hessian
    cv::Sobel(gaussBlur, dx, -1, 1, 0, ksize);
    cv::Sobel(gaussBlur, dy, -1, 0, 1, ksize);
    cv::Sobel(dx, dxx, -1, 1, 0, ksize);
    cv::Sobel(dx, dxy, -1, 0, 1, ksize);
    cv::Sobel(dy, dyy, -1, 0, 1, ksize);

    // 3. DoH & gaussian curvature & eigen
    dst_doh.create(src.size(), CV_32FC1);
    dst_eigenvalue1.create(src.size(), CV_32FC1);
    dst_eigenvalue2.create(src.size(), CV_32FC1);

    for(int i = 0; i < src.rows; i++){
        for(int j = 0; j < src.cols; j++){
            // float fx = dx.at<float>(i, j);
            // float fy = dy.at<float>(i, j);
            float fxx = dxx.at<float>(i, j);
            float fxy = dxy.at<float>(i, j);
            float fyy = dyy.at<float>(i, j);
            // DoH
            float doh = fxx * fyy - fxy * fxy;
            // gaussian curvature
            // float k = doh / pow(fx * fx + fy * fy + 1e-8, 2);

            // eigen value & eigen vector
            cv::Mat hessianAtEachPoint_(2, 2, CV_32FC1);
            hessianAtEachPoint_.at<float>(0, 0) = fxx;
            hessianAtEachPoint_.at<float>(0, 1) = fxy;
            hessianAtEachPoint_.at<float>(1, 0) = fxy;
            hessianAtEachPoint_.at<float>(1, 1) = fyy;
            cv::Mat eigenValue_, eigenVector_;
            cv::eigen(hessianAtEachPoint_, eigenValue_, eigenVector_);
            // float gaussCurv_ = eigenValue_.at<float>(0, 0) * eigenValue_.at<float>(1, 0);
            // gaussianCurvature_.at<float>(i, j) = gaussCurv_;

            dst_doh.at<float>(i, j) = doh;
            dst_eigenvalue1.at<float>(i, j) = eigenValue_.at<float>(0, 0);
            dst_eigenvalue2.at<float>(i, j) = eigenValue_.at<float>(1, 0);

            // dst.at<float>(i, j) = k;
            // dst.at<float>(i, j) = gaussCurv_;
        }
    }
}

void SDFServer::makeDescriptorForSingleKeypoint(cv::Mat& src_sdf_, cv::Point& keypoint, int point_type){
    // 1. Compute keypoint roi window
    cv::Mat kp_window = cv::Mat::zeros(src_sdf_.size(), src_sdf_.type());
    cv::Mat mask = cv::Mat::zeros(src_sdf_.size(), CV_8UC1);
    cv::circle(mask, keypoint, radius_, cv::Scalar(255), -1);
    src_sdf_.copyTo(kp_window, mask);

    cv::Mat noZeroCoordinates;
    cv::findNonZero(kp_window, noZeroCoordinates);
    cv::Rect boundingRect = cv::boundingRect(noZeroCoordinates);
    cv::Mat roi = kp_window(boundingRect);

    // compute gradient of sdf map
    cv::Mat grad_x, grad_y, grad_mag, grad_dir;
    cv::Sobel(roi, grad_x, -1, 1, 0, 3);
    cv::Sobel(roi, grad_y, -1, 0, 1, 3);
    cv::cartToPolar(grad_x, grad_y, grad_mag, grad_dir, true);

    // 2. 36-bin gradient orientation Histogram
    cv::Mat hist_36bin = cv::Mat::zeros(1, 36, CV_32FC1);
    int rows = grad_dir.rows;
    int cols = grad_dir.cols;
    float bin_width = 360.0/36.0;
    for(int i = 0; i < rows; i++){
        for(int j = 0; j < cols; j++){
            int bin_idx = int(grad_dir.at<float>(i, j) / bin_width);
            hist_36bin.at<float>(0, bin_idx) += grad_mag.at<float>(i, j) * gaussianDistanceWeight(i, j);
        }
    }
    cv::normalize(hist_36bin, hist_36bin, 1.0);

    // 3. 17-bin relative histogram
    int main_bin = 0;
    float main_val = hist_36bin.at<float>(0, 0);
    for(int i = 0; i < 36; i++){
        if(hist_36bin.at<float>(0, i) > main_val){
            main_val = hist_36bin.at<float>(0, i);
            main_bin = i;
        }
    }
    std::vector<float> hist_17bin_data = std::vector<float>(17);
    for(int i = 0; i < 36; i++){
        int idx = (i - main_bin + 36) % 36;
        if(idx < 18){
            hist_17bin_data[idx] += hist_36bin.at<float>(0, i);
        } else {
            hist_17bin_data[idx - 18] += hist_36bin.at<float>(0, i);
        }
    }

    // 4. average distance
    float avg_dis{0};
    for(int i = 0; i < roi.rows; i++){
        for(int j = 0; j < roi.cols; j++){
            avg_dis += roi.at<float>(i, j);
        }
    }
    avg_dis /= roi.rows * roi.cols;

    SDFKeyPoint sdfkeypoint(keypoint, avg_dis, hist_17bin_data, point_type);
    sdfkeypoints_[point_type].push_back(sdfkeypoint);
    // cv::Mat hist_17bin(hist_17bin_data);
    // std::vector<cv::Mat> data_{hist_36bin, hist_17bin};
    // std::vector<std::string> pnames{"/home/yuxuanzhao/Desktop/hist_36bin.txt", "/home/yuxuanzhao/Desktop/hist_17bin.txt"};
    // toTXT(data_, pnames);
}

float SDFServer::gaussianDistanceWeight(int i, int j){
    float sigma = 1.0;
    float expf_scale = -1.0/(2.0 * sigma * sigma);
    return std::exp((i*i+j*j)*expf_scale);
}

// void log(){
//     cv::Mat scaled_angle = grad_dir * (255 / ( 2 * CV_PI));
//     cv::Mat hsvImage, hsv[3];
//     cv::normalize(grad_mag, grad_mag, 0, 255, cv::NORM_MINMAX);
//     hsv[0] = scaled_angle;
//     hsv[1] = cv::Mat::ones(scaled_angle.size(), CV_32F);
//     hsv[2] = grad_mag;
//     cv::merge(hsv, 3, hsvImage);
//     cv::cvtColor(hsvImage, hsvImage, cv::COLOR_HSV2BGR);

//     cv::namedWindow("gradient", cv::WINDOW_NORMAL);
//     cv::imshow("gradient", hsvImage);
//     cv::resizeWindow("gradient", 800, 800);
//     cv::waitKey(0);

//     std::vector<cv::Mat> data_{roi, grad_mag, grad_dir};
//     std::vector<std::string> pnames{"/home/yuxuanzhao/Desktop/roi.txt", "/home/yuxuanzhao/Desktop/grad_mag.txt", "/home/yuxuanzhao/Desktop/grad_dir.txt"};
//     toTXT(data_, pnames);
// }

bool SDFServer::srvCallback(grid_map_demos::sdfDetect::Request& req, grid_map_demos::sdfDetect::Response& res){
    cv_bridge::CvImagePtr cv_ptr;
    cv_ptr = cv_bridge::toCvCopy(req.sdf_map, sensor_msgs::image_encodings::TYPE_32FC1);

    cv::Mat src_sdf_ = cv_ptr->image;
    cv::Mat doh_, eigenValue1_, eigenValue2_;
    std::vector<cv::Point> extrema_points_;
    std::vector<std::vector<cv::Point>> classified_extrema_points_;

    // keypoint detector
    detect_gaussian_curvature_and_eigen(src_sdf_, 3, doh_, eigenValue1_, eigenValue2_);
    find_extrema_points(doh_, extrema_points_);
    classify_extrema_points(extrema_points_, eigenValue1_, eigenValue2_, classified_extrema_points_);

    grid_map_demos::PointCloud17 data;
    int type_offset = 1;
    for(int i = 0; i < 4; i++){
        for(const auto& pt: classified_extrema_points_[i]){
            grid_map_demos::Point17 pt17;
            pt17.x = pt.x;
            pt17.y = pt.y;
            pt17.type = float(type_offset + i);
            data.points.push_back(pt17);
        }
    }
    res.cloud = data;
    // keypoint description
    // sensor_msgs::PointCloud msg_extrema_points;
    // int offset_ = 1;
    // for(int i = 0; i < 4; i++){
    //     for(const auto& pt: classified_extrema_points_[i]){
    //         geometry_msgs::Point32 point32;
    //         point32.x = pt.x;
    //         point32.y = pt.y;
    //         point32.z = float(i+offset_);
    //         msg_extrema_points.points.push_back(point32);
    //     }
    // }
    // for(int i = 0 ; i < 4; i++){
    //     for(int j = 0; j < classified_extrema_points_[i].size(); j++){
    //         makeDescriptorForSingleKeypoint(src_sdf_, classified_extrema_points_[i][j], i);
    //     }
    // }
    // res.cloud = msg_extrema_points;

    return true;
}


void SDFServer::imageCallback(const sensor_msgs::ImageConstPtr& msg){
    cv_bridge::CvImagePtr cv_ptr;
    try{
        cv_ptr = cv_bridge::toCvCopy(msg, sensor_msgs::image_encodings::TYPE_32FC1);
        ROS_INFO_THROTTLE(1.0, "[Callback] SDF Descriptor...");
    } catch (cv_bridge::Exception& e){
        ROS_ERROR("cv_bridge exception: %s", e.what());
        return ;
    }

    cv::Mat src_sdf_ = cv_ptr->image;
    cv::Mat doh_, eigenValue1_, eigenValue2_;
    std::vector<cv::Point> extrema_points_;
    std::vector<std::vector<cv::Point>> classified_extrema_points_;

    detect_gaussian_curvature_and_eigen(src_sdf_, 3, doh_, eigenValue1_, eigenValue2_);
    find_extrema_points(doh_, extrema_points_);
    classify_extrema_points(extrema_points_, eigenValue1_, eigenValue2_, classified_extrema_points_);

    sensor_msgs::PointCloud msg_extrema_points;
    int offset_ = 1;
    for(int i = 0; i < 3; i++){
        for(const auto& pt: classified_extrema_points_[i]){
            geometry_msgs::Point32 point32;
            point32.x = pt.x;
            point32.y = pt.y;
            point32.z = float(i+offset_);
            msg_extrema_points.points.push_back(point32);
        }
    }
    pub_extrema_points_.publish(msg_extrema_points);

    if(once_ == 0){
        once_++;
        std::vector<cv::Mat> data_{doh_};
        std::vector<std::string> filenames_{"/home/yuxuanzhao/Desktop/gaussCurv.txt"};
        toTXT(data_, filenames_);
    }
}
