#include "demuxthread.h"

/**
 * @brief 构造函数，初始化解复用线程
 * @param audio_queue 音频数据包队列指针，存放解复用后的音频包
 * @param video_queue 视频数据包队列指针，存放解复用后的视频包
 */
DemuxThread::DemuxThread(AVPacketQueue *audio_queue, AVPacketQueue *video_queue):
    audio_queue_(audio_queue), video_queue_(video_queue)
{
    printf("DemuxThread\n");
    ifmt_ctx_ = nullptr;  // 初始化格式上下文为空
}

/**
 * @brief 析构函数，释放资源
 */
DemuxThread::~DemuxThread()
{
    printf("~DemuxThread\n");
    
    // 确保停止线程
    Stop();
    
    // 关闭并释放格式上下文
    if (ifmt_ctx_) {
        avformat_close_input(&ifmt_ctx_);  //自动将ifmt_ctx_ =nullptr;
    }
}

/**
 * @brief 初始化解复用线程
 * @param url 媒体文件路径或URL
 * @return 成功返回0，失败返回负值
 */
int DemuxThread::Init(const char *url)
{
    // 检查参数有效性
    if(!url) {
        printf("%s(%d) url is null\n", __FUNCTION__, __LINE__);
        return -1;
    }
    
    // 检查队列是否有效
    if(!audio_queue_ || !video_queue_) {
        printf("%s(%d) audio_queue_ or video_queue_  null\n", __FUNCTION__, __LINE__);
        return -1;
    }
    
    // 保存URL
    url_ = url;
    
    // 分配格式上下文
    ifmt_ctx_ = avformat_alloc_context();
    
    // 打开输入文件
    int ret = avformat_open_input(&ifmt_ctx_, url_.c_str(), NULL, NULL);
    if(ret < 0) {
        av_strerror(ret, err2str, sizeof(err2str));
        printf("%s(%d) avformat_open_input failed:%d, %s\n", __FUNCTION__, __LINE__, ret, err2str);
        return -1;
    }
    
    // 读取媒体文件信息
    ret = avformat_find_stream_info(ifmt_ctx_, NULL);
    if(ret < 0) {
        av_strerror(ret, err2str, sizeof(err2str));
        printf("%s(%d) avformat_find_stream_info failed:%d, %s\n", __FUNCTION__, __LINE__, ret, err2str);
        return -1;
    }
    
    // 打印媒体文件信息
    av_dump_format(ifmt_ctx_, 0, url_.c_str(), 0);
    
    // 查找最佳音频流
    audio_stream_ = av_find_best_stream(ifmt_ctx_, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
    
    // 查找最佳视频流
    video_stream_ = av_find_best_stream(ifmt_ctx_, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    
    printf("%s(%d) audio_stream_:%d, video_stream_:%d\n", __FUNCTION__, __LINE__, audio_stream_, video_stream_);
    
    // 检查是否找到音频或视频流
    if(audio_stream_ < 0 || video_stream_ < 0) {
        printf("no audio or no video\n");
        return -1;
    }
    
    return 0;
}

/**
 * @brief 启动解复用线程
 * @return 成功返回0，失败返回负值
 */
int DemuxThread::Start()
{
    // 创建新线程执行Run方法
    thread_ = new std::thread(&DemuxThread::Run, this);
    if(!thread_) {
        printf("new DemuxThread failed\n");
        return -1;
    }
    return 0;
}

/**
 * @brief 停止解复用线程
 * @return 成功返回0
 */
int DemuxThread::Stop()
{
    printf("%s(%d)\n", __FUNCTION__, __LINE__);
    // 调用基类的Stop方法，设置abort_标志并等待线程结束
    Thread::Stop();
    return 0;
}

/**
 * @brief 解复用线程主函数
 * 
 * 从媒体文件读取数据包，根据流类型分发到音频或视频队列
 */
void DemuxThread::Run()
{
    printf("DemuxThread::Run() into\n");
    
    AVPacket packet;
    int ret = 0;
    
    // 主解复用循环
    while(1) {
        // 检查是否需要退出
        if(abort_ == 1) {
            break;
        }
        
        // 如果队列已满，等待一段时间再继续
        if(audio_queue_->Size() > 100 || video_queue_->Size() > 100) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        
        // 读取一个数据包
        ret = av_read_frame(ifmt_ctx_, &packet);
        if(ret < 0) {
            av_strerror(ret, err2str, sizeof(err2str));
            printf("%s(%d) av_read_frame failed:%d, %s\n", __FUNCTION__, __LINE__, ret, err2str);
            break;
        }
        
        // 根据数据包所属的流类型，分发到相应的队列
        if(packet.stream_index == audio_stream_) {  // 音频包队列
            audio_queue_->Push(&packet);
//            printf("audio pkt size:%d\n", audio_queue_->Size());
        } else if(packet.stream_index == video_stream_) {  // 视频包队列
            video_queue_->Push(&packet);
//            printf("video pkt size:%d\n", video_queue_->Size());
        } else {
            // 其他类型的流，直接释放数据包
            av_packet_unref(&packet);
        }
    }
    
    // 资源释放移到析构函数中，避免重复关闭
    printf("DemuxThread::Run() leave\n");
}

/**
 * @brief 获取音频流的编解码参数
 * @return 音频流的编解码参数指针，如果没有音频流则返回NULL
 */
AVCodecParameters *DemuxThread::AudioCodecParameters()
{
    if(audio_stream_ != -1) {
        return ifmt_ctx_->streams[audio_stream_]->codecpar;
    } else {
        return NULL;
    }
}

/**
 * @brief 获取视频流的编解码参数
 * @return 视频流的编解码参数指针，如果没有视频流则返回NULL
 */
AVCodecParameters *DemuxThread::VideoCodecParameters()
{
    if(video_stream_ != -1) {
        return ifmt_ctx_->streams[video_stream_]->codecpar;
    } else {
        return NULL;
    }
}

/**
 * @brief 获取音频流的时间基准
 * @return 音频流的时间基准，用于时间戳转换
 */
AVRational DemuxThread::AudioStreamTimebase()
{
    if(audio_stream_ != -1) {
        return ifmt_ctx_->streams[audio_stream_]->time_base;
    } else {
        AVRational tb = {1, 1};
        return tb;
    }
}

/**
 * @brief 获取视频流的时间基准
 * @return 视频流的时间基准，用于时间戳转换
 */
AVRational DemuxThread::VideoStreamTimebase()
{
    if(video_stream_ != -1) {
        return ifmt_ctx_->streams[video_stream_]->time_base;
    } else {
        AVRational tb = {1, 1};
        return tb;
    }
}
