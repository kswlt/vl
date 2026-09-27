/**************************************************************************************
 * Copyright (C) 2022 Xintan Technology Corporation
 *
 * Author: Marco
 ***************************************************************************************/
#pragma once
#ifdef WIN32
#include <pcl/point_types.h>

#include <pcl/visualization/pcl_visualizer.h>
namespace XinTan
{

    struct XTPCL_PARAMTERS
    {
        float percent;
        float distance_check;
        float distance_devide;
        float outlier_timers;
        float mls_times;
        bool plane_on;
        bool outlier_on;
        bool smooth_on;
        uint8_t dllverion[3];
    };

    bool xtpcl_loaddll();

    void xtpcl_filter(const pcl::PointCloud<pcl::PointXYZI>::Ptr &cloud_in, pcl::PointCloud<pcl::PointXYZI>::Ptr cloudout);

    void xtpclfilter_setconfig(XTPCL_PARAMTERS paramters);

    void xtpclfilter_getconfig(XTPCL_PARAMTERS &paramters);

}
#endif
