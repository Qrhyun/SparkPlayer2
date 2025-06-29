#ifndef AVSYNC_H
#define AVSYNC_H
#include <chrono>
#include <ctime>
#include <math.h>
using namespace std::chrono;

class AVSync
{
public:
    AVSync()
    {
    }
    ~AVSync()
    {
    }
     /**
     * @brief 初始化时钟
     * 将时钟重置为0，准备开始计时
     */
    void InitClock()
    {
        SetClock(0);
    }
     /**
     * @brief 设置时钟当前值
     * @param pts 当前的播放时间点，单位为秒
     * 
     * 通常在音频回调中被调用，将音频的当前播放时间点作为主时钟值
     */
    void SetClock(double pts)
    {
        double time = GetMicroseconds() / 1000000.0; //秒
        pts_drift_ = pts - time;
    }
    /**
     * @brief 获取当前时钟值
     * @return 当前时钟值，单位为秒
     * 
     * 通常在视频刷新循环中被调用，用于计算视频帧的显示时机
     */
    double GetClock()
    {
        double time = GetMicroseconds() / 1000000.0;
        return pts_drift_ + time;
    }

    // 微妙的单位
    time_t GetMicroseconds()
    {
        system_clock::time_point time_point_new = system_clock::now();  // 时间一直动  纪元通常是1970年1月1日午夜
        system_clock::duration duration = time_point_new.time_since_epoch();
        time_t us = duration_cast<microseconds>(duration).count();
        return us;
    }
    double pts_drift_ = 0;
};

#endif // AVSYNC_H
