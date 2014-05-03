/*
 * Software License Agreement (BSD License)
 *
 * Point Cloud Library (PCL) - www.pointclouds.org
 * Copyright (c) 2013-, Open Perception, Inc.
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 * * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following
 * disclaimer in the documentation and/or other materials provided
 * with the distribution.
 * * Neither the name of the copyright holder(s) nor the names of its
 * contributors may be used to endorse or promote products derived
 * from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * main_ground_based_people_detection_app.cpp
 * Created on: Nov 30, 2012
 * Author: Matteo Munaro
 *
 * Example file for performing people detection on a Kinect live stream.
 * As a first step, the ground is manually initialized, then people detection is performed with the GroundBasedPeopleDetectionApp class,
 * which implements the people detection algorithm described here:
 * M. Munaro, F. Basso and E. Menegatti,
 * Tracking people within groups with RGB-D data,
 * In Proceedings of the International Conference on Intelligent Robots and Systems (IROS) 2012, Vilamoura (Portugal), 2012.
 */

#include <ros/ros.h>
#include <tf/transform_listener.h>
#include <sensor_msgs/PointCloud2.h>
#include <pcl17/ros/conversions.h>
#include <pcl17_ros/transforms.h>

#include <pcl17/console/parse.h>
#include <pcl17/point_types.h>
#include <pcl17/visualization/pcl_visualizer.h>    
#include <pcl17/sample_consensus/sac_model_plane.h>
#include <pcl17/people/ground_based_people_detection_app.h>

typedef pcl17::PointXYZRGBA PointT;
typedef pcl17::PointCloud<PointT> PointCloudT;

std::string robot_frame = "/camera_depth_optical_frame";

tf::TransformListener* listener;

// PCL viewer //
pcl17::visualization::PCLVisualizer viewer("PCL Viewer");

struct callback_args{
  // structure used to pass arguments to the callback function
  PointCloudT::Ptr clicked_points_3d;
  pcl17::visualization::PCLVisualizer::Ptr viewerPtr;
};

PointCloudT cloud_obj;
bool new_cloud_available_flag = false;
void cloudCallback(const sensor_msgs::PointCloud2::ConstPtr& cloud_in)
{
	pcl17::fromROSMsg(*cloud_in,cloud_obj);
	new_cloud_available_flag = true;
	ROS_INFO("%d",cloud_obj.size()); 		
}

/*void cloudCallback(const sensor_msgs::PointCloud2::ConstPtr& cloud_in)
{ 		
  	try{
	  	PointCloudT::Ptr cloud (new PointCloudT);
	  	pcl17::fromROSMsg(*cloud_in,*cloud);
	  	listener->waitForTransform(robot_frame, cloud_in->header.frame_id, cloud_in->header.stamp, ros::Duration(1.0));
	  	pcl17_ros::transformPointCloud(robot_frame, *cloud, cloud_obj, *listener);
  		new_cloud_available_flag = true;
		ROS_INFO("%d",cloud_obj.size()); 
  	}
  	catch(tf::TransformException& ex){
  		ROS_ERROR("Received an exception trying to transform a point from %s to %s: %s", cloud_in->header.frame_id.c_str(),robot_frame.c_str(),ex.what());
  	}
}*/

void pp_callback (const pcl17::visualization::PointPickingEvent& event, void* args)
{
  struct callback_args* data = (struct callback_args *)args;
  if (event.getPointIndex () == -1)
    return;
  PointT current_point;
  event.getPoint(current_point.x, current_point.y, current_point.z);
  data->clicked_points_3d->points.push_back(current_point);
  // Draw clicked points in red:
  pcl17::visualization::PointCloudColorHandlerCustom<PointT> red (data->clicked_points_3d, 255, 0, 0);
  data->viewerPtr->removePointCloud("clicked_points");
  data->viewerPtr->addPointCloud(data->clicked_points_3d, red, "clicked_points");
  data->viewerPtr->setPointCloudRenderingProperties(pcl17::visualization::PCL17_VISUALIZER_POINT_SIZE, 10, "clicked_points");
  std::cout << current_point.x << " " << current_point.y << " " << current_point.z << std::endl;
}

int main (int argc, char** argv)
{
	ros::init(argc, argv, "people_detection");
	ros::NodeHandle n;
	ros::Subscriber	cloub_sub = n.subscribe("/camera/depth_registered/points", 1, cloudCallback);
listener = new tf::TransformListener();

  // Algorithm parameters:
  float voxel_size = 0.06;
  float min_confidence = -1.5;
  float min_height = 1.3;
  float max_height = 2.3;
  std::string svm_filename = "/home/skuba/skuba_athome/people_detection/trainedLinearSVMForPeopleDetectionWithHOG.yaml";


  Eigen::Matrix3f rgb_intrinsics_matrix;
  rgb_intrinsics_matrix << 131.25, 0.0, 79.5, 0.0, 131.25, 59.5, 0.0, 0.0, 1.0;//525, 0.0, 319.5, 0.0, 525, 239.5, 0.0, 0.0, 1.0; // Kinect RGB camera intrinsics

  PointCloudT::Ptr cloud (new PointCloudT);

  // Wait for the first frame:
	
  while(!new_cloud_available_flag) 
  {  
		ros::spinOnce();
    boost::this_thread::sleep(boost::posix_time::milliseconds(1));
  }
  pcl17::copyPointCloud<PointT, PointT>(cloud_obj, *cloud);
  new_cloud_available_flag = false;

  // Initialize classifier for people detection:  
  pcl17::people::PersonClassifier<pcl17::RGB> person_classifier;
  person_classifier.loadSVMFromFile(svm_filename);   // load trained SVM

  // Ground initialization:
  // Display pointcloud:
  pcl17::visualization::PointCloudColorHandlerRGBField<PointT> rgb(cloud);
  viewer.addPointCloud<PointT> (cloud, rgb, "input_cloud");
  viewer.setCameraPosition(0,0,-2,0,-1,0,0);

  // Add point picking callback to viewer:
  struct callback_args cb_args;
  PointCloudT::Ptr clicked_points_3d (new PointCloudT);
  cb_args.clicked_points_3d = clicked_points_3d;
  cb_args.viewerPtr = pcl17::visualization::PCLVisualizer::Ptr(&viewer);
  viewer.registerPointPickingCallback (pp_callback, (void*)&cb_args);
  std::cout << "Shift+click on three floor points, then press 'Q'..." << std::endl;

  // Spin until 'Q' is pressed:
  viewer.spin();
	
  std::cout << "done." << std::endl;

  // Ground plane estimation:
  Eigen::VectorXf ground_coeffs;
  ground_coeffs.resize(4);
  std::vector<int> clicked_points_indices;
  for (unsigned int i = 0; i < clicked_points_3d->points.size(); i++)
    clicked_points_indices.push_back(i);
  pcl17::SampleConsensusModelPlane<PointT> model_plane(clicked_points_3d);
  model_plane.computeModelCoefficients(clicked_points_indices,ground_coeffs);
  std::cout << "Ground plane: " << ground_coeffs(0) << " " << ground_coeffs(1) << " " << ground_coeffs(2) << " " << ground_coeffs(3) << std::endl;
	//ground_coeffs << -0.021013, -0.999772, 0.003708, 1.03842;/////////////////////////
  // Initialize new viewer:
  pcl17::visualization::PCLVisualizer viewer("PCL Viewer");          // viewer initialization
  viewer.setCameraPosition(0,0,-2,0,-1,0,0);
//viewer.setCameraPosition(2,0,0,0,0,1,0);

  // People detection main loop:
  pcl17::people::GroundBasedPeopleDetectionApp<PointT> people_detector;    // people detection object
  people_detector.setVoxelSize(voxel_size);                        // set the voxel size
  people_detector.setIntrinsics(rgb_intrinsics_matrix);            // set RGB camera intrinsic parameters
  people_detector.setClassifier(person_classifier);                // set person classifier
  people_detector.setHeightLimits(min_height, max_height);         // set person classifier
//  people_detector.setSensorPortraitOrientation(true);             // set sensor orientation to vertical

  // For timing:
  static unsigned count = 0;
  static double last = pcl17::getTime ();

  while (!viewer.wasStopped())
  {
		ros::spinOnce();
    if (new_cloud_available_flag)    // if a new cloud is available
    {
      // Make the "cloud" pointer point to the new cloud:
      pcl17::copyPointCloud<PointT, PointT>(cloud_obj, *cloud);
      new_cloud_available_flag = false;

      // Perform people detection on the new cloud:
      std::vector<pcl17::people::PersonCluster<PointT> > clusters;   // vector containing persons clusters
      people_detector.setInputCloud(cloud);
      people_detector.setGround(ground_coeffs);                    // set floor coefficients
      people_detector.compute(clusters);                           // perform people detection

      ground_coeffs = people_detector.getGround();                 // get updated floor coefficients
	std::cout << "Ground plane: " << ground_coeffs(0) << " " << ground_coeffs(1) << " " << ground_coeffs(2) << " " << ground_coeffs(3) << std::endl;

      // Draw cloud and people bounding boxes in the viewer:
      viewer.removeAllPointClouds();
      viewer.removeAllShapes();
      pcl17::visualization::PointCloudColorHandlerRGBField<PointT> rgb(cloud);
      viewer.addPointCloud<PointT> (cloud, rgb, "input_cloud");
      unsigned int k = 0;
      for(std::vector<pcl17::people::PersonCluster<PointT> >::iterator it = clusters.begin(); it != clusters.end(); ++it)
      {
        if(it->getPersonConfidence() > min_confidence)             // draw only people with confidence above a threshold
        {
          // draw theoretical person bounding box in the PCL viewer:
          it->drawTBoundingBox(viewer, k);
          k++;
        }
      }
      std::cout << k << " people found" << std::endl;
      viewer.spinOnce();

      // For timing:
      if (++count == 30)
      {
        double now = pcl17::getTime ();
        std::cout << "Average framerate: " << double(count)/double(now - last) << " Hz" <<  std::endl;
        count = 0;
        last = now;
      }
    }
  }

  return 0;
}
