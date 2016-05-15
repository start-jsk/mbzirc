
#include <jsk_mbzirc_tasks/uav_detect_landing_region.h>

UAVLandingRegion::UAVLandingRegion() :
    down_size_(1), ground_plane_(0.0), track_width_(3.0f) {
   
    // std::string templ_path;
    // pnh_.getParam("/uav_detect_landing_region/templ_path", templ_path);
    // this->templ_img_ = cv::imread(templ_path, CV_LOAD_IMAGE_COLOR);
    // if (this->templ_img_.empty()) {
    //     ROS_ERROR("TEMPLATE NOT FOUND");
    //     return;
    // }

    //! svm load or save path
    std::string svm_path = "/home/krishneel/Desktop/mbzirc/data/svm.xml";
    // this->pnh_.getParam("/uav_detect_landing_region/svm_path", svm_path);
    if (svm_path.empty()) {
       ROS_ERROR("NOT SVM DETECTOR PATH. PROVIDE A VALID PATH");
       return;
    }
     
    //! train svm
    bool is_train = true;
    // this->pnh_.getParam("/uav_detect_landing_region/train_detector", is_train);
    if (is_train) {
       std::string object_data_path;
       std::string background_dataset_path;
       this->pnh_.getParam("/uav_detect_landing_region/object_dataset_path",
                           object_data_path);
       this->pnh_.getParam("/uav_detect_landiang_region/object_dataset_path",
                           background_dataset_path);

       object_data_path = "/home/krishneel/Desktop/mbzirc/data/positive.txt";
       background_dataset_path =
          "/home/krishneel/Desktop/mbzirc/data/negative.txt";
       
       this->trainUAVLandingRegionDetector(object_data_path,
                                           background_dataset_path, svm_path);
       
       ROS_INFO("\033[34m-- SVM DETECTOR SUCCESSFULLY TRAINED \033[0m");
    }
    this->svm_->load(static_cast<std::string>(svm_path));
    ROS_INFO("\033[34m-- SVM DETECTOR SUCCESSFULLY LOADED \033[0m");
    
    this->onInit();
}

void UAVLandingRegion::onInit() {
    this->subscribe();
    this->pub_image_ = pnh_.advertise<sensor_msgs::Image>(
       "/image", sizeof(char));
    this->pub_rect_ = pnh_.advertise<jsk_recognition_msgs::Rect>(
       "/rect", sizeof(char));
}

void UAVLandingRegion::subscribe() {
    this->sub_image_.subscribe(this->pnh_, "input_image", 1);
    this->sub_mask_.subscribe(this->pnh_, "input_mask", 1);
    this->sync_ = boost::make_shared<message_filters::Synchronizer<
                                        SyncPolicy> >(100);
    this->sync_->connectInput(this->sub_image_, this->sub_mask_);
    this->sync_->registerCallback(
      boost::bind(
         &UAVLandingRegion::imageCB, this, _1, _2));
}

void UAVLandingRegion::unsubscribe() {
    this->sub_image_.unsubscribe();
    this->sub_mask_.unsubscribe();
}

void UAVLandingRegion::imageCB(
    const sensor_msgs::Image::ConstPtr & image_msg,
    const sensor_msgs::Image::ConstPtr &mask_msg) {
    std::cout << "CALLBK"  << "\n";
    cv::Mat image = this->convertImageToMat(image_msg, "bgr8");
    cv::Mat im_mask = this->convertImageToMat(mask_msg, "mono8");
    if (image.empty() || im_mask.empty()) {
       ROS_ERROR("EMPTY IMAGE. SKIP LANDING SITE DETECTION");
       return;
    }

    cv::Size im_downsize = cv::Size(image.cols/this->down_size_,
                                    image.rows/this->down_size_);
    cv::resize(image, image, im_downsize);
    cv::resize(im_mask, im_mask, im_downsize);
    
    cv::Mat detector_path = im_mask.clone();
    // this->skeletonization(detector_path);

    cv::Mat weight;
    this->slidingWindowDetect(weight, image);
    

    cv::waitKey(5);
    
    cv_bridge::CvImagePtr pub_msg(new cv_bridge::CvImage);
    pub_msg->header = image_msg->header;
    pub_msg->encoding = sensor_msgs::image_encodings::BGR8;
    pub_msg->image = image.clone();
    this->pub_image_.publish(pub_msg);
}

void UAVLandingRegion::traceandDetectLandingMarker(
    const cv::Mat image, const cv::Mat marker, const cv::Size wsize) {
    if (image.empty() || marker.empty() || image.size() != marker.size()) {
        ROS_ERROR("EMPTY INPUT IMAGE FOR DETECTION");
        return;
    }

    // 1 - detect
    // 2 - non_max_suprresion
    // 3 - return bounding box
}

cv::Size UAVLandingRegion::getSlidingWindowSize(
    const cv::Mat marker_img,
    const jsk_msgs::VectorArray projection_matrix) {
    float A[2][2];
    float bv[2];

    // TODO(FIX): check if this can be down during skeleton
    for (int y = 0; y < marker_img.rows; y++) {
       bool is_break = false;
       for (int x = 0; x < marker_img.cols; x++) {
          if (marker_img.at<uchar>(y, x) == 255) {
             int i = y;
             int j = x;
             
             A[0][0] = j * projection_matrix.data.at(8) -
                projection_matrix.data.at(0);
             A[0][1] = j * projection_matrix.data.at(9) -
                projection_matrix.data.at(1);
             A[1][0] = i * projection_matrix.data.at(8) -
                projection_matrix.data.at(4);
             A[1][1] = i * projection_matrix.data.at(9) -
                projection_matrix.data.at(5);
             bv[0] = projection_matrix.data.at(2)*ground_plane_ +
                projection_matrix.data.at(3) - j*projection_matrix.data.at(
                   10)*ground_plane_ - j*projection_matrix.data.at(11);
             bv[1] = projection_matrix.data.at(4)*ground_plane_ +
                projection_matrix.data.at(7) - i*projection_matrix.data.at(
                   10)*ground_plane_ - i*projection_matrix.data.at(11);
             float dominator = A[1][1] * A[0][0] - A[0][1] * A[1][0];

             float x = (A[1][1]*bv[0]-A[0][1]*bv[1]) / dominator;
             float y = (A[0][0]*bv[1]-A[1][0]*bv[0]) / dominator;
             float z = this->ground_plane_;
          }
       }
       if (is_break) {
          break;
       }
    }
}

void UAVLandingRegion::slidingWindowDetect(
    cv::Mat &weight, const cv::Mat image) {
    if (image.empty()) {
        ROS_ERROR("EMPTY INPUT IMAGE FOR SLIDING WINDOW DETECTION");
        return;
    }
    // cv::resize(templ_img_, templ_img_, cv::Size(32, 32));
    const int step_stride = 4;
    // HOGFeatureDescriptor hog;
    // cv::Mat templ_desc = hog.computeHOG(this->templ_img_);


    weight = image.clone();
    cv::Rect r_rect;
    double dist = 100;
#ifdef _OPENMP
#pragma omp parallel for num_threads(8)
#endif
    for (int i = 0; i < image.rows; i += step_stride) {
        for (int j = 0; j < image.cols; j += step_stride) {
           cv::Rect rect = cv::Rect(j, i, 32, 32);
            if (rect.x + rect.width < image.cols &&
                rect.y + rect.height < image.rows) {
                cv::Mat roi = image(rect).clone();
                cv::Mat desc = this->extractFeauture(roi);
                float response = this->svm_->predict(desc);
                if (response == 1) {
                   cv::rectangle(weight, rect, cv::Scalar(0, 255, 0), 1);
                }
            }
        }
    }

    std::string wname = "result";
    cv::namedWindow(wname, cv::WINDOW_NORMAL);
    cv::imshow(wname, weight);
}


void UAVLandingRegion::skeletonization(
    cv::Mat &image) {
    if (image.empty()) {
       ROS_ERROR("--CANNOT THIN EMPTY DATA...");
       return;
    }
    if (image.type() == CV_8UC3) {
       cv::cvtColor(image, image, CV_BGR2GRAY);
    }
    cv::Mat img;
    image.convertTo(img, CV_32F, 1/255.0);
    cv::Mat prev = cv::Mat::zeros(img.size(), CV_32F);
    cv::Mat difference;
    do {
       this->iterativeThinning(img, 0);
       this->iterativeThinning(img, 1);
       cv::absdiff(img, prev, difference);
       img.copyTo(prev);

    } while (cv::countNonZero(difference) > 0);
    image = img.clone();
    img = cv::Mat();
}
   
void UAVLandingRegion::iterativeThinning(
    cv::Mat& img, int iter) {
    if (img.empty()) {
       ROS_ERROR("--CANNOT THIN EMPTY DATA...");
       return;
    }
    cv::Mat marker = cv::Mat::zeros(img.size(), CV_32F);
#ifdef _OPENMP
#pragma omp parallel for collapse(2) num_threads(this->num_threads_)
#endif
    for (int i = 1; i < img.rows-1; i++) {
       for (int j = 1; j < img.cols-1; j++) {
          float val[9] = {};
          int icounter = 0;
          for (int y = -1; y <= 1; y++) {
             for (int x = -1; x <= 1; x++) {
                val[icounter] = img.at<float>(i + y, j + x);
                icounter++;
             }
          }
          int A = ((val[1] == 0 && val[2] == 1) ? ODD : EVEN)
             + ((val[2] == 0 && val[5] == 1) ? ODD : EVEN)
             + ((val[5] == 0 && val[8] == 1) ? ODD : EVEN)
             + ((val[8] == 0 && val[7] == 1) ? ODD : EVEN)
             + ((val[7] == 0 && val[6] == 1) ? ODD : EVEN)
             + ((val[6] == 0 && val[3] == 1) ? ODD : EVEN)
             + ((val[3] == 0 && val[0] == 1) ? ODD : EVEN)
             + ((val[0] == 0 && val[1] == 1) ? ODD : EVEN);
          int B  = val[0] + val[1] + val[2] + val[3]
             + val[5] + val[6] + val[7] + val[8];
          int m1 = iter == EVEN ? (val[1] * val[5] * val[7])
             : (val[1] * val[3] * val[5]);
          int m2 = iter == EVEN ? (val[3] * val[5] * val[7])
             : (val[1] * val[3] * val[7]);
          if (A == 1 && (B >= 2 && B <= 6) && !m1 && !m2) {
             marker.at<float>(i, j) = sizeof(char);
          }
       }
    }
    cv::bitwise_not(marker, marker);
    cv::bitwise_and(img, marker, img);
    marker = cv::Mat();
}

cv::Mat UAVLandingRegion::convertImageToMat(
    const sensor_msgs::Image::ConstPtr &image_msg, std::string encoding) {
    cv_bridge::CvImagePtr cv_ptr;
    try {
       cv_ptr = cv_bridge::toCvCopy(image_msg, encoding);
    } catch (cv_bridge::Exception& e) {
        ROS_ERROR("cv_bridge exception: %s", e.what());
        return cv::Mat();
    }
    return cv_ptr->image.clone();
}

cv::Mat UAVLandingRegion::extractFeauture(
    cv::Mat &image) {
    if (image.empty()) {
      return cv::Mat();
    }
    cv::Mat desc = this->hog_->computeHOG(image);
    return desc;
}

bool UAVLandingRegion::trainTrackDetector(
    const std::string save_path, const std::string dataset_path) {
    if (save_path.empty() || dataset_path.empty()) {
      ROS_ERROR("PROVIDE THE SAVE AND DATASET PATH");
      return false;
    }
    cv::FileStorage fs;
    fs.open(dataset_path, cv::FileStorage::READ);
    if (!fs.isOpened()) {
       ROS_ERROR("CANNOT LOCATE .xml FILE");
       return false;
    }

    cv::FileNode fn = fs["strings"];
    if (fn.type() != cv::FileNode::SEQ) {
       ROS_ERROR("STRING NO A SEQUENCE");
       return false;
    }

    cv::Mat feature_vector;
    for (cv::FileNodeIterator it = fn.begin(); it != fn.end(); ++it) {
       cv::Mat img = cv::imread(static_cast<std::string>(*it), 0);
       if (!img.empty()) {
          cv::Mat feature;
          // extract feature
          feature_vector.push_back(feature);
       }
    }

    fn = fs["labels"];
    
    return true;
}

int main(int argc, char *argv[]) {
    ros::init(argc, argv, "jsk_mbzirc_tasks");
    UAVLandingRegion ulr;
    ros::spin();
    return 0;
}
