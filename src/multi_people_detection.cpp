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
#include <string>
#include <sensor_msgs/PointCloud2.h>
#include <std_msgs/Bool.h>
#include <std_msgs/String.h>
#include "lumyai_navigation_msgs/NavGoalMsg.h"
#include <geometry_msgs/PointStamped.h>
#include <shape_msgs/Plane.h>
#include <pcl17/ros/conversions.h>
#include <pcl17_ros/transforms.h>

#include <pcl17/console/parse.h>
#include <pcl17/point_types.h>
#include <pcl17/visualization/pcl_visualizer.h>    
#include <pcl17/sample_consensus/sac_model_plane.h>
#include <pcl17/people/ground_based_people_detection_app.h>

#include <pcl17/point_types.h>
#include <pcl17/sample_consensus/sac_model_plane.h>
#include <pcl17/sample_consensus/ransac.h>
#include <pcl17/filters/extract_indices.h>
#include <pcl17/segmentation/extract_clusters.h>
#include <pcl17/kdtree/kdtree.h>
#include <pcl17/filters/voxel_grid.h>
#include <pcl17/people/person_cluster.h>
#include <pcl17/people/head_based_subcluster.h>
#include <pcl17/people/person_classifier.h>



template <typename T> std::string tostr(const T& t) { std::ostringstream os; os<<t; return os.str(); }

typedef pcl17::PointXYZRGB PointT;
typedef pcl17::PointCloud<PointT> PointCloud;

using namespace Eigen;

tf::TransformListener* listener;

std::string camera_optical_frame = "/camera_rgb_optical_frame";
std::string robot_camera_frame = "/pan_link";
std::string robot_frame = "/base_link";
std::string world_frame = "/odom";

PointCloud::Ptr cloud_obj (new PointCloud);
bool new_cloud_available_flag = false;
void cloudCallback(const sensor_msgs::PointCloud2::ConstPtr& cloud_in)
{
	pcl17::fromROSMsg(*cloud_in,*cloud_obj);
	new_cloud_available_flag = true;
	ROS_INFO("%d",cloud_obj->size());  		
}

void extractRGBFromPointCloud (boost::shared_ptr<PointCloud> input_cloud, pcl17::PointCloud<pcl17::RGB>::Ptr& output_cloud)
{
  // Extract RGB information from a point cloud and output the corresponding RGB point cloud  
  output_cloud->points.resize(input_cloud->height*input_cloud->width);
  output_cloud->width = input_cloud->width;
  output_cloud->height = input_cloud->height;

  pcl17::RGB rgb_point;
  for (int j = 0; j < input_cloud->width; j++)
  {
    for (int i = 0; i < input_cloud->height; i++)
    { 
      rgb_point.r = (*input_cloud)(j,i).r;
      rgb_point.g = (*input_cloud)(j,i).g;
      rgb_point.b = (*input_cloud)(j,i).b;    
      (*output_cloud)(j,i) = rgb_point; 
    }
  }
}

float getCameraHeight()
{
	  tf::StampedTransform transform;
    try{
      listener->lookupTransform(robot_frame, robot_camera_frame, ros::Time(0), transform);
			return transform.getOrigin().z();
    }
    catch (tf::TransformException ex){
      ROS_ERROR("%s",ex.what());
			ROS_ERROR("Invalid camera height");
    }
		return -10;
}

Eigen::VectorXf getGroundCoeffs()
{
	tf::StampedTransform transform;
	try{
		listener->lookupTransform(robot_frame, camera_optical_frame, ros::Time(0), transform);
	}
	catch (tf::TransformException ex){
		ROS_ERROR("%s",ex.what());
	}	
		
	Eigen::Matrix4f T;
	pcl17_ros::transformAsMatrix(transform,T);

	Eigen::MatrixXf coeffs(1,4); coeffs << 0, 0, 1, 0;
	Eigen::MatrixXf coeffs_out(1,4);
	coeffs_out = coeffs*T;

	Eigen::Vector4f re_co(4); re_co << coeffs_out(0,0), coeffs_out(0,1), coeffs_out(0,2), coeffs_out(0,3);
	return re_co;
}

//get Homogeneous Transform Matrix (4x4)
Eigen::Matrix4f getHomogeneousMatrix(std::string input_frame,std::string des_frame)
{
	tf::StampedTransform transform;
	try{
		listener->lookupTransform(des_frame, input_frame, ros::Time(0), transform);
	}
	catch (tf::TransformException ex){
		ROS_ERROR("%s",ex.what());
	}	
		
	Eigen::Matrix4f T;
	pcl17_ros::transformAsMatrix(transform,T);

	return T;
}

Eigen::Vector3f getFrameOrigin(std::string ref_frame,std::string des_frame)
{
	tf::StampedTransform transform;
	try{
		listener->lookupTransform(des_frame, ref_frame, ros::Time(0), transform);
	}
	catch (tf::TransformException ex){
		ROS_ERROR("%s",ex.what());
	}
	Eigen::Vector3f T;
	T << transform.getOrigin().x(),transform.getOrigin().y(),transform.getOrigin().z();
	return T;	
}

int main(int argc, char **argv)
{
  ros::init(argc, argv, "multi_people_detection");
  ros::NodeHandle n;
  ros::Subscriber cloub_sub = n.subscribe("camera/depth_registered/points", 1, cloudCallback);
	ros::Publisher pp_pub = n.advertise<std_msgs::String>("/people/point", 1000);

  listener = new tf::TransformListener();

  // Algorithm parameters:
  float voxel_size = 0.06;
  float min_confidence = -1.5;
  float min_height = 1.3;
  float max_height = 2.3;
//===================================================================================
  // set default values for optional parameters:
  int min_points = 30;     // this value is adapted to the voxel size in method "compute"
  int max_points = 5000;   // this value is adapted to the voxel size in method "compute"
  float heads_minimum_distance = 0.3;
//===================================================================================

	std::string svm_filename = "/home/skuba/skuba_athome/people_detection/trainedLinearSVMForPeopleDetectionWithHOG.yaml";
	Eigen::Matrix3f rgb_intrinsics_matrix;
  rgb_intrinsics_matrix << 131.25, 0.0, 79.5, 0.0, 131.25, 59.5, 0.0, 0.0, 1.0;//525, 0.0, 319.5, 0.0, 525, 239.5, 0.0, 0.0, 1.0; // Kinect RGB camera intrinsics

  // Initialize classifier for people detection:  
  pcl17::people::PersonClassifier<pcl17::RGB> person_classifier;
  person_classifier.loadSVMFromFile(svm_filename);   // load trained SVM

  // Initialize new viewer:
  pcl17::visualization::PCLVisualizer viewer("People viewer");          // viewer initialization
  viewer.setCameraPosition(0,0,-2,0,-1,0,0);

	ros::Rate loop_rate(10);
  while (ros::ok())
  {
		if(new_cloud_available_flag)
		{
			VectorXf ground_coeffs = getGroundCoeffs();
			std::cout << "Ground plane: " << ground_coeffs(0) << " " << ground_coeffs(1) << " " << ground_coeffs(2) << " " << ground_coeffs(3) << std::endl;

			PointCloud::Ptr cloud (new PointCloud);
			pcl17::copyPointCloud<PointT, PointT>(*cloud_obj, *cloud);
			new_cloud_available_flag = false;

      // Perform people detection on the new cloud:
      std::vector<pcl17::people::PersonCluster<PointT> > clusters;   // vector containing persons clusters

	    // Adapt thresholds for clusters points number to the voxel size:
  	  max_points = int(float(max_points) * std::pow(0.06/voxel_size, 2));
  	  if (voxel_size > 0.06)
  	  min_points = int(float(min_points) * std::pow(0.06/voxel_size, 2));

  		// Fill rgb image:
  		pcl17::PointCloud<pcl17::RGB>::Ptr rgb_image(new pcl17::PointCloud<pcl17::RGB>);
  		extractRGBFromPointCloud(cloud, rgb_image);          // fill RGB pointcloud

  		// Voxel grid filtering:
  		PointCloud::Ptr cloud_filtered(new PointCloud);
  		pcl17::VoxelGrid<PointT> voxel_grid_filter_object;
  		voxel_grid_filter_object.setInputCloud(cloud);
  		voxel_grid_filter_object.setLeafSize (voxel_size, voxel_size, voxel_size);
  		voxel_grid_filter_object.filter (*cloud_filtered);

  		// Ground removal and update:
  		pcl17::IndicesPtr inliers(new std::vector<int>);
  		pcl17::SampleConsensusModelPlane<PointT>::Ptr ground_model(new pcl17::SampleConsensusModelPlane<PointT>(cloud_filtered));
  		ground_model->selectWithinDistance(ground_coeffs, voxel_size*3.5, *inliers);
  		PointCloud::Ptr no_ground_cloud(new PointCloud);
  		pcl17::ExtractIndices<PointT> extract;
  		extract.setInputCloud(cloud_filtered);
  		extract.setIndices(inliers);
  		extract.setNegative(true);
  		extract.filter(*no_ground_cloud);

  		// Euclidean Clustering:
  		std::vector<pcl17::PointIndices> cluster_indices;
  		typename pcl17::search::KdTree<PointT>::Ptr tree (new pcl17::search::KdTree<PointT>);
  		tree->setInputCloud(no_ground_cloud);
  		pcl17::EuclideanClusterExtraction<PointT> ec;
  		ec.setClusterTolerance(2 * 0.06);
  		ec.setMinClusterSize(min_points);
		  ec.setMaxClusterSize(max_points);
		  ec.setSearchMethod(tree);
  		ec.setInputCloud(no_ground_cloud);
  		ec.extract(cluster_indices);

  		// Head based sub-clustering //
  		pcl17::people::HeadBasedSubclustering<PointT> subclustering;
  		subclustering.setInputCloud(no_ground_cloud);
  		subclustering.setGround(ground_coeffs);
  		subclustering.setInitialClusters(cluster_indices);
  		subclustering.setHeightLimits(min_height, max_height);
  		subclustering.setMinimumDistanceBetweenHeads(heads_minimum_distance);
  		subclustering.setSensorPortraitOrientation(false);
  		subclustering.subcluster(clusters);

      // Draw cloud and people bounding boxes in the viewer:
      viewer.removeAllPointClouds();
      viewer.removeAllShapes();
      pcl17::visualization::PointCloudColorHandlerRGBField<PointT> rgb(cloud);
      viewer.addPointCloud<PointT> (cloud, rgb, "input_cloud");

			unsigned int k = 0;
			Matrix4f cam2botTF = getHomogeneousMatrix(camera_optical_frame,robot_frame);
			float min_distsq = 1000.0f;
			MatrixXf tmpB;
			bool isFound = false;
			for(typename std::vector<pcl17::people::PersonCluster<PointT> >::iterator it = clusters.begin(); it != clusters.end(); ++it)
			{
				//Evaluate confidence for the current PersonCluster:
				Eigen::Vector3f centroid = rgb_intrinsics_matrix * (it->getTCenter());
				centroid /= centroid(2);
				Eigen::Vector3f top = rgb_intrinsics_matrix * (it->getTTop());
				top /= top(2);
				Eigen::Vector3f bottom = rgb_intrinsics_matrix * (it->getTBottom());
				bottom /= bottom(2);
				it->setPersonConfidence(person_classifier.evaluate(rgb_image, bottom, top, centroid, rgb_intrinsics_matrix, false));

				if(it->getPersonConfidence() > min_confidence)
				{
					Vector3f Tcc = it->getTCenter();
					MatrixXf tmp(4,1); tmp << Tcc(0),Tcc(1),Tcc(2),1;
					MatrixXf tmpC = cam2botTF*tmp;
					float distsq = pow(tmpC(0,0),2)+pow(tmpC(1,0),2);
					if(distsq < min_distsq)
					{
						min_distsq = distsq;
						tmpB = tmpC;
						isFound = true;
					}
					it->drawTBoundingBox(viewer, k);
					k++;
				}
			}

			if(isFound)
			{
				//std::cout << "pp pose<<<<<<<<<" << tmpB << std::endl;
				char* str = new char[30];

				//publish to Main_Control
				float angle = atan2(tmpB(1,0),tmpB(0,0));
				Vector3f extend;
				extend(0) = tmpB(0,0) - 0.5*cos(angle);
				extend(1) = tmpB(1,0) - 0.5*sin(angle);
				sprintf(str,"%f,%f,%f",extend(0), extend(1), tmpB(2,0));
				std_msgs::String msg;
				std::stringstream ss;
				ss << str;
				msg.data = ss.str();
				ROS_INFO("%s", msg.data.c_str());
				pp_pub.publish(msg);
			}

			loop_rate.sleep();	
		}
	viewer.spinOnce();
	ros::spinOnce();
	}
  return 0;
}


/*void doTracking(std::vector<Eigen::Vector3f> pp_center_list, std::vector<float> pp_height_list)
{
	Matrix4f bot2worldTF = getHomogeneousMatrix(robot_frame,world_frame);
	Matrix4f cam2botTF = getHomogeneousMatrix(camera_optical_frame,robot_frame);
	Matrix4f cam2worldTF = bot2worldTF*cam2botTF;
	static MatrixXf pp_center_world_last(4,1);
	static bool init = false;
	static bool init_height = false;
	static float pp_height_last = 0.5;
	if(!init)
	{
		MatrixXf init_point_bot(4,1); init_point_bot << 1.0,0.0,1.0,1.0;
		pp_center_world_last = bot2worldTF*init_point_bot;
		init = true;
	}

	//std::cout << pp_center_world_last << "-------------" << std::endl;
	float min_dist = 1000.0f;
	
	MatrixXf pp_center_world = pp_center_world_last;
	float pp_height = pp_height_last;
	for(int k = 0; k<pp_center_list.size(); k++)
	{
		float pp_height_tmp = pp_height_list[k];
		MatrixXf pp_center_cam(4,1); pp_center_cam << pp_center_list[k](0),pp_center_list[k](1),pp_center_list[k](2), 1;
		MatrixXf pp_center_world_tmp = cam2worldTF*pp_center_cam;
		float dist = (pp_center_world_tmp - pp_center_world_last).squaredNorm();
		if(dist < 0.3f)
		{
			if(!init_height)
			{
				pp_height_last = pp_height_tmp;
				init_height = true;	
			}
			
			if(init_height && fabs(pp_height_tmp - pp_height_last) < 0.1 && dist<min_dist)
			{	
				min_dist = dist;
				pp_center_world = pp_center_world_tmp;
				pp_height = pp_height_tmp;
			}
		}
	}

	std::cout << "people pose:\n" << pp_center_world << "\nwith dis: " << min_dist << std::endl;

	target_visualization(pp_center_world, pp_height);
	pp_center_world_last = pp_center_world;
	pp_height_last = pp_height;
}*/

/*Eigen::VectorXf ground_coeffs(4,1);
void groundcoeffsCallback(const shape_msgs::Plane::ConstPtr& coeffs_in)
{
	tf::StampedTransform transform;
	try{
		listener->lookupTransform(robot_camera_frame, camera_optical_frame, ros::Time(0), transform);
	}
	catch (tf::TransformException ex){
		ROS_ERROR("%s",ex.what());
	}	
		
	Eigen::Matrix4f T;
	pcl17_ros::transformAsMatrix(transform,T);

	Eigen::MatrixXf coeffs(1,4); coeffs << coeffs_in->coef[0], coeffs_in->coef[1], coeffs_in->coef[2], coeffs_in->coef[3];
	Eigen::MatrixXf coeffs_out(1,4);
	coeffs_out = coeffs*T;
	ground_coeffs(0) = coeffs_out(0,0);
	ground_coeffs(1) = coeffs_out(0,1);
	ground_coeffs(2) = coeffs_out(0,2);
	ground_coeffs(3) = coeffs_out(0,3);
	std::cout << "Ground plane: " << ground_coeffs(0) << " " << ground_coeffs(1) << " " << ground_coeffs(2) << " " << ground_coeffs(3) << std::endl;
}*/

/*void doTracking(std::vector<Eigen::Vector3f> pp_center_list, std::vector<float> pp_height_list)
{
	static MatrixXf pp_center_world_last(4,1);
	static bool init = false;

//-------------fill in homogeneous matrix-----------------//
	Matrix4f bot2worldTF = getHomogeneousMatrix(robot_frame,world_frame);
	Matrix4f cam2botTF = getHomogeneousMatrix(camera_optical_frame,robot_frame);
	Matrix4f cam2worldTF = bot2worldTF*cam2botTF;
//--------------------------------------------------------//

	if(!init)
	{
		MatrixXf init_point_bot(4,1); init_point_bot << 1.0,0.0,1.0,1.0;
		pp_center_world_last = bot2worldTF*init_point_bot;
		init = true;
	}

	float min_dist = 1000.0f;
	MatrixXf pp_center_world = pp_center_world_last;
	for(int k = 0; k<pp_center_list.size(); k++)
	{
		MatrixXf pp_center_cam(4,1); pp_center_cam << pp_center_list[k](0),pp_center_list[k](1),pp_center_list[k](2), 1;
		MatrixXf pp_center_world_tmp = cam2worldTF*pp_center_cam;
		float dist = (pp_center_world_tmp - pp_center_world_last).squaredNorm();
		if(dist < 0.3f && dist<min_dist)
		{
				min_dist = dist;
				pp_center_world = pp_center_world_tmp;
		}
	}

	std::cout << "people pose:\n" << pp_center_world << "\nwith dis: " << min_dist << std::endl;

	target_visualization(pp_center_world, 1);
	pp_center_world_last = pp_center_world;
}*/