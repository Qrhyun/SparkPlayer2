#include <iostream>
#include "demuxthread.h"    // 解复用线程，负责从媒体文件读取数据包
#include "decodethread.h"   // 解码线程，负责解码音频和视频数据包
#include "audiooutput.h"    // 音频输出，负责播放音频数据
#include "videooutput.h"    // 视频输出，负责显示视频帧
#include "avsync.h"         // 音视频同步，维护统一的时钟基准
using namespace std;
#undef main               // 解决SDL重定义main的问题

/**
 * @brief 程序入口
 * @param argc 命令行参数数量
 * @param argv 命令行参数数组，argv[1]为要播放的媒体文件路径
 * @return 成功返回0，失败返回负值
 */
int main(int argc, char *argv[])
{
    cout << "Hello World!" << endl;
    printf("url :%s\n", argv[1]);  // 打印要播放的媒体文件路径
    int ret = 0;
    
    // 创建音视频数据包队列和帧队列，用于线程间数据传递
    AVPacketQueue audio_packet_queue;  // 音频数据包队列
    AVPacketQueue video_packet_queue;  // 视频数据包队列
    AVFrameQueue audio_frame_queue;    // 音频帧队列
    AVFrameQueue video_frame_queue;    // 视频帧队列
    
    AVSync avsync;                     // 音视频同步器
    
    // 创建并初始化解复用线程，负责读取媒体文件并分离音视频流
    DemuxThread *demux_thread = new DemuxThread(&audio_packet_queue, &video_packet_queue);
    ret = demux_thread->Init(argv[1]);  // 初始化解复用线程，打开媒体文件
    if(ret < 0) {
        printf("%s(%d) demux_thread Init\n", __FUNCTION__, __LINE__);
        return -1;
    }
    ret = demux_thread->Start();        // 启动解复用线程
    if(ret < 0) {
        printf("%s(%d) demux_thread Start\n", __FUNCTION__, __LINE__);
        return -1;
    }
    
    // 创建并初始化音频解码线程，负责解码音频数据包
    DecodeThread *audio_decode_thread = new DecodeThread(&audio_packet_queue, &audio_frame_queue);
    ret = audio_decode_thread->Init(demux_thread->AudioCodecParameters());  // 使用音频流参数初始化解码器
    if(ret < 0) {
        printf("%s(%d) audio_decode_thread Init\n", __FUNCTION__, __LINE__);
        return -1;
    }
    ret = audio_decode_thread->Start();  // 启动音频解码线程
    if(ret < 0) {
        printf("%s(%d) audio_decode_thread Start\n", __FUNCTION__, __LINE__);
        return -1;
    }
    
    // 创建并初始化视频解码线程，负责解码视频数据包
    DecodeThread *video_decode_thread = new DecodeThread(&video_packet_queue, &video_frame_queue);
    ret = video_decode_thread->Init(demux_thread->VideoCodecParameters());  // 使用视频流参数初始化解码器
    if(ret < 0) {
        printf("%s(%d) video_decode_thread Init\n", __FUNCTION__, __LINE__);
        return -1;
    }
    ret = video_decode_thread->Start();  // 启动视频解码线程
    if(ret < 0) {
        printf("%s(%d) video_decode_thread Start\n", __FUNCTION__, __LINE__);
        return -1;
    }
    
    // 初始化音视频同步时钟
    avsync.InitClock();
    
    // 设置音频参数，用于后续音频输出
    AudioParams audio_params;
    memset(&audio_params, 0, sizeof(audio_params));
    audio_params.ch_layout = audio_decode_thread->GetAVCodecContext()->ch_layout;   // 音频通道布局
    audio_params.fmt = audio_decode_thread->GetAVCodecContext()->sample_fmt;        // 音频采样格式
    audio_params.freq = audio_decode_thread->GetAVCodecContext()->sample_rate;      // 音频采样率
    
    // 创建并初始化音频输出，负责播放音频
    AudioOutput *audio_output = new AudioOutput(&avsync, audio_params, &audio_frame_queue, demux_thread->AudioStreamTimebase());
    ret = audio_output->Init();  // 初始化音频输出，设置SDL音频
    if(ret < 0) {
        printf("%s(%d) audio_output Init\n", __FUNCTION__, __LINE__);
        return -1;
    }
    
    // 创建并初始化视频输出，负责显示视频
    VideoOutput *video_output_ = new VideoOutput(&avsync, &video_frame_queue, video_decode_thread->GetAVCodecContext()->width,
            video_decode_thread->GetAVCodecContext()->height, demux_thread->VideoStreamTimebase());
    ret = video_output_->Init();  // 初始化视频输出，创建SDL窗口和渲染器
    if(ret < 0) {
        printf("%s(%d) video_output_ Init\n", __FUNCTION__, __LINE__);
        return -1;
    }
    
    // 进入视频主循环，此函数会阻塞直到用户退出
    video_output_->MainLoop();

    // 优化资源释放顺序，先停止所有线程，然后再清理资源
    printf("%s(%d) stopping threads\n", __FUNCTION__, __LINE__);
    // 先停止解码线程，因为它们依赖解复用线程提供数据
    video_decode_thread->Stop();
    audio_decode_thread->Stop();
    // 再停止解复用线程
    demux_thread->Stop();

    // 释放音频输出
    printf("%s(%d) cleaning audio output\n", __FUNCTION__, __LINE__);
    delete audio_output;
    
    // 释放视频输出
    printf("%s(%d) cleaning video output\n", __FUNCTION__, __LINE__);
    delete video_output_;    // 释放SDL视频资源
    
    // 显式调用队列的Abort方法释放队列中的资源
    printf("%s(%d) cleaning frame queues\n", __FUNCTION__, __LINE__);
    audio_frame_queue.Abort();  // 终止音频帧队列并释放内部资源
    video_frame_queue.Abort();  // 终止视频帧队列并释放内部资源
    
    printf("%s(%d) cleaning packet queues\n", __FUNCTION__, __LINE__);
    audio_packet_queue.Abort();  // 终止音频包队列并释放内部资源
    video_packet_queue.Abort();  // 终止视频包队列并释放内部资源
    
    // 删除线程对象
    printf("%s(%d) deleting thread objects\n", __FUNCTION__, __LINE__);
    delete audio_decode_thread;
    delete video_decode_thread;
    delete demux_thread;
    
    printf("main finish\n");
    return 0;
}
