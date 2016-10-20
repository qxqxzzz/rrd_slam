// ROS node: dataset_gt version for "dataset" node with ground truth depth
// support
//
// New option:
//
//   _depthFiles:=<folder>/depthlist.txt
//
// Author: Jae-Hak Kim <jaehak.kim@adelaide.edu.au>
// Copyright@2015, University of Adelaide
//------------------------------------------------------------------------------
// HISTORY
//------------------------------------------------------------------------------
// 2015-02-11 Modified from main_on_images.cpp by Jae-Hak Kim
//------------------------------------------------------------------------------
/**
* This file is part of LSD-SLAM.
*
* Copyright 2013 Jakob Engel <engelj at in dot tum dot de> (Technical University of Munich)
* For more information see <http://vision.in.tum.de/lsdslam> 
*
* LSD-SLAM is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* LSD-SLAM is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with LSD-SLAM. If not, see <http://www.gnu.org/licenses/>.
*/

#include "LiveSLAMWrapper.h"

#include <boost/thread.hpp>
#include "util/settings.h"
#include "util/globalFuncs.h"
#include "SlamSystem.h"

#include <sstream>
#include <fstream>
#include <dirent.h>
#include <algorithm>

#include "IOWrapper/ROS/ROSOutput3DWrapper.h"
#include "IOWrapper/ROS/rosReconfigure.h"

#include "util/Undistorter.h"
#include <ros/package.h>

#include "opencv2/opencv.hpp"

#include <thread>
#include <X11/Xlib.h>

std::string &ltrim(std::string &s) {
        s.erase(s.begin(), std::find_if(s.begin(), s.end(), std::not1(std::ptr_fun<int, int>(std::isspace))));
        return s;
}
std::string &rtrim(std::string &s) {
        s.erase(std::find_if(s.rbegin(), s.rend(), std::not1(std::ptr_fun<int, int>(std::isspace))).base(), s.end());
        return s;
}
std::string &trim(std::string &s) {
        return ltrim(rtrim(s));
}
int getdir (std::string dir, std::vector<std::string> &files)
{
    DIR *dp;
    struct dirent *dirp;
    if((dp  = opendir(dir.c_str())) == NULL)
    {
        return -1;
    }

    while ((dirp = readdir(dp)) != NULL) {
    	std::string name = std::string(dirp->d_name);

    	if(name != "." && name != "..")
    		files.push_back(name);
    }
    closedir(dp);


    std::sort(files.begin(), files.end());

    if(dir.at( dir.length() - 1 ) != '/') dir = dir+"/";
	for(unsigned int i=0;i<files.size();i++)
	{
		if(files[i].at(0) != '/')
			files[i] = dir + files[i];
	}

    return files.size();
}

int getFile (std::string source, std::vector<std::string> &files)
{
	std::ifstream f(source.c_str());

	if(f.good() && f.is_open())
	{
		while(!f.eof())
		{
			std::string l;
			std::getline(f,l);

			l = trim(l);

			if(l == "" || l[0] == '#')
				continue;

			files.push_back(l);
		}

		f.close();

		size_t sp = source.find_last_of('/');
		std::string prefix;
		if(sp == std::string::npos)
			prefix = "";
		else
			prefix = source.substr(0,sp);

		for(unsigned int i=0;i<files.size();i++)
		{
			if(files[i].at(0) != '/')
				files[i] = prefix + "/" + files[i];
		}

		return (int)files.size();
	}
	else
	{
		f.close();
		return -1;
	}

}


using namespace lsd_slam;
int main( int argc, char** argv )
{
	ros::init(argc, argv, "LSD_SLAM");

	dynamic_reconfigure::Server<lsd_slam_core::LSDParamsConfig> srv(ros::NodeHandle("~"));
	srv.setCallback(dynConfCb);

	dynamic_reconfigure::Server<lsd_slam_core::LSDDebugParamsConfig> srvDebug(ros::NodeHandle("~Debug"));
	srvDebug.setCallback(dynConfCbDebug);

	packagePath = ros::package::getPath("lsd_slam_core")+"/";



	// get camera calibration in form of an undistorter object.
	// if no undistortion is required, the undistorter will just pass images through.
	std::string calibFile;
	Undistorter* undistorter = 0;
	if(ros::param::get("~calib", calibFile))
	{
		 undistorter = Undistorter::getUndistorterForFile(calibFile.c_str());
		 ros::param::del("~calib");
	}

	if(undistorter == 0)
	{
		printf("need camera calibration file! (set using _calib:=FILE)\n");
		exit(0);
	}

	int w = undistorter->getOutputWidth();
	int h = undistorter->getOutputHeight();
    printf("output width = %d\n", w);
    printf("output height = %d\n", h);
    
	int w_inp = undistorter->getInputWidth();
	int h_inp = undistorter->getInputHeight();
    printf("input width = %d\n", w_inp);
    printf("input height = %d\n", h_inp);
    
	float fx = undistorter->getK().at<double>(0, 0);
	float fy = undistorter->getK().at<double>(1, 1);
	float cx = undistorter->getK().at<double>(0, 2);
	float cy = undistorter->getK().at<double>(1, 2);
	Sophus::Matrix3f K;
	K << fx, 0.0, cx, 0.0, fy, cy, 0.0, 0.0, 1.0;


	// make output wrapper. just set to zero if no output is required.
	Output3DWrapper* outputWrapper = new ROSOutput3DWrapper(w,h);

	// make slam system
	SlamSystem* system = new SlamSystem(w, h, K, doSlam);
	system->setVisualization(outputWrapper);

    //--------------------------------------------------------------------------
    // Print some settings
    lsd_slam::minUseGrad = 5; // Default: 5
    lsd_slam::KFUsageWeight = 3; // Default:3, Max:20
    lsd_slam::KFDistWeight = 4; // Default:4, Max: 20
    lsd_slam::allowNegativeIdepths = false; // Default: true
    lsd_slam::plotStereoImages = true;
    lsd_slam::plotTracking = true;
    lsd_slam::plotTrackingIterationInfo = true;
    lsd_slam::printConstraintSearchInfo = true;
    lsd_slam::printKeyframeSelectionInfo = true;
    lsd_slam::continuousPCOutput = true;
    lsd_slam::printThreadingInfo = true; // Default: false
    
    lsd_slam::useFabMap = true;

    // Dethmap propagation and regularization info when keyframe changed
    lsd_slam::printPropagationStatistics = true; // Default: false
    lsd_slam::printRegularizeStatistics = true; //Default: false

    // Print essential info to observe in experiments
    fprintf(stderr, "KFUsageWeight = %f\n", lsd_slam::KFUsageWeight);
    fprintf(stderr, "KFDistWeight = %f\n", lsd_slam::KFDistWeight);
    fprintf(stderr, "allowNegativeIdepth = %d\n", lsd_slam::allowNegativeIdepths);
    printf("KFUsageWeight = %f\n", lsd_slam::KFUsageWeight);
    printf("KFDistWeight = %f\n", lsd_slam::KFDistWeight);
    printf("minUseGrad = %f\n", lsd_slam::minUseGrad);
    printf("allowNegativeIdepth = %d\n", lsd_slam::allowNegativeIdepths);
    
    // Settings for print or debug
    lsd_slam::consoleMode = false; // Default: false;
    
    if (lsd_slam::consoleMode)
    {
        lsd_slam::plotStereoImages = false; // Default: true
        lsd_slam::plotTracking = false;
        lsd_slam::plotTrackingIterationInfo = false;
        lsd_slam::printThreadingInfo = true;
    }
    else
    {
        lsd_slam::plotStereoImages = false; // Default: true
        lsd_slam::plotTracking = true;
        lsd_slam::plotTrackingIterationInfo = true;
        lsd_slam::printThreadingInfo = true;
        lsd_slam::printRelocalizationInfo = true;
        lsd_slam::displayDepthMap = true;
    }
    
    printf("plotStereoImages = %d\n", lsd_slam::plotStereoImages);
    printf("plotTracking = %d\n", lsd_slam::plotTracking);
    printf("plotTrackingIterationInfo = %d\n", lsd_slam::plotTrackingIterationInfo);
    printf("printThreadingInfo = %d\n", lsd_slam::printThreadingInfo);
    printf("printRelocalizationInfo = %d\n", lsd_slam::printRelocalizationInfo);
    printf("displayDepthMap = %d\n", lsd_slam::displayDepthMap);
    

    //--------------------------------------------------------------------------

	// open image files: first try to open as file.
	std::string source;
	std::vector<std::string> files;
	if(!ros::param::get("~files", source))
	{
		printf("need source files! (set using _files:=FOLDER)\n");
		exit(0);
	}
    ros::param::del("~files");


	if(getdir(source, files) >= 0)
	{
		printf("found %d image files in folder %s!\n", (int)files.size(), source.c_str());
	}
	else if(getFile(source, files) >= 0)
	{
		printf("found %d image files in file %s!\n", (int)files.size(), source.c_str());
	}
	else
	{
		printf("could not load file list! wrong path / file?\n");
	}

    //--------------------------------------------------------------------------
    // open depth files
    std::string depthSource;
    std::vector<std::string> depthFiles;
    if (!ros::param::get("~depthFiles", depthSource))
    {
        printf("Need depth source files! (set using _depthFiles:=FOLDER)\n");
        exit(0);
    }
    ros::param::del("~depthFiles");

    if(getdir(depthSource, depthFiles) >= 0)
    {
        printf("found %d depth files in folder %s!\n",
               (int)depthFiles.size(), depthSource.c_str());
    }
    else if(getFile(depthSource, depthFiles) >= 0)
    {
        printf("found %d depth files in file %s!\n",
               (int)depthFiles.size(), depthSource.c_str());
    }
    else
    {
        printf("could not load file list! wrong path / file?\n");
    }

    //--------------------------------------------------------------------------
    // get HZ
	double hz = 0;
	if(!ros::param::get("~hz", hz))
		hz = 0;
	ros::param::del("~hz");

	cv::Mat image = cv::Mat(h,w,CV_8U);
	int runningIDX=0;
	float fakeTimeStamp = 0;

	ros::Rate r(hz);
    
    //--------------------------------------------------------------------------
    // Get depth file format
    int depthFormat = 0; // Default 0: TUM format; 1: JHK(ICL-NUIM) format
    if (!ros::param::get("~depthFormat", depthFormat))
    {
        
        depthFormat = 0; // Default 0, TUM format
        
    }
    ros::param::del("~depthFormat");
    
    if ((depthFormat != 0) && (depthFormat != 1)) // Invalid format
    {
        fprintf(stderr, "Error: specify depth file format\n"
                "E.g. _depthFormat:=0 for JHK or ICL-NUIM format\n"
                "     _depthFormat:=1 for TUM format\n");
    }
    
    //--------------------------------------------------------------------------
    // Set noise on GT initial depth
    double noiseOnInitGT = 0.0;
    if (!ros::param::get("~noiseOnInitGT", noiseOnInitGT))
    {
        noiseOnInitGT = 0.0;
    }
    ros::param::del("~noiseOnInitGT");
    fprintf(stderr, "_noiseOnInitGT:=%f\n", noiseOnInitGT);
    printf("_noiseOnInitGT:=%f\n", noiseOnInitGT);

    //--------------------------------------------------------------------------

    XInitThreads();

    //--------------------------------------------------------------------------
    for(unsigned int i=0;i<files.size();i++)
	{
		cv::Mat imageDist = cv::imread(files[i], CV_LOAD_IMAGE_GRAYSCALE);

		if(imageDist.rows != h_inp || imageDist.cols != w_inp)
		{
			if(imageDist.rows * imageDist.cols == 0)
				printf("failed to load image %s! skipping.\n", files[i].c_str());
			else
				printf("image %s has wrong dimensions - expecting %d x %d, found %d x %d. Skipping.\n",
						files[i].c_str(),
						w,h,imageDist.cols, imageDist.rows);
			continue;
		}
		assert(imageDist.type() == CV_8U);

		undistorter->undistort(imageDist, image);
		assert(image.type() == CV_8U);

		if(runningIDX == 0)
        {

            if(getFile(depthSource, depthFiles) >= 0)
            {

                // Read a depth file
                // Ground truth depth init
                float *depthInput = (float*)malloc(sizeof(float)*w*h);
                printf("---------------------------------------------\n");
                printf("Reading %s\n", depthFiles[runningIDX].c_str());

                FILE* fid = fopen(depthFiles[runningIDX].c_str(),"r");
                float *ptrDepth = depthInput;
                float depthVal;
                int u = 0;
                int v = 0;
                while (fscanf(fid, "%f", &depthVal)!=EOF)
                {

                    if (u == w) // u for column index
                    {
                        v++; // v for row index
                        u = 0;
                    }

                    // Set depth value from file via calibration information
                    float u_u0_by_fx = ((float)u - cx)/fx;
                    float v_v0_by_fy = ((float)v - cy)/fy;
                    float u_u0_by_fx_sq = u_u0_by_fx*u_u0_by_fx;
                    float v_v0_by_fy_sq = v_v0_by_fy*v_v0_by_fy;
                    
                    // Refer to compute3Dpositions.m from ICL-NUIM dataset
                    float zVal;
                    if (depthFormat == 1) // (JHK Synth depth format)
                    {
                        
                        if (u==0 && v==0)
                        {
                            fprintf(stderr, "ICL-NUIM RGBD depth format:"
                                    "(Eucliden distance depth)\n");
                            printf("ICL-NUIM RGBD depth format:"
                                   "(Eucliden distance depth)\n");
                            
                        }
                        
                        zVal = depthVal /
                        sqrt(u_u0_by_fx_sq + v_v0_by_fy_sq + 1.0);
                        
                    }
                    else  // depthFormat == 0 (Freiburg depth format)
                    {
                        
                        if (u==0 && v==0)
                        {
                            fprintf(stderr,"TUM RGBD depth format:"
                                    "(z-direction depth)\n");
                            printf("TUM RGBD depth format:"
                                   "(z-direction depth)\n");
                        }
                        
                        zVal = depthVal;
                        
                    }
                    
                    // Add a uniform noise on ground truth depth
                    if (noiseOnInitGT != 0.0)
                    {
                        
                        zVal = zVal + noiseOnInitGT *
                        ((rand() % 100001) / 100000.0f);
                        
                    }

                    *ptrDepth = zVal;
                    ptrDepth++;

                    u++;

                }

                fclose(fid);

                // Init depth from the ground truth depth file
                system->gtDepthInit(image.data, depthInput,
                                    fakeTimeStamp, runningIDX);
                free (depthInput);

            }
            else
            {
                // Init depth randomly
                system->randomInit(image.data, fakeTimeStamp, runningIDX);

            }


        }
		else
			system->trackFrame(image.data, runningIDX ,hz == 0,fakeTimeStamp);
		runningIDX++;
		fakeTimeStamp+=0.03;

		if(hz != 0)
			r.sleep();

		if(fullResetRequested)
		{

			printf("FULL RESET!\n");
			delete system;

			system = new SlamSystem(w, h, K, doSlam);
			system->setVisualization(outputWrapper);

			fullResetRequested = false;
			runningIDX = 0;
		}

		ros::spinOnce();

		if(!ros::ok())
			break;

        //----------------------------------------------------------------------
        if (runningIDX <= 2)
        {
            fprintf(stderr,"Press any key to start...\n");
            std::cin.ignore(std::numeric_limits <std::streamsize> ::max(),'\n');
            fprintf(stderr,"Started\n");

        }

    }


	system->finalize();



	delete system;
	delete undistorter;
	delete outputWrapper;
	return 0;
}
