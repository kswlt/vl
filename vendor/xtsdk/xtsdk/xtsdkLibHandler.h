
#ifndef XTSDKLIBHANDLER_H
#define XTSDKLIBHANDLER_H

#include "IbaseFilter_lib.h"
#include "frame.h"
#include "xtlogger.h"
#include <iostream>
#include <memory>
#include <mutex>

// #include "xtsdk.h"

#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>
typedef HMODULE LibHandle;
#else
#include <cstring>
#include <dlfcn.h>
typedef void *LibHandle;
#endif

namespace XinTan {
extern void xtdlllogger(const char *pfunction, const char *plogstr);
class LibHandler {
  private:
    std::string lib_path = "";
    // DustFilterParams params_dustfilter;

    LibHandle handle;
    typedef IBaseFilter_lib *(*CreateBaseFilterFunc)(LogFunction);
    typedef void (*DestroyInstanceFunc)(IBaseFilter_lib *);
    // std::shared_ptr<XinTan::BaseFilter_lib> filter;
    IBaseFilter_lib *filter;

    bool bLibLoadSuccess = false;

  public:
    typedef std::shared_ptr<LibHandler> Ptr;
    typedef std::shared_ptr<const LibHandler> ConstPtr;
    std::string &logtagname;
    bool getLoadFlag();
    LibHandler(std::string &logtag);
    ~LibHandler();
    bool setDustFilterLib(uint16_t threshold, uint16_t framecount,
                          uint16_t validpercent, uint16_t timedf = 300);
    void doDustFilterLib(const std::shared_ptr<Frame> &frame);


    bool setEdgeFilterLib(uint16_t threshold);
    void doEdgeFilterLib(const std::shared_ptr<Frame> &frame);


    bool setKalmanFilterLib(uint16_t factor, uint16_t threshold, uint16_t timedf = 300);

    bool setMedianFilterLib(uint16_t size);
    bool setPostParamLib(const float &dilation_pixels, const int &mode, const uint8_t &winsize, uint8_t motion_size);
    bool setSdkReflectiveFilterLib(const float &threshold_min, const float &threshold_max);
    bool setAvgFilterLib(uint16_t size, uint16_t timedf = 300);
    bool setSpatialFilterLib(const float &alpha, const uint32_t &delta, const uint8_t &iterations);

    void resetFiltersLib();

    void doMedianFilterLib(const std::shared_ptr<IFrame> &frame);
    void doKalmanFilterLib(const std::shared_ptr<IFrame> &frame);
    void doKalmanFilter_distLib(const std::shared_ptr<IFrame> &frame);
    void doAverageFilterLib(const std::shared_ptr<IFrame> &frame);
    void doPostProcessLib(const std::shared_ptr<IFrame> &frame);
    void doCloseProcessLib(const std::shared_ptr<IFrame> &frame);
    void doMotionTrackLib(const std::shared_ptr<IFrame> &frame);
    void doReflectiveFilterLib(const std::shared_ptr<IFrame> &frame);
    void doSpatialFilterLib(const std::shared_ptr<IFrame> &frame);
    void setSpecParaLib(uint8_t sn[29]);

    };
}

#endif // XTSDKLIBHANDLER_H
