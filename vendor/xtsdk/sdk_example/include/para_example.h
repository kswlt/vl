
#ifndef __PARA_ROS_H__
#define __PARA_ROS_H__
#include <string>

struct lidar_setting
{
    int imgType;
    int HDR;
    int minLSB;
    int cloud_coord;
    int int1, int2, int3, int4, intgs;

    int freq;

    int freq1, freq2, freq3, freq4;
    int cut_corner;
    int maxfps;
    int renderType;

    bool start_stream;
    bool hmirror;
    bool vmirror;

    std::string connect_address;
};
struct lidar_filter
{
    int medianSize;
    bool kalmanEnable;
    float kalmanFactor;
    int kalmanThreshold;
    bool edgeEnable;
    int edgeThreshold;
    bool dustEnable;
    int dustThreshold;
    int dustFrames;
    bool postprocessEnable;
    bool dynamicsEnabled;
    int dynamicsWinsize;
    int dynamicsMotionsize;
    float postprocessThreshold;
    bool reflectiveEnable;
    float ref_th_min;
    float ref_th_max;
    bool spatialEnable;
    float spatialAlpha;
    int spatialDelta;
    int spatialIterations;
    bool averageEnable;
    int averageSize;
};

struct para_example
{
    lidar_setting lidar_setting_;
    lidar_filter lidar_filter_;
};

#endif
