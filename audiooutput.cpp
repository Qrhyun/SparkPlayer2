#include "audiooutput.h"

/**
 * @brief 构造函数，初始化音频输出对象
 * @param avsync 音视频同步器指针
 * @param aduio_params 源音频参数
 * @param frame_queue 音频帧队列指针
 * @param time_base 音频流时间基准
 */
AudioOutput::AudioOutput(AVSync *avsync, const AudioParams &aduio_params, AVFrameQueue *frame_queue, AVRational time_base)
    : avsync_(avsync), src_tgt_(aduio_params), frame_queue_(frame_queue), time_base_(time_base)
{
    swr_ctx_ = nullptr;           // 初始化重采样上下文为空
    audio_buf1_ = nullptr;        // 初始化音频缓冲区为空
    audio_buf1_size = 0;          // 初始化音频缓冲区大小为0
    audio_buf_ = nullptr;         // 初始化音频数据指针为空
    audio_buf_size = 0;           // 初始化音频数据大小为0
    audio_buf_index = 0;          // 初始化音频数据索引为0
}

/**
 * @brief 析构函数，释放资源
 */
AudioOutput::~AudioOutput()
{
    // 释放重采样上下文
    if (swr_ctx_) {
        swr_free(&swr_ctx_);
        swr_ctx_ = nullptr;
    }
    
    // 释放音频缓冲区
    if (audio_buf1_) {
        av_free(audio_buf1_);
        audio_buf1_ = nullptr;
        audio_buf1_size = 0;
    }
    
    // 确保调用了DeInit
    DeInit();
}

/**
 * @brief SDL音频回调函数，当SDL需要音频数据时调用
 * @param userdata 用户数据，此处为AudioOutput对象指针
 * @param stream 音频输出流缓冲区
 * @param len 需要的音频数据长度(字节)
 */
void sdl_audio_callback(void *userdata, Uint8 * stream, int len)
{
    AudioOutput *audio_output = (AudioOutput *)userdata;
//    printf("sdl_audio_callback len: %d\n", len);
    while(len > 0) {
        // 如果当前缓冲区已用完，需要获取新的音频帧
        if(audio_output->audio_buf_index == audio_output->audio_buf_size) {
            // 1. 读取pcm的数据
            audio_output->audio_buf_index = 0;
            AVFrame *frame = audio_output->frame_queue_->Pop(2);  // 从队列获取音频帧，最多等待2ms
            
            // 如果获取到了帧，设置播放时间戳
            if(frame) {
                audio_output->pts = frame->pts * av_q2d(audio_output->time_base_);
                
                // 2. 执行音频重采样
                // 2.1 初始化重采样器(如果需要)
                if(( (frame->format != audio_output->dst_tgt_.fmt)      // 采样格式不同
                     || (frame->sample_rate != audio_output->dst_tgt_.freq) // 采样率不同
                     || av_channel_layout_compare(&frame->ch_layout, &audio_output->dst_tgt_.ch_layout) != 0) // 通道布局不同
                   && (!audio_output->swr_ctx_)) {
                    // 配置并分配重采样器
                    swr_alloc_set_opts2(&audio_output->swr_ctx_,
                                        &audio_output->dst_tgt_.ch_layout,  // 输出通道布局
                                        audio_output->dst_tgt_.fmt,         // 输出采样格式
                                        audio_output->dst_tgt_.freq,        // 输出采样率
                                        &frame->ch_layout,                  // 输入通道布局
                                        (enum AVSampleFormat)frame->format, // 输入采样格式
                                        frame->sample_rate,                 // 输入采样率
                                        0, NULL);
                    // 初始化重采样器
                    if(!audio_output->swr_ctx_ || swr_init(audio_output->swr_ctx_) < 0) {
                        printf("swr_init failed");
                        if(audio_output->swr_ctx_) {
                            swr_free(&audio_output->swr_ctx_);
                        }
                        return;
                    }
                }
                
                // 如果需要重采样，执行重采样操作
                if(audio_output->swr_ctx_) {
                    // 需要重采样
                    const uint8_t **in = (const uint8_t **)frame->extended_data;  // 输入音频数据
                    uint8_t **out = &audio_output->audio_buf1_;                  // 输出缓冲区
                    // 计算输出样本数和所需缓冲区大小
                    int out_samples = frame->nb_samples * audio_output->dst_tgt_.freq / frame->sample_rate + 256;
                    int out_bytes = av_samples_get_buffer_size(NULL,
                                    audio_output->dst_tgt_.ch_layout.nb_channels,
                                    out_samples,
                                    audio_output->dst_tgt_.fmt, 0);
                    if(out_bytes < 0) {
                        printf("av_samples_get_buffer_size failed");
                        return;
                    }
                    
                    // 确保缓冲区足够大
                    av_fast_malloc(&audio_output->audio_buf1_, &audio_output->audio_buf1_size, out_bytes);
                    
                    // 执行重采样
                    int len2 = swr_convert(audio_output->swr_ctx_, out, out_samples, in, frame->nb_samples);
                    if(len2 < 0) {
                        printf("swr_convert failed\n");
                        return;
                    }
                    
                    // 计算实际输出大小
                    audio_output->audio_buf_size = av_samples_get_buffer_size(NULL,
                                                   audio_output->dst_tgt_.ch_layout.nb_channels,
                                                   len2,
                                                   audio_output->dst_tgt_.fmt, 0);
                    audio_output->audio_buf_ = audio_output->audio_buf1_;
                } else { // 不需要重采样
                    // 直接使用原始音频数据
                    int out_bytes = av_samples_get_buffer_size(NULL,
                                    frame->ch_layout.nb_channels,
                                    frame->nb_samples,
                                    (enum AVSampleFormat)frame->format, 0);
                    av_fast_malloc(&audio_output->audio_buf1_, &audio_output->audio_buf1_size, out_bytes);
                    audio_output->audio_buf_ = audio_output->audio_buf1_;
                    audio_output->audio_buf_size = out_bytes;
                    memcpy(audio_output->audio_buf_, frame->extended_data[0], out_bytes);
                }
                
                // 释放已处理的帧
                av_frame_free(&frame);
            } else {
                // 没有获取到帧，设置静音数据
                audio_output->audio_buf_ = NULL;
                audio_output->audio_buf_size = 512;
            }
        } // end of if(audio_output->audio_buf_index < audio_output->audio_buf_size)
        
        // 3. 将音频数据拷贝到SDL缓冲区
        int len3 = audio_output->audio_buf_size - audio_output->audio_buf_index;
        
        // 确保不超过请求的长度
        if(len3 > len) {
            len3 = len;
        }
        
        // 如果没有音频数据，生成静音
        if(!audio_output->audio_buf_) {
            memset(stream, 0, len3);
        } else {
            // 拷贝音频数据到SDL缓冲区
            memcpy(stream, audio_output->audio_buf_ + audio_output->audio_buf_index, len3);
        }
        
        // 更新剩余长度和位置
        len -= len3;
        audio_output->audio_buf_index += len3;
        stream += len3;
//        printf("len:%d, audio_buf_index:%d, %d\n", len, audio_output->audio_buf_index,
//               audio_output->audio_buf_size);
    }
    
    // 更新音频时钟作为主时钟
    printf("audio pts: %0.3lf\n", audio_output->pts);
    audio_output->avsync_->SetClock(audio_output->pts);
}

/**
 * @brief 初始化音频输出
 * @return 成功返回0，失败返回负值
 */
int AudioOutput::Init()
{
    // 初始化SDL音频子系统
    if(SDL_Init(SDL_INIT_AUDIO) != 0) {
        printf("SDL_Init failed\n");
        return -1;
    }
    
    // 配置SDL音频规格
    SDL_AudioSpec wanted_spec;
    wanted_spec.channels = 2;                    // 2通道立体声
    wanted_spec.freq = src_tgt_.freq;            // 采样率
    wanted_spec.format = AUDIO_S16SYS;           // 16位有符号整数格式
    wanted_spec.silence = 0;                     // 静音值
    wanted_spec.callback = sdl_audio_callback;   // 设置回调函数
    wanted_spec.userdata = this;                 // 回调函数的用户数据
    wanted_spec.samples = 1024;                 // 每次回调的采样数 2*2*1024 = 4096字节
    
    // 打开音频设备
    int ret = SDL_OpenAudio(&wanted_spec, NULL);
    if(ret != 0) {
        printf("SDL_OpenAudio failed\n");
        return -1;
    }
    
    // 设置目标音频参数，用于重采样
    av_channel_layout_default(&dst_tgt_.ch_layout, wanted_spec.channels);
    dst_tgt_.fmt = AV_SAMPLE_FMT_S16;      // 设置为SDL要求的16位有符号格式
    dst_tgt_.freq = wanted_spec.freq;      // 保持与SDL一致的采样率
    
    // 开始播放音频
    SDL_PauseAudio(0);
    printf("AudioOutput::Init() finish\n");
    return 0;
}

/**
 * @brief 释放音频输出资源
 * @return 成功返回0
 */
int AudioOutput::DeInit()
{
    // 暂停音频播放
    SDL_PauseAudio(1);
    // 关闭音频设备
    SDL_CloseAudio();
    printf("AudioOutput::DeInit() finish\n");
    return 0;
}
