#pragma once
#include "Iframe.h"
#include <iostream>
#include <memory>
#include <mutex>
#include <functional>
#include <cstring>
namespace XinTan
{
// typedef void (*LogFunction)(const std::string& function, const std::string& logstr);


enum BaseFilterType
{
    FILTER_KALMAN = 0x0001,
    FILTER_AVERAGE = 0x0002,
    FILTER_EDGE = 0x0004,
    FILTER_MEDIAN = 0x0008,
    FILTER_DUST = 0x0010,
    FILTER_POST = 0x0020,
    FILTER_REFLECT = 0x0040,
    FILTER_SPATIAL = 0x0080
};

struct SpecFiltersPara
{
    //edge filter
    uint32_t edge_range;

    //reflect filter
    float min_valid_ref;
    uint16_t min_valid_amp;
    uint32_t min_valid_dist;
    uint32_t max_valid_dist;
    uint16_t max_valid_int;

};

struct DynamicArray
{
    uint32_t *data;
    size_t size;

    DynamicArray() : data(nullptr), size(0) {}

    ~DynamicArray()
    {
        if (data)
        {
            delete[] data;
            data = nullptr; // 确保在释放后将指针置空
            size = 0;
        }
    }

    // 拷贝构造函数
    DynamicArray(const DynamicArray &other) : data(nullptr), size(0)
    {
        if (other.size > 0)
        {
            data = new uint32_t[other.size];
            size = other.size;
            std::memcpy(data, other.data, size * sizeof(uint32_t));
        }
    }

    // 拷贝赋值操作符
    DynamicArray &operator=(const DynamicArray &other)
    {
        if (this != &other)
        {
            if (data)
            {
                delete[] data;
                data = nullptr; // 确保在释放后将指针置空
                size = 0;
            }

            if (other.size > 0)
            {
                data = new uint32_t[other.size];
                size = other.size;
                std::memcpy(data, other.data, size * sizeof(uint32_t));
            }
        }
        return *this;
    }

    // 移动构造函数
    DynamicArray(DynamicArray &&other) noexcept : data(other.data), size(other.size)
    {
        other.data = nullptr;
        other.size = 0;
    }

    // 移动赋值操作符
    DynamicArray &operator=(DynamicArray &&other) noexcept
    {
        if (this != &other) {
            delete[] data;
            data = other.data;
            size = other.size;
            other.data = nullptr;
            other.size = 0;
        }
        return *this;
    }
};
struct DustFilterParams
{
    std::shared_ptr<IFrame> frame;
    time_t lastFrameTime;
    uint32_t currlast_count;

    DynamicArray last_distData[11];

    //// 构造函数
    // DustFilterParams(std::shared_ptr<IFrame> framePtr, time_t frameTime, uint32_t count, const std::vector<uint16_t>* last_distData_)
    //     : frame(framePtr), lastFrameTime(frameTime), currlast_count(count) {
    //     for (size_t i = 0; i < 10; ++i) {
    //         last_distData[i] = last_distData_[i]; // 逐个初始化数组成员
    //     }
    // }
};

class IBaseFilter_lib
{
public:
    virtual ~IBaseFilter_lib() = default;

    virtual bool setDustFilter(uint16_t threshold,
                               uint16_t framecount,
                               uint16_t validpercent,
                               uint16_t timedf = 300) = 0;

    virtual bool setEdgeFilter(uint16_t threshold) = 0;
    virtual bool setKalmanFilter(uint16_t factor, uint16_t threshold, uint16_t timedf = 300) = 0;
    virtual bool setMedianFilter(uint16_t size) = 0;
    virtual bool setPostParam(const float &dilation_pixels, const int &mode, const uint8_t &winsize, uint8_t motion_size) = 0;
    virtual bool setSdkReflectiveFilter(const float &threshold_min, const float &threshold_max) = 0;
    virtual void setSpecPara(uint8_t sn[29]) = 0;
    virtual bool setAvgFilter(uint16_t size, uint16_t timedf = 300) = 0;
    virtual bool setSpatialFilter(const float &alpha, const uint32_t &delta, const uint8_t &iterations) = 0;

    virtual void resetFilters() = 0;

    virtual void doDustFilter(const std::shared_ptr<IFrame> &frame) = 0;
    virtual void doMedianFilter(const std::shared_ptr<IFrame> &frame) = 0;
    virtual void doKalmanFilter(const std::shared_ptr<IFrame> &frame) = 0;
    virtual void doKalmanFilter_dist(const std::shared_ptr<IFrame> &frame) = 0;
    virtual void doAverageFilter(const std::shared_ptr<IFrame> &frame) = 0;
    virtual void doEdgeFilter(const std::shared_ptr<IFrame> &frame) = 0;

    virtual void doPostProcess(const std::shared_ptr<IFrame> &frame) = 0;
    virtual void doCloseProcess(const std::shared_ptr<IFrame> &frame) = 0;
    virtual void doMotionTrack(const std::shared_ptr<IFrame> &frame) = 0;

    virtual void doCloudFilter(const std::shared_ptr<IFrame> &frame) = 0;
    virtual void doReflectiveFilter(const std::shared_ptr<IFrame> &frame) = 0;
    virtual void doSpatialFilter(const std::shared_ptr<IFrame> &frame) = 0;

    virtual void printVersionLib() = 0;
};

#if defined(_WIN32) || defined(_WIN64)
#define EXPORT __declspec(dllexport)
#else
#define EXPORT
#endif

extern "C"
{
EXPORT IBaseFilter_lib *createInstance(LogFunction logFunction);
EXPORT void destroyInstance(IBaseFilter_lib *instance);
}
}
