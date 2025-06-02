#ifndef MUXER_H
#define MUXER_H

#include <string>
#include <thread>
#include <atomic>
#include <functional>
#include "queue.h"

// 前向声明
struct AVFormatContext;
struct AVCodecContext;
struct AVStream;
struct AVPacket;
struct AVRational;

// 复用器类
class Muxer
{
private:
    // 输出格式上下文
    AVFormatContext *formatContext;

    // 视频流
    AVStream *videoStream;
    AVCodecContext *videoCodecContext;

    // 音频流
    AVStream *audioStream;
    AVCodecContext *audioCodecContext;

    // 输入队列引用
    VideoPacketQueue &videoPacketQueue;
    AudioPacketQueue &audioPacketQueue;

    // 线程控制
    std::thread muxThread;
    std::atomic<bool> isRunning;
    std::atomic<bool> isPaused;

    // 输出文件路径
    std::string outputFile;

    // 包计数
    int videoPacketCount;
    int audioPacketCount;

    // 播放速度
    double playbackSpeed;

    // 上一个时间戳，用于确保时间戳单调递增
    int64_t lastVideoPts;
    int64_t lastVideoDts;
    int64_t lastAudioPts;
    int64_t lastAudioDts;

    // 时间基转换
    int64_t rescaleTimestamp(int64_t timestamp, const AVRational &srcTimeBase, const AVRational &dstTimeBase, bool isVideo = true);

    // 私有方法
    bool initMuxer();
    void closeMuxer();
    void muxThreadFunc();
    bool writePacket(AVPacket *packet, bool isVideo);
    bool finalizeFile();

public:
    // 构造函数和析构函数
    Muxer(VideoPacketQueue &videoQueue, AudioPacketQueue &audioQueue);
    ~Muxer();

    // 禁止拷贝和赋值
    Muxer(const Muxer &) = delete;
    Muxer &operator=(const Muxer &) = delete;

    // 初始化方法
    bool init(const std::string &outputFile, AVCodecContext *videoCodecCtx, AVCodecContext *audioCodecCtx = nullptr);

    // 线程控制
    void start();
    void stop();
    void pause(bool pause);

    // 获取统计信息
    int getVideoPacketCount() const;
    int getAudioPacketCount() const;
    std::string getOutputFile() const;

    // 检查复用器是否正在运行
    bool isActive() const;

    // 设置播放速度
    void setPlaybackSpeed(double speed);

    // 获取当前播放速度
    double getPlaybackSpeed() const;
};

#endif // MUXER_H