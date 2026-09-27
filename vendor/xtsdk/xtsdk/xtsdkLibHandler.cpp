
#include "xtsdkLibHandler.h"
#include <sstream>
namespace XinTan
{

    LibHandler::LibHandler(std::string &logtag) : logtagname(logtag)
    {
        // logtagname = logtag;
#ifdef _WIN32
        lib_path = "xtsdk_shared.dll";
        // lib_path = std::string(PROJECT_DIR) + "/xtsdk/lib/win32/xtsdk_shared.dll";
        std::cout << "start loading win32 lib " << lib_path << std::endl;
        handle = LoadLibrary(lib_path.c_str());
        if (!handle)
        {
            std::cerr << "Cannot open library: " << GetLastError() << std::endl;
            bLibLoadSuccess = false;
            return;
            // exit(1);
        }
        std::cout << "loading win32 success " << lib_path << std::endl;
        bLibLoadSuccess = true;
        CreateBaseFilterFunc createBaseFilter = (CreateBaseFilterFunc)GetProcAddress(handle, "createBaseFilter");

#else
        // #ifdef ARCH_X86_64
        //         lib_path = std::string(PROJECT_DIR) + "/xtsdk/lib/linux/x86_64/libxtsdk_shared.so";
        // #else
        //         lib_path = std::string(PROJECT_DIR) + "/xtsdk/lib/linux/aarch64/libxtsdk_shared.so";
        // #endif
        lib_path = std::string(LIB_DIR) + "/libxtsdk_shared.so";
        std::cout << "start loading linux lib " << lib_path << std::endl;
        handle = dlopen(lib_path.c_str(), RTLD_LAZY);
        if (!handle)
        {
            std::cerr << "Failed to load the library: " << dlerror() << std::endl;
            bLibLoadSuccess = false;
            return;
            // exit(1);
        }
        std::cout << "loading linux success " << lib_path << std::endl;
        bLibLoadSuccess = true;
        CreateBaseFilterFunc createBaseFilter = (CreateBaseFilterFunc)dlsym(handle, "createBaseFilter");

#endif
        if (!createBaseFilter)
        {
            std::cerr << "Cannot load symbol createBaseFilter" << std::endl;
#if defined(_WIN32) || defined(_WIN64)
            FreeLibrary(handle);
#else
            dlclose(handle);
#endif
            // exit(1);
            bLibLoadSuccess = false;
            return;
        }
        bLibLoadSuccess = true;
        filter = createBaseFilter(xtdlllogger);

        if(bLibLoadSuccess)
        {
            filter->printVersionLib();
        }
    }

    LibHandler::~LibHandler()
    {
#ifdef _WIN32
        if (handle)
        {
            FreeLibrary(handle);
        }
#else
        if (handle)
        {
            dlclose(handle);
        }
#endif
    }
    bool LibHandler::setDustFilterLib(uint16_t threshold,
                                      uint16_t framecount,
                                      uint16_t validpercent,
                                      uint16_t timedf)
    {
        // if (filter != nullptr)
        if (bLibLoadSuccess)
            return filter->setDustFilter(threshold, framecount, validpercent, timedf);
        else
            return false;
    }

    bool LibHandler::setEdgeFilterLib(uint16_t threshold)
    {
        if (!bLibLoadSuccess)
            return false;
        return filter->setEdgeFilter(threshold);
    }
    void LibHandler::doEdgeFilterLib(const std::shared_ptr<Frame> &frame)
    {
        if (!bLibLoadSuccess)
            return;

        filter->doEdgeFilter(frame);

    }

    std::string printSharedPtrAddress(const std::shared_ptr<Frame> &ptr)
    {
        std::ostringstream oss;
        oss << static_cast<const void *>(ptr.get());
        return oss.str();
    }
    void LibHandler::doDustFilterLib(const std::shared_ptr<Frame> &frame) {
        if (!bLibLoadSuccess)
            return;

        filter->doDustFilter(frame);
    }

    bool LibHandler::setAvgFilterLib(uint16_t size, uint16_t timedf)
    {
        if (!bLibLoadSuccess)
            return false;
        return filter->setAvgFilter(size, timedf);
    }

    bool LibHandler::setKalmanFilterLib(uint16_t factor, uint16_t threshold, uint16_t timedf)
    {
        if (!bLibLoadSuccess)
            return false;
        return filter->setKalmanFilter(factor, threshold, timedf);
    }

    bool LibHandler::setMedianFilterLib(uint16_t size)
    {
        if (!bLibLoadSuccess)
            return false;
        return filter->setMedianFilter(size);
    }
    bool LibHandler::setPostParamLib(const float &dilation_pixels, const int &mode, const uint8_t &winsize, uint8_t motion_size)
    {
        if (!bLibLoadSuccess)
            return false;
        return filter->setPostParam(dilation_pixels, mode, winsize, motion_size);
    }
    bool LibHandler::setSdkReflectiveFilterLib(const float &threshold_min, const float &threshold_max)
    {
        if (!bLibLoadSuccess)
            return false;
        return filter->setSdkReflectiveFilter(threshold_min, threshold_max);
    }

    bool LibHandler::setSpatialFilterLib(const float &alpha, const uint32_t &delta, const uint8_t &iterations)
    {
        if (!bLibLoadSuccess)
            return false;
        return filter->setSpatialFilter(alpha, delta, iterations);
    }

    void LibHandler::doMedianFilterLib(const std::shared_ptr<IFrame> &frame)
    {
        if (!bLibLoadSuccess)
            return;

        filter->doMedianFilter(frame);
    }
    void LibHandler::doKalmanFilterLib(const std::shared_ptr<IFrame> &frame)
    {
        if (!bLibLoadSuccess)
            return;

        filter->doKalmanFilter(frame);
    }
    void LibHandler::doKalmanFilter_distLib(const std::shared_ptr<IFrame> &frame)
    {
        if (!bLibLoadSuccess)
            return;

        filter->doKalmanFilter_dist(frame);
    }
    void LibHandler::doAverageFilterLib(const std::shared_ptr<IFrame> &frame)
    {
        if (!bLibLoadSuccess)
            return;

        filter->doAverageFilter(frame);
    }
    void LibHandler::doPostProcessLib(const std::shared_ptr<IFrame> &frame)
    {
        if (!bLibLoadSuccess)
            return;

        filter->doPostProcess(frame);
    }
    void LibHandler::doCloseProcessLib(const std::shared_ptr<IFrame> &frame)
    {
        if (!bLibLoadSuccess)
            return;

        filter->doCloseProcess(frame);
    }
    void LibHandler::doMotionTrackLib(const std::shared_ptr<IFrame> &frame)
    {
        if (!bLibLoadSuccess)
            return;

        filter->doMotionTrack(frame);
    }
    void LibHandler::doReflectiveFilterLib(const std::shared_ptr<IFrame> &frame)
    {
        if (!bLibLoadSuccess)
            return;

        filter->doReflectiveFilter(frame);
    }

    void LibHandler::doSpatialFilterLib(const std::shared_ptr<IFrame> &frame)
    {
        if (!bLibLoadSuccess)
            return;

        filter->doSpatialFilter(frame);
    }

    void LibHandler::setSpecParaLib(uint8_t sn[29])
    {
        if (!bLibLoadSuccess)
            return;

        filter->setSpecPara(sn);
    }

    void LibHandler::resetFiltersLib()
    {
        if (!bLibLoadSuccess)
            return;

        filter->resetFilters();
    }

    bool LibHandler::getLoadFlag()
    {
        return bLibLoadSuccess;
    }

}
