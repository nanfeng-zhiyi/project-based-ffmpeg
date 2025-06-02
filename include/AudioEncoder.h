#ifndef AUDIO_ENCODER_H
#define AUDIO_ENCODER_H

#include <string>
#include <thread>
#include <atomic>
#include <functional>
#include "queue.h"
#include "../include/AudioFilter.h"

// 前向声明
struct AVCodecContext;
struct AVFrame;
struct AVPacket;
struct AVCodec;
struct SwrContext;

// 音频编码回调函数类型
typedef std::function<void(AVPacket *)> AudioEncodeCallback;

// 音频编码器类
class AudioEncoder
{
private:
    // 编码器上下文
    AVCodecContext *codecContext;

    // 编码器
    const AVCodec *codec;

    // 重采样上下文
    SwrContext *swrContext;

    // 输入帧队列引用
    AudioFrameQueue &frameQueue;

    // 输出队列引用
    AudioPacketQueue &packetQueue;

    // 线程控制
    std::thread encodeThread;
    std::atomic<bool> isRunning;
    std::atomic<bool> isPaused;

    // 帧计数
    int frameCount;

    // 编码回调函数
    AudioEncodeCallback encodeCallback;

    // 音频参数
    int sampleRate;
    int channels;
    uint64_t channelLayout;
    int bitRate;
    std::string codecName;

    // 音频滤镜
    bool useFilter;
    AudioFilter *audioFilter;

    // 时间戳跟踪
    int64_t nextPts;

    // 私有方法
    bool initEncoder();
    void closeEncoder();
    void encodeThreadFunc();
    bool encodeFrame(AVFrame *frame);
    void sendEOF();

public:
    // 构造函数和析构函数
    AudioEncoder(AudioFrameQueue &frameQueue, AudioPacketQueue &packetQueue);
    ~AudioEncoder();

    // 禁止拷贝和赋值
    AudioEncoder(const AudioEncoder &) = delete;
    AudioEncoder &operator=(const AudioEncoder &) = delete;

    // 初始化方法
    bool init(int sampleRate, int channels, uint64_t channelLayout, int bitRate, const std::string &codecName = "ac3");

    // 设置音频滤镜
    bool setAudioFilter(AudioFilter *filter);

    // 编码单帧
    bool encode(AVFrame *frame);

    // 线程控制
    void start();
    void stop();
    void pause(bool pause);

    // 设置编码回调
    void setEncodeCallback(AudioEncodeCallback callback);

    // 获取编码器信息
    int getSampleRate() const;
    int getChannels() const;
    uint64_t getChannelLayout() const;
    int getBitRate() const;
    const char *getCodecName() const;

    // 获取编码帧数
    int getFrameCount() const;

    // 刷新编码器
    void flush();

    // 获取编解码器上下文
    AVCodecContext *getCodecContext() const;
};

#endif // AUDIO_ENCODER_H