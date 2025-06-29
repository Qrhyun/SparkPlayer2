#include "decodethread.h"

/**
 * @brief 构造函数，初始化解码线程
 * @param packet_queue 数据包队列指针，作为解码输入
 * @param frame_queue 帧队列指针，作为解码输出
 */
DecodeThread::DecodeThread(AVPacketQueue *packet_queue, AVFrameQueue  *frame_queue):
    packet_queue_(packet_queue), frame_queue_(frame_queue)
{
    codec_ctx_ = nullptr;  // 初始化编解码器上下文为空
}

/**
 * @brief 析构函数，释放资源
 */
DecodeThread::~DecodeThread()
{
    // 确保停止线程
    Stop();
    
    // 释放编解码器上下文
    if (codec_ctx_) {
        avcodec_free_context(&codec_ctx_);
        codec_ctx_ = nullptr;
    }
}

/**
 * @brief 初始化解码器
 * @param par 编解码器参数
 * @return 成功返回0，失败返回负值
 */
int DecodeThread::Init(AVCodecParameters *par)
{
    // 检查参数有效性
    if(!par) {
        printf("DecodeThread::Init par is NULL\n");
        return -1;
    }
    
    // 分配编解码器上下文
    codec_ctx_ = avcodec_alloc_context3(NULL);
    
    // 将流参数复制到编解码器上下文
    int ret = avcodec_parameters_to_context(codec_ctx_, par);
    // avcodec_parameters_from_context // 合成复用的时候用
    if(ret < 0) {
        av_strerror(ret, err2str, sizeof(err2str));
        printf("avcodec_parameters_to_context failed, ret:%d, err2str:%s", ret, err2str);
        return -1;
    }
    
    // 根据编解码器ID查找解码器
    const AVCodec *codec = avcodec_find_decoder(codec_ctx_->codec_id);
    if(!codec) {
        printf("avcodec_find_decoder failed\n");
        return -1;
    }
    
    // 打开解码器
    ret = avcodec_open2(codec_ctx_, codec, NULL);
    if(ret < 0) {
        av_strerror(ret, err2str, sizeof(err2str));
        printf("avcodec_open2 failed, ret:%d, err2str:%s", ret, err2str);
        return -1;
    }
    
    printf("Init decode finish\n");
    return 0;
}

/**
 * @brief 启动解码线程
 * @return 成功返回0，失败返回负值
 */
int DecodeThread::Start()
{
    // 创建新线程执行Run方法
    thread_ = new std::thread(&DecodeThread::Run, this);
    if(!thread_) {
        printf("new DecodeThread failed\n");
        return -1;
    }
    return 0;
}

/**
 * @brief 停止解码线程
 * @return 成功返回0
 */
int DecodeThread::Stop()
{
    printf("%s(%d)\n", __FUNCTION__, __LINE__);
    // 调用基类的Stop方法，设置abort_标志并等待线程结束
    Thread::Stop();
    return 0;
}

/**
 * @brief 解码线程主函数
 * 
 * 从packet_queue获取数据包，解码成帧，然后放入frame_queue
 */
void DecodeThread::Run()
{
    int ret = 0;
    // 分配一个用于存放解码结果的帧
    AVFrame *frame = av_frame_alloc();
    
    // 主解码循环
    while(1) {
        // 检查是否需要退出
        if(abort_ == 1) {
            break;
        }
        
        // 如果帧队列已满，等待一段时间再继续
        // 1920*1080*1.5*100 (一帧YUV占用大小约为宽*高*1.5字节)
        if(frame_queue_->Size() > 10) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        
        // 从packet_queue读取数据包
        AVPacket *packet = packet_queue_->Pop(10);  // 最多等待10ms
        if(packet) {
            // 送给解码器
            ret = avcodec_send_packet(codec_ctx_, packet);
            // 释放数据包(已经送入解码器)
            av_packet_free(&packet);
            
            if(ret < 0) {
                av_strerror(ret, err2str, sizeof(err2str));
                printf("avcodec_send_packet failed, ret:%d, err2str:%s", ret, err2str);
                break;
            }
            
            // 从解码器读取解码后的帧
            while (true) {
                ret = avcodec_receive_frame(codec_ctx_, frame);  // 存在B帧的场景  B3-2  P2-3   I1-1 --> P3  B2  I1
                if(ret == 0) {
                    // 成功解码到一帧，放入帧队列
                    frame_queue_->Push(frame);
//                    printf("%s frame_queue size:%d\n ", codec_ctx_->codec->name, frame_queue_->Size());
                    continue;
                } else if(ret == AVERROR(EAGAIN)) {
                    // 需要更多数据包才能产生下一帧，跳出内层循环
                    break;
                } else {
                    // 其他错误，设置终止标志并跳出循环
                    abort_  = 1;
                    av_strerror(ret, err2str, sizeof(err2str));
                    printf("avcodec_receive_frame failed, ret:%d, err2str:%s", ret, err2str);
                    break;
                }
            }
            // 把frame发送给framequeue
        } else {
            printf("no packet\n");
        }
    }
    
    // 退出前释放frame资源
    if (frame) {
        av_frame_free(&frame);
    }
}

/**
 * @brief 获取解码器上下文
 * @return 解码器上下文指针
 */
AVCodecContext *DecodeThread::GetAVCodecContext()
{
    return codec_ctx_;
}
