/**************************************************************************************
 * Copyright (C) 2022 Xintan Technology Corporation
 *
 * Author: Marco
 ***************************************************************************************/
#pragma once

#include <mutex>
#include "frame.h"
#include <opencv2/opencv.hpp>
#include "utils.h"

namespace XinTan
{

    typedef unsigned int uint;

    class CartesianTransform
    {
    public:
        CartesianTransform(std::string &logtag);
        ~CartesianTransform();
        std::string &logtagname;

        CamParameterS camParams;
        CamParameterS camParamsM60default;
        ExtrinsicIMULidar e_imu_lidar_;
        ExtrinsicIMULidar e_imu_lidar_mirror_;
        ExtrinsicIMULidar e_imu_lidar_default_g;
        ExtrinsicIMULidar e_imu_lidar_default_q;
        ExtrinsicIMULidar mirror_q;
        double ocX, ocY;
        std::vector<cv::Point2f> outputUndistortedPoints;
        std::vector<bool> outputUndistortedPointsDeled;
        bool bPointsCornerCut;
        uint16_t cutMinAmp;
        int chamferpixels;
        bool hasparam;
        int cutcornervalue;

        std::mutex camparams_mutex;

        bool blast_camparmsIsZero;
        bool bisM60;
        uint8_t pointout_coord;
        bool hmirror, vmirror;
        void updateInverseExtrinsic(const ExtrinsicIMULidar &extrinsic, ExtrinsicIMULidar &mirror_extrinsic);
        void setTransMirror(bool hmirror0, bool vmirror0);
        void setcutcorner(uint32_t cutvalue);

        void maptable(CamParameterS &camparams);
        void pcltransCamparm(const std::shared_ptr<Frame> &frame);
        void updateImuExtParamters(const ExtrinsicIMULidar &e_imu_lidar);
        ExtrinsicIMULidar getCurrentImuExtParamters();

        void undistort(float x, float y, float *out_x, float *out_y);
    };

} // end namespace XinTan
