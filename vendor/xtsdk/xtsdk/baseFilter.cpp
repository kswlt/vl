/**************************************************************************************
 * Copyright (C) 2022 Xintan Technology Corporation
 *
 * Author: Marco
 ***************************************************************************************/

#include <bitset>

#include "utils.h"
#include "xtlogger.h"
#include "basefilter.h"
#include <algorithm>
#include <opencv2/opencv.hpp>

using namespace cv;

namespace XinTan
{

    BaseFilter::BaseFilter(std::string &logtag)
        : logtagname(logtag)
    {
        filter_flag = 0;

        filter_lib = std::make_shared<LibHandler>(logtag);
    }

    BaseFilter::~BaseFilter()
    {
    }

    void BaseFilter::setSpecPara(uint8_t sn[29])
    {
        filter_lib->setSpecParaLib(sn);
    }

    bool BaseFilter::setAvgFilter(uint16_t size, uint16_t timedf)
    {
        const std::lock_guard<std::mutex> lock(filterLock);
        filter_flag &= ~FILTER_AVERAGE;
        if(!filter_lib->setAvgFilterLib(size, timedf)){
            return true;
        }
        filter_flag |= FILTER_AVERAGE;
        return true;
    }

    bool BaseFilter::setPostParam(const float &dilation_pixels, const int &mode, const uint8_t &winsize, uint8_t motion_size)
    {
        const std::lock_guard<std::mutex> lock(filterLock);
        filter_flag &= ~FILTER_POST;
        if(!filter_lib->setPostParamLib(dilation_pixels, mode, winsize, motion_size)){
            return true;
        }
        filter_flag |= FILTER_POST;
        return true;
    }

    bool BaseFilter::setKalmanFilter(uint16_t factor, uint16_t threshold, uint16_t timedf)
    {
        const std::lock_guard<std::mutex> lock(filterLock);
        filter_flag &= ~FILTER_KALMAN;
        if(!filter_lib->setKalmanFilterLib(factor, threshold, timedf)){
            return true;
        }
        filter_flag |= FILTER_KALMAN;
        return true;
    }

    bool BaseFilter::setEdgeFilter(uint16_t threshold)
    {
        const std::lock_guard<std::mutex> lock(filterLock);
        filter_flag &= ~FILTER_EDGE;

        if(!filter_lib->setEdgeFilterLib(threshold)){
            return true;
        }
        filter_flag |= FILTER_EDGE;
        return true;
    }

    bool BaseFilter::setMedianFilter(uint16_t size) // 必须是奇数
    {
        const std::lock_guard<std::mutex> lock(filterLock);
        filter_flag &= ~FILTER_MEDIAN;
        if(!filter_lib->setMedianFilterLib(size)){
            return true;
        }
        filter_flag |= FILTER_MEDIAN;
        return true;
    }

    bool BaseFilter::setDustFilter(uint16_t threshold, uint16_t framecount, uint16_t validpercent, uint16_t timedf)
    {
        const std::lock_guard<std::mutex> lock(filterLock);
        filter_flag &= ~FILTER_DUST;

        if(!filter_lib->setDustFilterLib(threshold, framecount, validpercent, timedf))
        {
            return true;
        }
        filter_flag |= FILTER_DUST;

        return true;

    }

    bool BaseFilter::setSdkReflectiveFilter(const float &threshold_min, const float &threshold_max)
    {
        const std::lock_guard<std::mutex> lock(filterLock);
        filter_flag &= ~FILTER_REFLECT;

        if(!filter_lib->setSdkReflectiveFilterLib(threshold_min, threshold_max)){
            return true;
        }

        filter_flag |= FILTER_REFLECT;
        return true;
    }

    bool BaseFilter::setSpatialFilter(const float &alpha, const uint32_t &delta, const uint8_t &iterations)
    {
        const std::lock_guard<std::mutex> lock(filterLock);
        filter_flag &= ~FILTER_SPATIAL;

        if (!filter_lib->setSpatialFilterLib(alpha, delta, iterations))
        {
            return true;
        }

        filter_flag |= FILTER_SPATIAL;
        return true;
    }

    bool BaseFilter::clearAllFilter()
    {
        const std::lock_guard<std::mutex> lock(filterLock);
        XTLOGINFO("");
        filter_flag = 0;
        return true;
    }

    void BaseFilter::doMedianFilter(const std::shared_ptr<Frame> &frame)
    {
        filter_lib->doMedianFilterLib(frame);
    }

    void BaseFilter::doKalmanFilter_dist(const std::shared_ptr<Frame> &frame)
    {
        filter_lib->doKalmanFilter_distLib(frame);
    }

    void BaseFilter::doKalmanFilter(const std::shared_ptr<Frame> &frame)
    {
        filter_lib->doKalmanFilterLib(frame);
    }

    void BaseFilter::doAverageFilter(const std::shared_ptr<Frame> &frame)
    {
        filter_lib->doAverageFilterLib(frame);
    }

    void BaseFilter::doEdgeFilter(const std::shared_ptr<Frame> &frame)
    {
        filter_lib->doEdgeFilterLib(frame);
    }

    void BaseFilter::doReflectiveFilter(const std::shared_ptr<Frame> &frame)
    {
        filter_lib->doReflectiveFilterLib(frame);
    }



    void BaseFilter::resetFilters()
    {
        filter_lib->resetFiltersLib();
    }

    std::string toBinaryString(int n, int bitWidth = 32)
    {
        std::bitset<32> binary(n);                                 // 使用 bitset 表示二进制
        return binary.to_string().substr(32 - bitWidth, bitWidth); // 截取指定宽度的二进制字符串
    }


    void BaseFilter::doBaseFilter(const std::shared_ptr<Frame> &frame)
    {
        const std::lock_guard<std::mutex> lock(filterLock);
        //TicToc tic;
        if (frame->frame_version > 2)
        {
            setSpecPara(frame->info.sn);
        }

        if (frame->dust_percent <= 15)
        {
            if (filter_flag & FILTER_REFLECT)
                doReflectiveFilter(frame);
        }

        if (filter_flag & FILTER_EDGE)
            doEdgeFilter(frame);

        if (filter_flag & FILTER_POST)
        {
            doMotionTrack(frame);
        }
        //std::cout << "doMotionTrack: " << tic.toc() << std::endl;

        if (filter_flag & FILTER_DUST)
            doDustFilter(frame);

        if (filter_flag & FILTER_POST)
            doPostProcess(frame);

        //tic.tic();
        if (filter_flag & FILTER_MEDIAN)
            doMedianFilter(frame);

        //std::cout << "doMedianFilter: " << tic.toc() << std::endl;


        // if(frame->dust_percent > 50){
        //     if(filter_flag & FILTER_KALMAN)
        //         doKalmanFilter(frame);
        // }
        if (filter_flag & FILTER_KALMAN)
            doKalmanFilter_dist(frame);

        if (filter_flag & FILTER_AVERAGE)
            doAverageFilter(frame);

        if (filter_flag & FILTER_SPATIAL)
            doSpatialFilter(frame);



        if (filter_flag & FILTER_EDGE)
            doEdgeFilter(frame);

    }

    void BaseFilter::doSpatialFilter(const std::shared_ptr<Frame> &frame)
    {
        filter_lib->doSpatialFilterLib(frame);
    }

    void BaseFilter::doPostProcess(const std::shared_ptr<Frame> &frame)
    {
        filter_lib->doPostProcessLib(frame);
    }

    void BaseFilter::doMotionTrack(const std::shared_ptr<Frame> &frame)
    {
        filter_lib->doMotionTrackLib(frame);
    }

    void BaseFilter::doCloseProcess(const std::shared_ptr<Frame> &frame)
    {
        filter_lib->doCloseProcessLib(frame);
    }

    void BaseFilter::doDustFilter(const std::shared_ptr<Frame> &frame)
    {
        filter_lib->doDustFilterLib(frame);
    }

} // end namespace XinTan
