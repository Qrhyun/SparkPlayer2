#include "videooutput.h"
#include <thread>

/**
 * @brief 构造函数，初始化视频输出对象
 * @param avsync 音视频同步器指针
 * @param frame_queue 视频帧队列指针
 * @param video_width 视频宽度
 * @param video_height 视频高度
 * @param time_base 视频流时间基准
 */
VideoOutput::VideoOutput(AVSync *avsync, AVFrameQueue *frame_queue,
                         int video_width, int video_height, AVRational time_base):
    avsync_(avsync), frame_queue_(frame_queue), video_width_(video_width), video_height_(video_height), time_base_(time_base)
{
    texture_ = nullptr;    // 初始化纹理为空
    renderer_ = nullptr;   // 初始化渲染器为空
    win_ = nullptr;        // 初始化窗口为空
}

/**
 * @brief 析构函数，释放资源
 */
VideoOutput::~VideoOutput()
{
    // 释放SDL资源
    if (texture_) {
        SDL_DestroyTexture(texture_);
        texture_ = nullptr;
    }
    
    if (renderer_) {
        SDL_DestroyRenderer(renderer_);
        renderer_ = nullptr;
    }
    
    if (win_) {
        SDL_DestroyWindow(win_);
        win_ = nullptr;
    }
}

/**
 * @brief 初始化视频输出
 * @return 成功返回0，失败返回负值
 */
int VideoOutput::Init()
{
    // 初始化SDL视频子系统
    if(SDL_Init(SDL_INIT_VIDEO))  {
        printf("SDL_Init failed\n");
        return -1;
    }
    
    // 创建窗口
    win_ = SDL_CreateWindow("player", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                            video_width_, video_height_, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    if(!win_) {
        printf("SDL_CreateWindow failed\n");
        return -1;
    }
    
    // 创建渲染器
    renderer_ = SDL_CreateRenderer(win_, -1, 0);
    if(!renderer_) {
        printf("SDL_CreateRenderer failed\n");
        return -1;
    }
    
    // 创建纹理，用于显示YUV格式的视频帧
    texture_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING, video_width_, video_height_);
    if(!texture_) {
        printf("SDL_CreateRenderer failed\n");
        return -1;
    }
    
    return 0;
}

/**
 * @brief 释放视频输出资源
 */
void VideoOutput::DeInit()
{
    // 释放纹理
    if(texture_) {
        SDL_DestroyTexture(texture_);
        texture_ = nullptr;
    }
    
    // 释放渲染器
    if(renderer_) {
        SDL_DestroyRenderer(renderer_);
        renderer_ = nullptr;
    }
    
    // 释放窗口
    if(win_) {
        SDL_DestroyWindow(win_);
        win_ = nullptr;
    }
    
    // 退出SDL
    SDL_Quit();
}

/**
 * @brief 视频主循环，处理事件并刷新显示
 * @return 成功返回0
 */
int VideoOutput::MainLoop()
{
    SDL_Event event;
    
    // 主事件循环
    while(true) {
        // 处理事件并刷新显示
        RefreshLoopWaitEvent(&event);
        
        // 根据事件类型做相应处理
        switch (event.type) {
            case SDL_KEYDOWN:
                // ESC键退出
                if(event.key.keysym.sym == SDLK_ESCAPE) {
                    printf("esc key down\n");
                    return 0;
                }
                break;
            case SDL_QUIT:
                // 窗口关闭事件
                printf("SDL_QUIT\n");
                return 0;
                break;
            default:
                break;
        }
    }
    
    return 0;
}

// 0.01秒循环一次，定义刷新率
#define REFRESH_RATE 0.01

/**
 * @brief 等待并处理事件，同时刷新视频显示
 * @param event SDL事件指针，用于接收事件
 */
void VideoOutput::RefreshLoopWaitEvent(SDL_Event *event)
{
    double remain_time = 0.0; // 下一帧等待时间，单位为秒
    
    // 获取所有待处理的事件
    SDL_PumpEvents();
    
    // 当没有事件时，刷新显示
    while(!SDL_PeepEvents(event, 1, SDL_GETEVENT, SDL_FIRSTEVENT, SDL_LASTEVENT)) {
        // 如果需要等待，则延时适当时间
        if(remain_time > 0.0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(int64_t(remain_time * 1000)));
        }
        
        // 刷新视频帧
        videoRefresh(remain_time);
        
        // 再次获取事件
        SDL_PumpEvents();
    }
}

/**
 * @brief 刷新视频帧
 * @param remain_time 引用参数，返回下一帧应该等待的时间
 * 
 * 此函数负责音视频同步和视频帧的渲染
 */
void VideoOutput::videoRefresh(double &remain_time)
{
    AVFrame *frame = NULL;
    
    // 获取队列中的第一帧但不移除
    frame = frame_queue_->Front();
    
    if(frame) {
        // 计算视频帧的显示时间点，单位为秒
        double pts = frame->pts * av_q2d(time_base_);
        
        // 计算当前帧与音频时钟的时间差
        double diff = pts - avsync_->GetClock();
        printf("video pts:%0.3lf, diff:%0.3f\n", pts, diff);
        
        // 如果视频帧还没到显示时间，等待
        if(diff > 0) { // 如diff = 0.005秒，表示视频比音频快了5ms
            remain_time = diff;
            
            // 限制最大等待时间为刷新率
            if(remain_time > REFRESH_RATE) {
                remain_time = REFRESH_RATE;
            }
            return;
        }
        
        // 到达或超过显示时间，渲染当前帧
        
        // 准备渲染区域
        SDL_Rect rect;
        rect.x = 0;
        rect.y = 0;
        rect.w = video_width_;
        rect.h = video_height_;
        
        // 更新纹理数据
        SDL_UpdateYUVTexture(texture_, &rect, frame->data[0], frame->linesize[0],
                             frame->data[1], frame->linesize[1],
                             frame->data[2], frame->linesize[2]);
        
        // 清空渲染器
        SDL_RenderClear(renderer_);
        
        // 将纹理复制到渲染器
        SDL_RenderCopy(renderer_, texture_, NULL, &rect);
        
        // 将渲染器的内容呈现到窗口
        SDL_RenderPresent(renderer_);
        
        // 显示完成后，从队列中取出并释放该帧
        frame = frame_queue_->Pop(1);
        av_frame_free(&frame);
    }
}
