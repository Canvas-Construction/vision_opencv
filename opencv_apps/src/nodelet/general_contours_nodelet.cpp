/*********************************************************************
* Software License Agreement (BSD License)
*
*  Copyright (c) 2014, Kei Okada.
*  All rights reserved.
* 
*  Redistribution and use in source and binary forms, with or without
*  modification, are permitted provided that the following conditions
*  are met:
* 
*   * Redistributions of source code must retain the above copyright
*     notice, this list of conditions and the following disclaimer.
*   * Redistributions in binary form must reproduce the above
*     copyright notice, this list of conditions and the following
*     disclaimer in the documentation and/or other materials provided
*     with the distribution.
*   * Neither the name of the Kei Okada nor the names of its
*     contributors may be used to endorse or promote products derived
*     from this software without specific prior written permission.
* 
*  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
*  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
*  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
*  FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
*  COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
*  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
*  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
*  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
*  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
*  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
*  ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
*  POSSIBILITY OF SUCH DAMAGE.
*********************************************************************/

// https://github.com/Itseez/opencv/blob/2.4/samples/cpp/tutorial_code/ShapeDescriptors/generalContours_demo2.cpp
/**
 * @function generalContours_demo2.cpp
 * @brief Demo code to obtain ellipses and rotated rectangles that contain detected contours
 * @author OpenCV team
 */

#include <ros/ros.h>
#include <nodelet/nodelet.h>
#include <image_transport/image_transport.h>
#include <sensor_msgs/image_encodings.h>
#include <cv_bridge/cv_bridge.h>

#include <opencv2/highgui/highgui.hpp>
#include <opencv2/imgproc/imgproc.hpp>

#include <dynamic_reconfigure/server.h>
#include "opencv_apps/GeneralContoursConfig.h"
#include "opencv_apps/RotatedRect.h"
#include "opencv_apps/RotatedRectArray.h"
#include "opencv_apps/RotatedRectArrayStamped.h"

namespace general_contours {
class GeneralContoursNodelet : public nodelet::Nodelet
{
  image_transport::Publisher img_pub_;
  image_transport::Subscriber img_sub_;
  image_transport::CameraSubscriber cam_sub_;
  ros::Publisher rects_pub_, ellipses_pub_;

  boost::shared_ptr<image_transport::ImageTransport> it_;
  ros::NodeHandle nh_, local_nh_;

  general_contours::GeneralContoursConfig config_;
  dynamic_reconfigure::Server<general_contours::GeneralContoursConfig> srv;

  bool debug_view_;
  int subscriber_count_;
  ros::Time prev_stamp_;

  int threshold_;

  std::string window_name_;
  static bool need_config_update_;

  void reconfigureCallback(general_contours::GeneralContoursConfig &new_config, uint32_t level)
  {
    config_ = new_config;
    threshold_ = config_.threshold;
    if (subscriber_count_)
    { // @todo Could do this without an interruption at some point.
      unsubscribe();
      subscribe();
    }
  }

  const std::string &frameWithDefault(const std::string &frame, const std::string &image_frame)
  {
    if (frame.empty())
      return image_frame;
    return frame;
  }

  void imageCallbackWithInfo(const sensor_msgs::ImageConstPtr& msg, const sensor_msgs::CameraInfoConstPtr& cam_info)
  {
    do_work(msg, cam_info->header.frame_id);
  }
  
  void imageCallback(const sensor_msgs::ImageConstPtr& msg)
  {
    do_work(msg, msg->header.frame_id);
  }

  static void trackbarCallback( int, void* )
  {
    need_config_update_ = true;
  }

  void do_work(const sensor_msgs::ImageConstPtr& msg, const std::string input_frame_from_msg)
  {
    // Work on the image.
    try
    {
      // Convert the image into something opencv can handle.
      cv::Mat frame = cv_bridge::toCvShare(msg, msg->encoding)->image;

      // Messages
      opencv_apps::RotatedRectArrayStamped rects_msg, ellipses_msg;
      rects_msg.header = msg->header;
      ellipses_msg.header = msg->header;

      // Do the work
      cv::Mat src_gray;

      /// Convert image to gray and blur it
      cv::cvtColor( frame, src_gray, cv::COLOR_RGB2GRAY );
      cv::blur( src_gray, src_gray, cv::Size(3,3) );

      /// Create window
      if( debug_view_) {
        cv::namedWindow( window_name_, cv::WINDOW_AUTOSIZE );
      }

      cv::Mat threshold_output;
      int max_thresh = 255;
      std::vector<std::vector<cv::Point> > contours;
      std::vector<cv::Vec4i> hierarchy;
      cv::RNG rng(12345);

      /// Detect edges using Threshold
      cv::threshold( src_gray, threshold_output, threshold_, 255, cv::THRESH_BINARY );

      /// Find contours
      cv::findContours( threshold_output, contours, hierarchy, CV_RETR_TREE, CV_CHAIN_APPROX_SIMPLE, cv::Point(0, 0) );

      /// Find the rotated rectangles and ellipses for each contour
      cv::vector<cv::RotatedRect> minRect( contours.size() );
      cv::vector<cv::RotatedRect> minEllipse( contours.size() );

      for( size_t i = 0; i < contours.size(); i++ )
      { minRect[i] = cv::minAreaRect( cv::Mat(contours[i]) );
        if( contours[i].size() > 5 )
        { minEllipse[i] = cv::fitEllipse( cv::Mat(contours[i]) ); }
      }

      /// Draw contours + rotated rects + ellipses
      cv::Mat drawing = cv::Mat::zeros( threshold_output.size(), CV_8UC3 );
      for( size_t i = 0; i< contours.size(); i++ )
      {
        cv::Scalar color = cv::Scalar( rng.uniform(0, 255), rng.uniform(0,255), rng.uniform(0,255) );
        // contour
        cv::drawContours( drawing, contours, (int)i, color, 1, 8, std::vector<cv::Vec4i>(), 0, cv::Point() );
        // ellipse
        cv::ellipse( drawing, minEllipse[i], color, 2, 8 );
        // rotated rectangle
        cv::Point2f rect_points[4]; minRect[i].points( rect_points );
        for( int j = 0; j < 4; j++ )
          cv::line( drawing, rect_points[j], rect_points[(j+1)%4], color, 1, 8 );

        opencv_apps::RotatedRect rect_msg;
        opencv_apps::Point2D rect_center;
        opencv_apps::Size rect_size;
        rect_center.x = minRect[i].center.x;
        rect_center.y = minRect[i].center.y;
        rect_size.width = minRect[i].size.width;
        rect_size.height = minRect[i].size.height;
        rect_msg.center = rect_center;
        rect_msg.size = rect_size;
        rect_msg.angle = minRect[i].angle;
        opencv_apps::RotatedRect ellipse_msg;
        opencv_apps::Point2D ellipse_center;
        opencv_apps::Size ellipse_size;
        ellipse_center.x = minEllipse[i].center.x;
        ellipse_center.y = minEllipse[i].center.y;
        ellipse_size.width = minEllipse[i].size.width;
        ellipse_size.height = minEllipse[i].size.height;
        ellipse_msg.center = ellipse_center;
        ellipse_msg.size = ellipse_size;
        ellipse_msg.angle = minEllipse[i].angle;

        rects_msg.rects.push_back(rect_msg);
        ellipses_msg.rects.push_back(ellipse_msg);
      }

      /// Create a Trackbar for user to enter threshold
      if( debug_view_) {
        if (need_config_update_) {
          config_.threshold = threshold_;
          srv.updateConfig(config_);
          need_config_update_ = false;
        }
        cv::createTrackbar( "Threshold:", window_name_, &threshold_, max_thresh, trackbarCallback);

        cv::imshow( window_name_, drawing );
        int c = cv::waitKey(1);
      }

      // Publish the image.
      sensor_msgs::Image::Ptr out_img = cv_bridge::CvImage(msg->header, sensor_msgs::image_encodings::BGR8, drawing).toImageMsg();
      img_pub_.publish(out_img);
      rects_pub_.publish(rects_msg);
      ellipses_pub_.publish(ellipses_msg);
    }
    catch (cv::Exception &e)
    {
      NODELET_ERROR("Image processing error: %s %s %s %i", e.err.c_str(), e.func.c_str(), e.file.c_str(), e.line);
    }

    prev_stamp_ = msg->header.stamp;
  }

  void subscribe()
  {
    NODELET_DEBUG("Subscribing to image topic.");
    if (config_.use_camera_info)
      cam_sub_ = it_->subscribeCamera("image", 3, &GeneralContoursNodelet::imageCallbackWithInfo, this);
    else
      img_sub_ = it_->subscribe("image", 3, &GeneralContoursNodelet::imageCallback, this);
  }

  void unsubscribe()
  {
    NODELET_DEBUG("Unsubscribing from image topic.");
    img_sub_.shutdown();
    cam_sub_.shutdown();
  }

  void img_connectCb(const image_transport::SingleSubscriberPublisher& ssp)
  {
    if (subscriber_count_++ == 0) {
      subscribe();
    }
  }

  void img_disconnectCb(const image_transport::SingleSubscriberPublisher&)
  {
    subscriber_count_--;
    if (subscriber_count_ == 0) {
      unsubscribe();
    }
  }

  void msg_connectCb(const ros::SingleSubscriberPublisher& ssp)
  {
    if (subscriber_count_++ == 0) {
      subscribe();
    }
  }

  void msg_disconnectCb(const ros::SingleSubscriberPublisher&)
  {
    subscriber_count_--;
    if (subscriber_count_ == 0) {
      unsubscribe();
    }
  }

public:
  virtual void onInit()
  {
    nh_ = getNodeHandle();
    it_ = boost::shared_ptr<image_transport::ImageTransport>(new image_transport::ImageTransport(nh_));
    local_nh_ = ros::NodeHandle("~");

    local_nh_.param("debug_view", debug_view_, false);
    subscriber_count_ = 0;
    prev_stamp_ = ros::Time(0, 0);
    
    window_name_ = "Contours";
    threshold_ = 100;

    image_transport::SubscriberStatusCallback img_connect_cb    = boost::bind(&GeneralContoursNodelet::img_connectCb, this, _1);
    image_transport::SubscriberStatusCallback img_disconnect_cb = boost::bind(&GeneralContoursNodelet::img_disconnectCb, this, _1);
    ros::SubscriberStatusCallback msg_connect_cb    = boost::bind(&GeneralContoursNodelet::msg_connectCb, this, _1);
    ros::SubscriberStatusCallback msg_disconnect_cb = boost::bind(&GeneralContoursNodelet::msg_disconnectCb, this, _1);
    img_pub_ = image_transport::ImageTransport(local_nh_).advertise("image", 1, img_connect_cb, img_disconnect_cb);
    rects_pub_ = local_nh_.advertise<opencv_apps::RotatedRectArrayStamped>("rectangles", 1, msg_connect_cb, msg_disconnect_cb);
    ellipses_pub_ = local_nh_.advertise<opencv_apps::RotatedRectArrayStamped>("ellipses", 1, msg_connect_cb, msg_disconnect_cb);
        
    if( debug_view_ ) {
      subscriber_count_++;
    }

    dynamic_reconfigure::Server<general_contours::GeneralContoursConfig>::CallbackType f =
      boost::bind(&GeneralContoursNodelet::reconfigureCallback, this, _1, _2);
    srv.setCallback(f);
  }
};
bool GeneralContoursNodelet::need_config_update_ = false;
}

#include <pluginlib/class_list_macros.h>
PLUGINLIB_EXPORT_CLASS(general_contours::GeneralContoursNodelet, nodelet::Nodelet);