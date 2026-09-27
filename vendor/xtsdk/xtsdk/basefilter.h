/**************************************************************************************
 * Copyright (C) 2022 Xintan Technology Corporation
 *
 * Author: Marco
 ***************************************************************************************/
#pragma once

#define NOMINMAX
#include <memory>
#include <mutex>
#include <vector>
#include <algorithm>
#include "frame.h"
#include "tic_toc.h"

#include "xtsdkLibHandler.h"
#define DUST_MAX_FRAME 9
#define MAX_DUSTPERCEN_QUE_SIZE 20
namespace XinTan
{

typedef unsigned int uint;

class BaseFilter
{
public:
    BaseFilter(std::string &logtag);
    ~BaseFilter();
    std::string &logtagname;

    std::mutex filterLock;

    uint16_t filter_flag;
    void doBaseFilter(const std::shared_ptr<Frame> &frame);

    bool setAvgFilter(uint16_t size, uint16_t timedf = 300);
    bool setKalmanFilter(uint16_t factor, uint16_t threshold, uint16_t timedf = 300);
    bool setEdgeFilter(uint16_t threshold);
    bool setMedianFilter(uint16_t size);
    bool setPostParam(const float &dilation_pixels, const int &mode, const uint8_t &winsize, uint8_t motion_size);
    bool clearAllFilter();

    bool setDustFilter(uint16_t threshold, uint16_t framecount, uint16_t validpercent, uint16_t timedf = 300);
    bool setSdkReflectiveFilter(const float &threshold_min, const float &threshold_max);
    bool setSpatialFilter(const float &alpha, const uint32_t &delta, const uint8_t &iterations);
    void resetFilters();

private:
    void doMedianFilter(const std::shared_ptr<Frame> &frame);
    void doKalmanFilter(const std::shared_ptr<Frame> &frame);
    void doKalmanFilter_dist(const std::shared_ptr<Frame> &frame);
    void doAverageFilter(const std::shared_ptr<Frame> &frame);
    void doEdgeFilter(const std::shared_ptr<Frame> &frame);
    void doSpatialFilter(const std::shared_ptr<Frame> &frame);
    void doPostProcess(const std::shared_ptr<Frame> &frame);
    void doCloseProcess(const std::shared_ptr<Frame> &frame);
    void doMotionTrack(const std::shared_ptr<Frame> &frame);

    void doCloudFilter(const std::shared_ptr<Frame> &frame);
    void doReflectiveFilter(const std::shared_ptr<Frame> &frame);
    void doDustFilter(const std::shared_ptr<Frame> &frame);
    void setSpecPara(uint8_t sn[29]);

    LibHandler::Ptr filter_lib;
};

} // end namespace XinTan
