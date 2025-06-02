#ifndef DEMUX_H
#define DEMUX_H

#include <string>
#include <thread>
#include <atomic>
#include "queue.h"

// 前向声明
struct AVFormatContext;
struct AVPacket;
struct AVCodecParameters;

/**
 * （顺序与定义顺序一致）
 * 媒体信息结构：
 *  视频信息：
 *     视频流索引
 *     视频宽度
 *     视频高度
 *     视频帧率
 *     视频编解码器参数
 *  音频信息：
 *     音频流索引
 *     音频采样率
 *     音频通道数
 *     音频编解码器参数
 *     总时长（秒）
 */
struct MediaInfo
{
    // 视频信息
    int videoStreamIndex;
    int width;
    int height;
    int fps;
    AVCodecParameters *videoCodecPar;

    // 音频信息
    int audioStreamIndex;
    int sampleRate;
    int channels;
    AVCodecParameters *audioCodecPar;

    // 总时长（秒）
    double duration;

    MediaInfo() : videoStreamIndex(-1), width(0), height(0), fps(0), videoCodecPar(nullptr),
                  audioStreamIndex(-1), sampleRate(0), channels(0), audioCodecPar(nullptr),
                  duration(0.0) {}
};

/**
 * 核心类：解复用模块(ffmpeg7.0)
 * 封装ffmpeg的解复用功能（将复合的媒体文件分离成独立的视频流和音频流）
 * 负责从视频文件中解读出视频流以及音频流并将其送入视频队列和音频队列
 * 成员变量：
 *  inputFile：输入文件路径
 *  formatContext：FFmpeg格式上下文
 *  mediaInfo：媒体信息
 *  videoQueue：视频队列
 *  audioQueue：音频队列
 *  demuxThread：解复用线程
 *  isRunning：是否运行
 *  isPaused：是否暂停
 *  isEOF：是否到达文件末尾
 */
class Demux
{
private:
    // 输入文件路径
    std::string inputFile;

    // FFmpeg相关结构
    AVFormatContext *formatContext;

    // 媒体信息
    MediaInfo mediaInfo;

    // 队列引用
    VideoPacketQueue &videoQueue;
    AudioPacketQueue &audioQueue;

    // 线程控制
    std::thread demuxThread;
    std::atomic<bool> isRunning;
    std::atomic<bool> isPaused;
    std::atomic<bool> isEOF;

    // 私有方法
    bool openInputFile();
    void closeInputFile();
    void demuxThreadFunc();

public:
    // 构造函数和析构函数
    Demux(const std::string &inputFile, VideoPacketQueue &videoQueue, AudioPacketQueue &audioQueue);
    ~Demux();

    // 禁止拷贝和赋值
    Demux(const Demux &) = delete;
    Demux &operator=(const Demux &) = delete;

    // 公共方法
    bool init();
    void start();
    void stop();
    void pause(bool pause);

    // 获取媒体信息
    const MediaInfo &getMediaInfo() const;

    // 检查解复用是否完成
    bool isFinished() const;
};

#endif // DEMUX_H
