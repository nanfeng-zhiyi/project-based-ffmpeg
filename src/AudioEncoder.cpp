#include "../include/AudioEncoder.h"
#include <iostream>

// 引入FFmpeg头文件
extern "C"
{
#include "ffmpeg/include_ffmpeg/libavcodec/avcodec.h"
#include "ffmpeg/include_ffmpeg/libavutil/opt.h"
#include "ffmpeg/include_ffmpeg/libavutil/channel_layout.h"
#include "ffmpeg/include_ffmpeg/libavutil/samplefmt.h"
#include "ffmpeg/include_ffmpeg/libavutil/frame.h"
#include "ffmpeg/include_ffmpeg/libavutil/error.h"
#include "ffmpeg/include_ffmpeg/libswresample/swresample.h"
#include "ffmpeg/include_ffmpeg/libavutil/audio_fifo.h"
}

// 构造函数
AudioEncoder::AudioEncoder(AudioFrameQueue &frameQueue, AudioPacketQueue &packetQueue)
    : codecContext(nullptr),
      codec(nullptr),
      swrContext(nullptr),
      frameQueue(frameQueue),
      packetQueue(packetQueue),
      isRunning(false),
      isPaused(false),
      frameCount(0),
      encodeCallback(nullptr),
      sampleRate(0),
      channels(0),
      channelLayout(0),
      bitRate(0),
      codecName(""),
      useFilter(false),
      audioFilter(nullptr),
      nextPts(0)
{
    std::cout << "音频编码器: 创建实例" << std::endl;
}

// 析构函数
AudioEncoder::~AudioEncoder()
{
    std::cout << "音频编码器: 销毁实例" << std::endl;
    stop();
    closeEncoder();
}

// 初始化编码器
bool AudioEncoder::init(int sampleRate, int channels, uint64_t channelLayout, int bitRate, const std::string &codecName)
{
    // 保存参数
    this->sampleRate = sampleRate;
    this->channels = channels;
    this->channelLayout = channelLayout;
    this->bitRate = bitRate;
    this->codecName = codecName;

    std::cout << "音频编码器: 开始初始化 " << sampleRate << "Hz, " << channels << "通道, " << bitRate / 1000 << "kbps, 编码器: " << codecName << std::endl;

    // 初始化编码器
    return initEncoder();
}

// 内部初始化编码器方法
bool AudioEncoder::initEncoder()
{
    // 打印FFmpeg版本信息
    std::cout << "音频编码器: FFmpeg版本信息:" << std::endl;
    std::cout << "  libavcodec: " << LIBAVCODEC_VERSION_MAJOR << "." << LIBAVCODEC_VERSION_MINOR << "." << LIBAVCODEC_VERSION_MICRO << std::endl;
    std::cout << "  libavutil: " << LIBAVUTIL_VERSION_MAJOR << "." << LIBAVUTIL_VERSION_MINOR << "." << LIBAVUTIL_VERSION_MICRO << std::endl;

    // 检查参数有效性
    if (sampleRate <= 0 || channels <= 0 || bitRate <= 0)
    {
        std::cerr << "音频编码器: 无效的参数 - 采样率: " << sampleRate
                  << ", 通道数: " << channels
                  << ", 比特率: " << bitRate << std::endl;
        return false;
    }

    std::cout << "音频编码器: 尝试初始化 - 采样率: " << sampleRate
              << ", 通道数: " << channels
              << ", 比特率: " << bitRate
              << ", 编码器: " << codecName << std::endl;

    // 查找编码器
    codec = avcodec_find_encoder_by_name(codecName.c_str());
    if (!codec)
    {
        std::cerr << "音频编码器: 找不到编码器 " << codecName << std::endl;

        // 尝试使用编码器ID
        std::cout << "音频编码器: 尝试使用编码器ID查找编码器..." << std::endl;
        if (codecName == "ac3" || codecName == "eac3")
        {
            codec = avcodec_find_encoder(AV_CODEC_ID_AC3);
        }
        else if (codecName == "aac")
        {
            codec = avcodec_find_encoder(AV_CODEC_ID_AAC);
        }
        else if (codecName == "mp3" || codecName == "libmp3lame")
        {
            codec = avcodec_find_encoder(AV_CODEC_ID_MP3);
        }
        else
        {
            std::cerr << "音频编码器: 不支持的编码器: " << codecName << std::endl;
            return false;
        }

        if (!codec)
        {
            std::cerr << "音频编码器: 无法找到编码器，尝试使用默认AC3编码器" << std::endl;
            codec = avcodec_find_encoder(AV_CODEC_ID_AC3);
            if (!codec)
            {
                std::cerr << "音频编码器: 无法找到AC3编码器" << std::endl;
                return false;
            }
        }
    }

    // 分配编码器上下文
    codecContext = avcodec_alloc_context3(codec);
    if (!codecContext)
    {
        std::cerr << "音频编码器: 无法分配编码器上下文" << std::endl;
        return false;
    }

    // 设置编码器参数
    codecContext->sample_fmt = AV_SAMPLE_FMT_FLTP; // AC3需要浮点平面格式
    codecContext->sample_rate = sampleRate;
    codecContext->channels = channels;
    codecContext->channel_layout = channelLayout;
    codecContext->bit_rate = bitRate;
    codecContext->time_base = (AVRational){1, sampleRate};

    // 打开编码器
    int ret = avcodec_open2(codecContext, codec, nullptr);
    if (ret < 0)
    {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errbuf, sizeof(errbuf));
        std::cerr << "音频编码器: 无法打开编码器: " << errbuf << std::endl;
        closeEncoder();
        return false;
    }

    // 初始化重采样上下文
    swrContext = swr_alloc();
    if (!swrContext)
    {
        std::cerr << "音频编码器: 无法分配重采样上下文" << std::endl;
        closeEncoder();
        return false;
    }

    // 设置重采样参数 (输入格式将在编码时设置)
    av_opt_set_int(swrContext, "out_sample_rate", codecContext->sample_rate, 0);
    av_opt_set_sample_fmt(swrContext, "out_sample_fmt", codecContext->sample_fmt, 0);
    av_opt_set_int(swrContext, "out_channel_layout", codecContext->channel_layout, 0);

    std::cout << "音频编码器: 成功初始化编码器 " << codecContext->codec->name << std::endl;
    std::cout << "  采样率: " << codecContext->sample_rate << " Hz" << std::endl;
    std::cout << "  通道数: " << codecContext->channels << std::endl;
    std::cout << "  采样格式: " << av_get_sample_fmt_name(codecContext->sample_fmt) << std::endl;
    std::cout << "  比特率: " << codecContext->bit_rate / 1000 << " kbps" << std::endl;

    return true;
}

// 关闭编码器
void AudioEncoder::closeEncoder()
{
    if (codecContext)
    {
        avcodec_free_context(&codecContext);
        codecContext = nullptr;
    }

    if (swrContext)
    {
        swr_free(&swrContext);
        swrContext = nullptr;
    }

    codec = nullptr;
}

// 设置音频滤镜
bool AudioEncoder::setAudioFilter(AudioFilter *filter)
{
    if (!filter)
    {
        useFilter = false;
        audioFilter = nullptr;
        return true;
    }

    audioFilter = filter;
    useFilter = true;
    return true;
}

// 编码单帧
bool AudioEncoder::encode(AVFrame *frame)
{
    if (!codecContext)
    {
        std::cerr << "音频编码器: 编码器未初始化" << std::endl;
        return false;
    }

    return encodeFrame(frame);
}

// 内部编码帧方法
bool AudioEncoder::encodeFrame(AVFrame *frame)
{
    if (!codecContext)
    {
        std::cerr << "音频编码器: 编码器未初始化" << std::endl;
        return false;
    }

    int ret;
    AVFrame *frameToEncode = nullptr;

    // 如果有滤镜并且帧不为空，应用滤镜
    if (useFilter && audioFilter && frame)
    {
        AVFrame *filteredFrame = av_frame_alloc();
        if (!filteredFrame)
        {
            std::cerr << "音频编码器: 无法分配滤镜帧" << std::endl;
            return false;
        }

        bool filterResult = audioFilter->processFrame(frame, filteredFrame);
        if (!filterResult)
        {
            av_frame_free(&filteredFrame);
            std::cerr << "音频编码器: 滤镜处理失败，使用原始帧" << std::endl;

            // 滤镜处理失败，使用原始帧
            frameToEncode = frame;
        }
        else
        {
            // 使用滤镜处理后的帧
            std::cout << "音频编码器: 使用滤镜处理后的帧" << std::endl;
            frameToEncode = filteredFrame;
        }
    }
    else
    {
        // 直接使用原始帧
        frameToEncode = frame;
    }

    // 检查帧是否为空（可能是EOF标记）
    if (!frameToEncode)
    {
        // 发送NULL帧表示结束编码
        ret = avcodec_send_frame(codecContext, nullptr);
        if (ret < 0)
        {
            char errbuf[AV_ERROR_MAX_STRING_SIZE];
            av_strerror(ret, errbuf, sizeof(errbuf));
            std::cerr << "音频编码器: 发送EOF帧失败: " << errbuf << std::endl;
            return false;
        }
    }
    else
    {
        // 设置正确的PTS值，确保单调递增
        if (frameToEncode->pts == AV_NOPTS_VALUE || frameToEncode->pts < nextPts)
        {
            frameToEncode->pts = nextPts;
        }

        // 更新下一个PTS值
        int64_t frameDuration = frameToEncode->nb_samples;
        nextPts = frameToEncode->pts + frameDuration;

        // 检查帧大小是否符合AC3编码器要求
        if (codecContext->codec_id == AV_CODEC_ID_AC3 &&
            frameToEncode->nb_samples != 1536 &&
            strcmp(codecContext->codec->name, "ac3") == 0)
        {
            std::cout << "音频编码器: 调整帧大小以符合AC3编码器要求，当前样本数: "
                      << frameToEncode->nb_samples << std::endl;

            // 创建一个新帧，样本数为1536
            AVFrame *adjustedFrame = av_frame_alloc();
            if (!adjustedFrame)
            {
                std::cerr << "音频编码器: 无法分配调整大小的帧" << std::endl;
                if (frameToEncode != frame)
                {
                    av_frame_free(&frameToEncode);
                }
                return false;
            }

            adjustedFrame->format = frameToEncode->format;
            adjustedFrame->channel_layout = frameToEncode->channel_layout;
            adjustedFrame->channels = frameToEncode->channels;
            adjustedFrame->sample_rate = frameToEncode->sample_rate;
            adjustedFrame->nb_samples = 1536;

            // 保持原始帧的PTS值
            adjustedFrame->pts = frameToEncode->pts;

            ret = av_frame_get_buffer(adjustedFrame, 0);
            if (ret < 0)
            {
                std::cerr << "音频编码器: 无法为调整大小的帧分配缓冲区" << std::endl;
                av_frame_free(&adjustedFrame);
                if (frameToEncode != frame)
                {
                    av_frame_free(&frameToEncode);
                }
                return false;
            }

            // 复制数据，确保不超出源帧的样本数
            int samples_to_copy = std::min(frameToEncode->nb_samples, 1536);
            for (int ch = 0; ch < frameToEncode->channels; ch++)
            {
                memcpy(adjustedFrame->data[ch],
                       frameToEncode->data[ch],
                       samples_to_copy * av_get_bytes_per_sample((AVSampleFormat)frameToEncode->format));

                // 如果样本数不足1536，用静音填充剩余部分
                if (samples_to_copy < 1536)
                {
                    memset(adjustedFrame->data[ch] + samples_to_copy * av_get_bytes_per_sample((AVSampleFormat)frameToEncode->format),
                           0,
                           (1536 - samples_to_copy) * av_get_bytes_per_sample((AVSampleFormat)frameToEncode->format));
                }
            }

            // 释放原始滤镜帧
            if (frameToEncode != frame)
            {
                av_frame_free(&frameToEncode);
            }

            // 使用调整后的帧
            frameToEncode = adjustedFrame;

            // 更新下一个PTS值，考虑到调整后的帧大小
            nextPts = frameToEncode->pts + 1536;

            std::cout << "音频编码器: 帧大小已调整为1536个样本" << std::endl;
        }

        // 发送帧到编码器
        ret = avcodec_send_frame(codecContext, frameToEncode);

        // 如果这是我们创建的帧（不是原始输入帧），释放它
        if (frameToEncode != frame)
        {
            av_frame_free(&frameToEncode);
        }
    }

    if (ret < 0)
    {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errbuf, sizeof(errbuf));
        std::cerr << "音频编码器: 发送帧失败: " << errbuf << std::endl;
        return false;
    }

    // 接收编码后的包
    while (ret >= 0)
    {
        AVPacket *packet = av_packet_alloc();
        if (!packet)
        {
            std::cerr << "音频编码器: 无法分配包" << std::endl;
            return false;
        }

        ret = avcodec_receive_packet(codecContext, packet);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
        {
            av_packet_free(&packet);
            break;
        }
        else if (ret < 0)
        {
            char errbuf[AV_ERROR_MAX_STRING_SIZE];
            av_strerror(ret, errbuf, sizeof(errbuf));
            std::cerr << "音频编码器: 接收包失败: " << errbuf << std::endl;
            av_packet_free(&packet);
            return false;
        }

        // 确保包的DTS是单调递增的
        if (packet->dts == AV_NOPTS_VALUE || packet->dts < 0)
        {
            packet->dts = packet->pts;
        }

        // 打印包信息，用于调试
        std::cout << "音频编码器: 生成音频包 PTS=" << packet->pts << ", DTS=" << packet->dts
                  << ", 大小=" << packet->size << " 字节" << std::endl;

        // 增加帧计数
        frameCount++;

        // 如果有回调函数，调用它
        if (encodeCallback)
        {
            encodeCallback(packet);
        }

        // 将包添加到队列
        packetQueue.push(packet);
    }

    return true;
}

// 发送EOF
void AudioEncoder::sendEOF()
{
    if (codecContext)
    {
        encodeFrame(nullptr);
    }
}

// 线程函数
void AudioEncoder::encodeThreadFunc()
{
    std::cout << "音频编码器: 编码线程启动" << std::endl;

    while (isRunning)
    {
        if (isPaused)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        // 从队列获取帧
        void *framePtr = frameQueue.pop();
        AVFrame *frame = static_cast<AVFrame *>(framePtr);
        if (!frame)
        {
            continue;
        }

        // 编码帧
        encodeFrame(frame);

        // 释放帧
        av_frame_free(&frame);
    }

    // 发送EOF
    sendEOF();

    std::cout << "音频编码器: 编码线程结束" << std::endl;
}

// 启动编码线程
void AudioEncoder::start()
{
    if (isRunning)
    {
        return;
    }

    isRunning = true;
    isPaused = false;
    encodeThread = std::thread(&AudioEncoder::encodeThreadFunc, this);
}

// 停止编码线程
void AudioEncoder::stop()
{
    if (!isRunning)
    {
        return;
    }

    isRunning = false;
    if (encodeThread.joinable())
    {
        encodeThread.join();
    }
}

// 暂停编码线程
void AudioEncoder::pause(bool pause)
{
    isPaused = pause;
}

// 设置编码回调
void AudioEncoder::setEncodeCallback(AudioEncodeCallback callback)
{
    encodeCallback = callback;
}

// 获取采样率
int AudioEncoder::getSampleRate() const
{
    return codecContext ? codecContext->sample_rate : sampleRate;
}

// 获取通道数
int AudioEncoder::getChannels() const
{
    return codecContext ? codecContext->channels : channels;
}

// 获取通道布局
uint64_t AudioEncoder::getChannelLayout() const
{
    return codecContext ? codecContext->channel_layout : channelLayout;
}

// 获取比特率
int AudioEncoder::getBitRate() const
{
    return codecContext ? codecContext->bit_rate : bitRate;
}

// 获取编码器名称
const char *AudioEncoder::getCodecName() const
{
    return codecContext && codecContext->codec ? codecContext->codec->name : codecName.c_str();
}

// 获取编码帧数
int AudioEncoder::getFrameCount() const
{
    return frameCount;
}

// 刷新编码器
void AudioEncoder::flush()
{
    if (codecContext)
    {
        sendEOF();
    }
}

// 获取编解码器上下文
AVCodecContext *AudioEncoder::getCodecContext() const
{
    return codecContext;
}