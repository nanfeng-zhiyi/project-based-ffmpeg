#ifndef VIDEO_ENCODER_H
#define VIDEO_ENCODER_H

#include <string>
#include <thread>
#include <atomic>
#include <functional>
#include "queue.h"
#include "../include/VideoFilter.h"

// 前向声明
struct AVCodecContext;
struct AVFrame;
struct AVPacket;
struct AVCodec;

// 视频编码回调函数类型
typedef std::function<void(AVPacket *)> VideoEncodeCallback;

// 视频编码器类
class VideoEncoder
{
private:
    // 编码器上下文
    AVCodecContext *codecContext;

    // 编码器
    const AVCodec *codec;

    // 输入帧队列引用
    VideoFrameQueue &frameQueue;

    // 输出队列引用
    VideoPacketQueue &packetQueue;

    // 线程控制
    std::thread encodeThread;
    std::atomic<bool> isRunning;
    std::atomic<bool> isPaused;

    // 帧计数
    int frameCount;

    // 编码回调函数
    VideoEncodeCallback encodeCallback;

    // 视频参数
    int width;
    int height;
    int frameRate;
    int bitRate;
    std::string codecName;

    // 视频滤镜
    bool useFilter;
    VideoFilter *videoFilter;

    // 私有方法
    bool initEncoder();
    void closeEncoder();
    void encodeThreadFunc();
    bool encodeFrame(AVFrame *frame);
    void sendEOF();

public:
    // 构造函数和析构函数
    VideoEncoder(VideoFrameQueue &frameQueue, VideoPacketQueue &packetQueue);
    ~VideoEncoder();

    // 禁止拷贝和赋值
    VideoEncoder(const VideoEncoder &) = delete;
    VideoEncoder &operator=(const VideoEncoder &) = delete;

    // 初始化方法
    bool init(int width, int height, int frameRate, int bitRate, const std::string &codecName = "libx264");

    // 设置视频滤镜
    bool setVideoFilter(VideoFilter *filter);

    // 编码单帧
    bool encode(AVFrame *frame);

    // 线程控制
    void start();
    void stop();
    void pause(bool pause);

    // 设置编码回调
    void setEncodeCallback(VideoEncodeCallback callback);

    // 获取编码器信息
    int getWidth() const;
    int getHeight() const;
    int getFrameRate() const;
    int getBitRate() const;
    const char *getCodecName() const;

    // 获取编码帧数
    int getFrameCount() const;

    // 刷新编码器
    void flush();

    // 获取编解码器上下文
    AVCodecContext *getCodecContext() const;
};

#endif // VIDEO_ENCODER_H