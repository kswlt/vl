/**************************************************************************************
 * Copyright (C) 2022 Xintan Technology Corporation
 *
 * Author: Marco
 ***************************************************************************************/

#include "xtpcl_filter.h"
#ifdef WIN32
#include <windows.h>

namespace XinTan
{

    typedef VOID (*PFUNXTPCL_FILTER)(const pcl::PointCloud<pcl::PointXYZI>::Ptr &cloud_in, pcl::PointCloud<pcl::PointXYZI>::Ptr cloudout);
    typedef VOID (*PFUNXTPCL_FILTER_CONFIG)(XTPCL_PARAMTERS paramters);
    typedef VOID (*PFUNXTPCL_FILTER_GETCONFIG)(XTPCL_PARAMTERS &paramters);

    PFUNXTPCL_FILTER xtpcl_filterd = nullptr;
    PFUNXTPCL_FILTER_CONFIG xtpclfilter_configd = nullptr;
    PFUNXTPCL_FILTER_GETCONFIG xtpclfilter_getconfigd = nullptr;

    bool xtpcl_loaddll()
    {
        HMODULE hModule = LoadLibrary("pointclouds_process_shared.dll");

        if (hModule != NULL)
        {
            xtpcl_filterd = (PFUNXTPCL_FILTER)GetProcAddress(hModule, "xtpcl_filter");
            xtpclfilter_configd = (PFUNXTPCL_FILTER_CONFIG)GetProcAddress(hModule, "xtpclfilter_setconfig");
            xtpclfilter_getconfigd = (PFUNXTPCL_FILTER_GETCONFIG)GetProcAddress(hModule, "xtpclfilter_getconfig");

            return true;
        }
        return false;
    }

    void xtpcl_filter(const pcl::PointCloud<pcl::PointXYZI>::Ptr &cloud_in, pcl::PointCloud<pcl::PointXYZI>::Ptr cloudout)
    {
        if (xtpcl_filterd != nullptr)
            xtpcl_filterd(cloud_in, cloudout);
    }

    void xtpclfilter_setconfig(XTPCL_PARAMTERS paramters)
    {
        if (xtpclfilter_configd != nullptr)
            xtpclfilter_configd(paramters);
    }

    void xtpclfilter_getconfig(XTPCL_PARAMTERS &paramters)
    {
        if (xtpclfilter_getconfigd != nullptr)
            xtpclfilter_getconfigd(paramters);
    }

} // end namespace XinTan
#endif