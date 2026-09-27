/**************************************************************************************
 * Copyright (C) 2022 Xintan Technology Corporation
 *
 * Author: Marco
 ***************************************************************************************/
#include "cartesianTransform.h"
#include <stdint.h>
#include <iostream>
#include <math.h>
#include <string>
#include <fstream>
#include <sstream>

using namespace cv;

namespace XinTan
{

    CartesianTransform::CartesianTransform(std::string &logtag)
        : logtagname(logtag)
    {
        hasparam = false;
        bPointsCornerCut = true;
        chamferpixels = 28; // 40

        camParams = {
            (float)170.0, (float)170.0, (float)160.0,
            (float)120.0, (float)0.0, (float)0.0,
            (float)0.0, (float)0.0, (float)0.0};

        camParamsM60default = {
            (float)78.212524, (float)79.055527, (float)80.701973,
            (float)26.804274, (float)-0.302470, (float)0.074865,
            (float)0.0, (float)-0.002307, (float)-0.001400};

        ocX = 0.0;
        ocY = 0.0;

        e_imu_lidar_default_g = {
            (float)0.0,
            (float)0.7071068,
            (float)0.0,
            (float)0.7071068,
            (float)-0.02932,
            (float)0.023823,
            (float)-0.001697,
        };
        e_imu_lidar_default_q = {
            (float)0.0,
            (float)0.7071068,
            (float)0.0,
            (float)0.7071068,
            (float)-0.02932,
            (float)0.020403,
            (float)-0.003163,
        };
        mirror_q = {
            (float)0.0,
            (float)1.0,
            (float)0.0,
            (float)0.0,
            (float)-0.02932,
            (float)0.020403,
            (float)-0.003163,
        };
        e_imu_lidar_ = e_imu_lidar_default_g;
        updateInverseExtrinsic(e_imu_lidar_, e_imu_lidar_mirror_);
        hmirror = false;
        vmirror = false;
        cutMinAmp = 0;
        cutcornervalue = 28;
        pointout_coord = 0;
        bisM60 = false;
        blast_camparmsIsZero = false;
    }

    CartesianTransform::~CartesianTransform()
    {
    }

    void CartesianTransform::undistort(float x, float y, float *out_x,
                                       float *out_y)
    {
        float x_mm = (x - camParams.cx) / camParams.fx; // 转为mm
        float y_mm = (y - camParams.cy) / camParams.fy; // 转为mm

        float r2 = x_mm * x_mm + y_mm * y_mm;
        float r4 = r2 * r2;
        float r6 = r4 * r2;

        float x_distorted = x_mm * (1 + camParams.k1 * r2 + camParams.k2 * r4 + camParams.k3 * r6) + 2 * camParams.p1 * x_mm * y_mm + camParams.p2 * (r2 + 2 * x_mm * x_mm);
        float y_distorted = y_mm * (1 + camParams.k1 * r2 + camParams.k2 * r4 + camParams.k3 * r6) + 2 * camParams.p2 * x_mm * y_mm + camParams.p1 * (r2 + 2 * y_mm * y_mm);

        *out_x = x_distorted;
        *out_y = y_distorted;

        *out_x = x_distorted * camParams.fx + camParams.cx; // 转为像素
        *out_y = y_distorted * camParams.fy + camParams.cy; // 转为像素
    }

    void CartesianTransform::setcutcorner(uint32_t cutvalue)
    {
        int sensor_width = 320;
        int sensor_height = 240;
        if (bisM60)
        {
            sensor_width = 160;
            sensor_height = 60;
        }

        cutcornervalue = cutvalue;

        outputUndistortedPointsDeled.clear();

        for (int h = 0; h < sensor_height; h++)
        {
            for (int w = 0; w < sensor_width; w++)
            {
                outputUndistortedPointsDeled.push_back(false);
            }
        }

        if ((cutvalue == 0) || (cutvalue > 201))
            bPointsCornerCut = false;
        else
        {
            chamferpixels = cutvalue;
            if (bisM60)
                chamferpixels = chamferpixels / 4;
            bPointsCornerCut = true;
        }

        // rect cut for corner
        if (bPointsCornerCut)
        {
            int deledcount = 0;
            for (int h = 0; h < sensor_height; h++)
            {
                // std::cout << "|";
                for (int w = 0; w < sensor_width; w++)
                {
                    // chamfer
                    if (w < chamferpixels)
                    {
                        if (h < chamferpixels)
                        {
                            if (sqrt((chamferpixels - w) * (chamferpixels - w) + (chamferpixels - h) * (chamferpixels - h)) > chamferpixels)
                                outputUndistortedPointsDeled[h * sensor_width + w] = true;
                        }

                        if (h > (sensor_height - chamferpixels))
                        {
                            if (sqrt((chamferpixels - w) * (chamferpixels - w) + (h - (sensor_height - chamferpixels)) * (h - (sensor_height - chamferpixels))) > chamferpixels)
                                outputUndistortedPointsDeled[h * sensor_width + w] = true;
                        }
                    }
                    if (w > (sensor_width - chamferpixels))
                    {
                        if (h < chamferpixels)
                        {
                            if (sqrt((w - (sensor_width - chamferpixels)) * (w - (sensor_width - chamferpixels)) + (chamferpixels - h) * (chamferpixels - h)) > chamferpixels)
                                outputUndistortedPointsDeled[h * sensor_width + w] = true;
                        }

                        if (h > (sensor_height - chamferpixels))
                        {
                            if (sqrt((w - (sensor_width - chamferpixels)) * (w - (sensor_width - chamferpixels)) + (h - (sensor_height - chamferpixels)) * (h - (sensor_height - chamferpixels))) > chamferpixels)
                                outputUndistortedPointsDeled[h * sensor_width + w] = true;
                        }
                    }

                    if (outputUndistortedPointsDeled[h * sensor_width + w])
                    {
                        deledcount++;
                    }
                }
            }
            std::cout << "deledpercent = " << static_cast<float>(deledcount) * 100 / (static_cast<float>(sensor_width * sensor_height) + 0.001) << "%" << std::endl;
        }
    }

    void CartesianTransform::maptable(CamParameterS &camparams)
    {
        const std::lock_guard<std::mutex> lock(camparams_mutex);

        if ((camParams.fx == camparams.fx) && (camParams.fy == camparams.fy) &&
            (camParams.cx == camparams.cx) && (camParams.cy == camparams.cy) &&
            (camParams.k1 == camparams.k1) && (camParams.k2 == camparams.k2) &&
            (camParams.k3 == camparams.k3) && (camParams.p1 == camparams.p1) &&
            (camParams.p2 == camparams.p2))
            return;

        if ((0 == camparams.fx) && (0 == camparams.fy) && (0 == camparams.k1))
        {
            if (blast_camparmsIsZero)
                return;
            blast_camparmsIsZero = true;
            std::cout << "changed camparams is empty:used default" << std::endl;
            camParams = camParamsM60default;
        }
        else
            camParams = camparams;

        blast_camparmsIsZero = false;

        std::cout << "changed camparams fx=" << std::to_string(camparams.fx) << "k1=" << std::to_string(camparams.k1) << std::endl;

        cv::Mat cameraMatrix2 = cv::Mat::eye(3, 3, CV_64F);
        cameraMatrix2.at<double>(0, 0) = camParams.fx;
        cameraMatrix2.at<double>(0, 1) = 0;
        cameraMatrix2.at<double>(0, 2) = camParams.cx;
        cameraMatrix2.at<double>(1, 0) = 0;
        cameraMatrix2.at<double>(1, 1) = camParams.fy;
        cameraMatrix2.at<double>(1, 2) = camParams.cy;
        cameraMatrix2.at<double>(2, 0) = 0;
        cameraMatrix2.at<double>(2, 1) = 0;
        cameraMatrix2.at<double>(2, 2) = 1;

        cv::Mat distCoeffs2 = cv::Mat::zeros(5, 1, CV_64F);
        distCoeffs2.at<double>(0) = camParams.k1;
        distCoeffs2.at<double>(1) = camParams.k2;
        distCoeffs2.at<double>(2) = camParams.p1;
        distCoeffs2.at<double>(3) = camParams.p2;
        distCoeffs2.at<double>(4) = camParams.k3;

        std::vector<cv::Point2f> inputDistortedPoints;
        outputUndistortedPoints.clear();
        outputUndistortedPointsDeled.clear();

        cv::Point2f m_point;
        float outx, outy;
        cv::Point2f out;

        int sensor_width = 320;
        int sensor_height = 240;
        if (bisM60)
        {
            sensor_width = 160;
            sensor_height = 60;
        }

        bool usingcv = true;
        for (int h = 0; h < sensor_height; h++)
        {
            for (int w = 0; w < sensor_width; w++)
            {
                m_point.x = w;
                m_point.y = h;
                inputDistortedPoints.push_back(m_point);
                outputUndistortedPointsDeled.push_back(false);

                if (usingcv == false)
                {
                    undistort(w, h, &outx, &outy);
                    out.x = outx;
                    out.y = outy;
                    outputUndistortedPoints.push_back(out);
                }
            }
        }
        if (usingcv)
            cv::undistortPoints(inputDistortedPoints, outputUndistortedPoints, cameraMatrix2, distCoeffs2, cv::noArray(), cameraMatrix2);

        ocX = camParams.cx;
        ocY = camParams.cy;

        // rect cut for corner
        setcutcorner(cutcornervalue);

        hasparam = true;
    }

    void CartesianTransform::setTransMirror(bool hmirror0, bool vmirror0)
    {
        hmirror = hmirror0;
        vmirror = vmirror0;
    }

    void
    CartesianTransform::pcltransCamparm(const std::shared_ptr<Frame> &frame)
    {
        const size_t nPixel = frame->width * frame->height;

        int i = 0;

        float xx, yy;

        if (hasparam == false)
            return;

        bool binningX = (frame->binning >> 1) & 0x01;
        bool binningY = frame->binning & 0X01;
        uint16_t ybinning = frame->ybinning;
        uint16_t xbinning = frame->xbinning;

        bool checkisM60 = false;
        int m60max = 9700;
        if (binningX)
            m60max = m60max / 2;
        if (binningY)
            m60max = m60max / 2;

        if (nPixel < m60max)
            checkisM60 = true;

        if (bisM60 != checkisM60)
        {
            bisM60 = checkisM60;
            CamParameterS camparams = camParams;
            camParams.p2 = -10.0;
            maptable(camparams);
        }

        const std::lock_guard<std::mutex> lock(camparams_mutex);

        int sensor_width = 320;
        int sensor_height = 240;
        if (bisM60)
        {
            sensor_width = 160;
            sensor_height = 60;
        }

        frame->points.resize((int)nPixel);
        int p = 0;

        uint32_t dist_unitmm = 1;

        // #pragma omp parallel for private(p)
        for (p = 0; p < frame->height; p++)
        {
            for (int q = 0; q < frame->width; q++, i++)
            {
                uint32_t curdis = frame->distData[p * frame->width + q];

                int index;

                if (binningY)
                {
                    if (binningX)
                        index = (p * 2 * ybinning + frame->roi_y0) * sensor_width + q * 2 * xbinning + frame->roi_x0;
                    else
                        index = (p * 2 * ybinning + frame->roi_y0) * sensor_width + q * xbinning + frame->roi_x0;
                }
                else
                {
                    if (binningX)
                        index = (p * ybinning + frame->roi_y0) * sensor_width + q * 2 * xbinning + frame->roi_x0;
                    else
                        index = (p * ybinning + frame->roi_y0) * sensor_width + q * xbinning + frame->roi_x0;
                }

                if (index >= sensor_width * sensor_height)
                    index = sensor_width * sensor_height - 1;

                index = (index / sensor_width) * sensor_width + sensor_width - 1 - index % sensor_width; // 左右翻转的取索引

                if ((frame->dataType == Frame::AMPLITUDE) && (cutMinAmp > 0))
                {
                    if ((frame->amplData[i] < cutMinAmp) ||
                        (frame->amplData[i] == AMPLITUDE_INVALID))
                    {
                        frame->points[i].x = std::numeric_limits<float>::quiet_NaN();
                        frame->points[i].y = std::numeric_limits<float>::quiet_NaN();
                        frame->points[i].z = std::numeric_limits<float>::quiet_NaN();
                        frame->distData[i] = DEPTH_LOW; // 48000 + 16001;
                        continue;
                    }
                }

                if (bPointsCornerCut && outputUndistortedPointsDeled[index])
                {
                    frame->points[i].x = std::numeric_limits<float>::quiet_NaN();
                    frame->points[i].y = std::numeric_limits<float>::quiet_NaN();
                    frame->points[i].z = std::numeric_limits<float>::quiet_NaN();
                    frame->distData[i] = 0;
                    continue;
                }
                xx = outputUndistortedPoints.at(index).x - ocX;
                yy = outputUndistortedPoints.at(index).y - ocY;

                double ax = xx * xx / (camParams.fx * camParams.fx);
                double ay = yy * yy / (camParams.fy * camParams.fy);

                frame->points[i].z = 0.001 * curdis / sqrt(ax + ay + 1);
                frame->points[i].x = xx * frame->points[i].z / camParams.fx;
                frame->points[i].y = yy * frame->points[i].z / camParams.fy;

                if (frame->dataType == Frame::AMPLITUDE)
                {
                    // frame->points[i].intensity = frame->amplData[i];
                    frame->points[i].intensity = frame->reflectivity[i];
                }
                else
                    frame->points[i].intensity = std::numeric_limits<float>::quiet_NaN();

                if (hmirror)
                    frame->points[i].x = -frame->points[i].x;

                if (!vmirror)
                    frame->points[i].y = -frame->points[i].y;

                if (curdis >= 964000)
                {
                    frame->points[i].x = std::numeric_limits<float>::quiet_NaN();
                    frame->points[i].y = std::numeric_limits<float>::quiet_NaN();
                    frame->points[i].z = std::numeric_limits<float>::quiet_NaN();
                }

                if (std::isnan(frame->points[i].z))
                    continue;

                if (pointout_coord == 1)
                {
                    XinTan::XtPointXYZI tmp = frame->points[i];
                    frame->points[i].x = tmp.z;
                    frame->points[i].y = tmp.x;
                    frame->points[i].z = tmp.y;
                }
            }
        }
        frame->hasPointcloud = true;
    }

    void CartesianTransform::updateInverseExtrinsic(const ExtrinsicIMULidar &extrinsic, ExtrinsicIMULidar &mirror_extrinsic)
    {
        // 激光雷达绕X轴旋转180度对应的四元数
        float q_rotation[4] = {0, 1, 0, 0}; // [qw, qx, qy, qz] 为绕X轴旋转180度的四元数

        // 更新四元数
        float q_current[4] = {extrinsic.qw, extrinsic.qx, extrinsic.qy, extrinsic.qz};
        float q_new[4];
        Utils::quaternionMultiply(q_rotation, q_current, q_new);

        mirror_extrinsic.qw = q_new[0];
        mirror_extrinsic.qx = q_new[1];
        mirror_extrinsic.qy = q_new[2];
        mirror_extrinsic.qz = q_new[3];

        // 更新平移
        float R_x_180[3][3] = {
            {1, 0, 0},
            {0, -1, 0},
            {0, 0, -1}};

        mirror_extrinsic.tx = R_x_180[0][0] * extrinsic.tx + R_x_180[0][1] * extrinsic.ty + R_x_180[0][2] * extrinsic.tz;
        mirror_extrinsic.ty = R_x_180[1][0] * extrinsic.tx + R_x_180[1][1] * extrinsic.ty + R_x_180[1][2] * extrinsic.tz;
        mirror_extrinsic.tz = R_x_180[2][0] * extrinsic.tx + R_x_180[2][1] * extrinsic.ty + R_x_180[2][2] * extrinsic.tz;
    }

    void CartesianTransform::updateImuExtParamters(const ExtrinsicIMULidar &e_imu_lidar)
    {
        if ((e_imu_lidar_.qw == e_imu_lidar.qw) && (e_imu_lidar_.qx == e_imu_lidar.qw) &&
            (e_imu_lidar_.qy == e_imu_lidar.qy) && (e_imu_lidar_.qz == e_imu_lidar.qz) &&
            (e_imu_lidar_.tx == e_imu_lidar.tx) && (e_imu_lidar_.ty == e_imu_lidar.ty) &&
            (e_imu_lidar_.tz == e_imu_lidar.tz))
            return;

        e_imu_lidar_ = e_imu_lidar;
        updateInverseExtrinsic(e_imu_lidar_, e_imu_lidar_mirror_);
    }

    ExtrinsicIMULidar CartesianTransform::getCurrentImuExtParamters()
    {
        if (hmirror && vmirror)
        {
            return e_imu_lidar_mirror_;
        }

        else
        {
            return e_imu_lidar_;
        }
    }

} // end namespace XinTan
