#include <cmath>
#include <chrono>
#include <string>
#include <fstream>
#include <sstream>
#include <iostream>

#include <opencv2/opencv.hpp>
#include <opencv2/highgui.hpp>

#include "Frame.h"
#include "Tracking.h"
#include "Converter.h"

enum MemRepType {
	Byte = 1,
	KiloByte = 1024,
	MegaByte = 1024 * 1024,
	GigaByte = 1024 * 1024 * 1024
};

void CudaCheckMemory(float & free, float & total, const MemRepType factor) {
	size_t freeByte ;
    size_t totalByte ;
    SafeCall(cudaMemGetInfo(&freeByte, &totalByte));
    free = (float)freeByte / factor;
    total = (float)totalByte / factor;
}

void PrintMemoryConsumption();
void SaveTrajectoryTUM(const std::string& filename, Tracking* pTracker, std::vector<double>& vdTimeList);
void LoadDatasetTUM(std::string & sRootPath,
					std::vector<std::string> & vsDList,
					std::vector<std::string> & vsRGBList,
					std::vector<double> & vdTimeStamp);

int main(int argc, char ** argv) {

	if(argc != 2) {
		std::cout << "Wrong Parameters.\n"
				      << "Usage: ./tum_example path_to_tum_dataset" << std::endl;
		exit(-1);
	}

	std::string sPath = std::string(argv[1]);
	std::vector<std::string> vsDList;
	std::vector<std::string> vsRGBList;
	std::vector<double> vdTimeList;

	LoadDatasetTUM(sPath, vsDList, vsRGBList, vdTimeList);

	if(vsDList.empty()) {
		std::cout << "Error occurs while reading the dataset.\n"
				  << "Please check your input parameters." << std::endl;
		exit(-1);
	}

	Tracking Tracker;
	Map map;
	map.AllocateDeviceMemory(MapDesc());
	map.ResetDeviceMemory();
	cv::Mat K = cv::Mat::eye(3, 3, CV_32FC1);
	cv::Mat Coeff = cv::Mat::zeros(5, 1, CV_32FC1);

	// default
//	K.at<float>(0, 0) = 525.0;
//	K.at<float>(1, 1) = 525.0;
//	K.at<float>(0, 2) = 319.5;
//	K.at<float>(1, 2) = 239.5;
	// Freiburg 1
//	K.at<float>(0, 0) = 517.3;
//	K.at<float>(1, 1) = 516.5;
//	K.at<float>(0, 2) = 318.6;
//	K.at<float>(1, 2) = 255.3;
	// Freiburg 2
	K.at<float>(0, 0) = 520.9;
	K.at<float>(1, 1) = 521.0;
	K.at<float>(0, 2) = 325.1;
	K.at<float>(1, 2) = 249.7;
	// Freiburg 3
//	K.at<float>(0, 0) = 535.4;
//	K.at<float>(1, 1) = 539.2;
//	K.at<float>(0, 2) = 320.1;
//	K.at<float>(1, 2) = 247.6;
	Frame::SetK(K);
	Frame::mDepthScale = 5000.0f;

	int nImages = std::min(vsRGBList.size(), vdTimeList.size());
	std::cout << "----------------------------------------------\n"
			  << "Total Images to be processed: " << nImages << std::endl;
	for(int i = 0; i < nImages; ++i) {
		cv::Mat imD = cv::imread(vsDList[i], cv::IMREAD_UNCHANGED);
		cv::Mat imRGB = cv::imread(vsRGBList[i], cv::IMREAD_UNCHANGED);

		auto t1 = std::chrono::steady_clock::now();
		Tracker.GrabImageRGBD(imRGB, imD);
		int no = map.FuseFrame(Tracker.mLastFrame);
		Rendering rd;
		rd.cols = 640;
		rd.rows = 480;
		rd.fx = K.at<float>(0, 0);
		rd.fy = K.at<float>(1, 1);
		rd.cx = K.at<float>(0, 2);
		rd.cy = K.at<float>(1, 2);
		rd.Rview = Tracker.mLastFrame.mRcw;
		rd.invRview = Tracker.mLastFrame.mRwc;
		rd.maxD = 5.0f;
		rd.minD = 0.1f;
		rd.tview = Converter::CvMatToFloat3(Tracker.mLastFrame.mtcw);

		map.RenderMap(rd, no);
		Tracker.AddObservation(rd);

		auto t2 = std::chrono::steady_clock::now();
        int key = cv::waitKey(10);

		cv::Mat tmp(rd.rows, rd.cols, CV_8UC4);
		rd.Render.download((void*)tmp.data, tmp.step);
		cv::imshow("img", tmp);

		switch(key) {

		case 't':
		case 'T':
			std::cout << "----------------------------------------------\n"
					  << "Frame Processed in : "
					  << std::chrono::duration_cast<std::chrono::microseconds>(t2 - t1).count()
					  << " microseconds" << std::endl;
			break;

		case 'm':
		case 'M':
			PrintMemoryConsumption();
			break;

		case 27: /* Escape */
			std::cout << "User Requested Termination." << std::endl;
			exit(0);
		}
	}

	std::cout << "Finished Processing Dataset, awaiting cleanup process." << std::endl;
	std::cout << "Save Trajectories? (Y/N)." << std::endl;
	int key = cv::waitKey(15 * 1000);
	if(key == 'y' || key == 'Y') {
		SaveTrajectoryTUM("./1.txt", &Tracker, vdTimeList);
	}
	std::cout << "Program finished, exiting." << std::endl;
}

void LoadDatasetTUM(std::string & sRootPath,
					std::vector<std::string> & vsDList,
					std::vector<std::string> & vsRGBList,
					std::vector<double> & vdTimeList) {

	std::ifstream dfile, rfile;
	std::string sDPath, sRGBPath;

	if(sRootPath.back() != '/')
		sRootPath += '/';

	dfile.open(sRootPath + "depth.txt");
	rfile.open(sRootPath + "rgb.txt");

	std::string temp;
	for(int i = 0; i < 3; ++i) {
		getline(dfile, temp);
		getline(rfile, temp);
	}

	std::string line, sD, sR;
	while(true) {
		double t;
		dfile >> t;
		vdTimeList.push_back(t);
		dfile >> sD;
		vsDList.push_back(sRootPath + sD);
		rfile >> sR;
		rfile >> sR;
		vsRGBList.push_back(sRootPath + sR);
		if(dfile.eof() || rfile.eof()) return;
	}
}

void PrintMemoryConsumption() {
	float free, total;
	CudaCheckMemory(free, total, MemRepType::MegaByte);
	std::cout << "----------------------------------------------\n"
			  << "Device Memory Consumption:\n"
			  << "Free  - " << free << "MB\n"
			  << "Total - " << total << "MB" << std::endl;
}

void SaveTrajectoryTUM(const std::string& filename, Tracking* pTracker, std::vector<double>& vdTimeList) {

//	std::cout << std::endl << "Saving camera trajectory to " << filename << " ..." << std::endl;
//    std::vector<cv::Mat>& Poses = pTracker->GetPoses();
//    std::ofstream f;
//    f.open(filename.c_str());
//    f << std::fixed;
//
//    std::vector<double>::iterator lT = vdTimeList.begin();
//    for(std::vector<cv::Mat>::iterator lit = Poses.begin(), lend = Poses.end(); lit != lend ;lit++, lT++) {
//        cv::Mat Rwc = (*lit);
//        cv::Mat twc = (*++lit);
//        std::cout << Rwc << std::endl << twc << std::endl;
//
//        std::vector<float> q = Converter::toQuaternion(Rwc);
//
//        f << std::setprecision(6) << *lT << " "
//        		                  <<  std::setprecision(9) << twc.at<float>(0) << " "
//        		                  << twc.at<float>(1) << " "
//        		                  << twc.at<float>(2) << " "
//        		                  << q[0] << " "
//        		                  << q[1] << " "
//        		                  << q[2] << " "
//        		                  << q[3] << std::endl;
//    }
//
//    f.close();
//    std::cout << std::endl << "trajectory saved!" << std::endl;
}
