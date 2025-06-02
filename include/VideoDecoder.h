#ifndef VIDEO_DECODER_H
#define VIDEO_DECODER_H

#include <string>
#include <thread>
#include <atomic>
#include <functional>
#include <fstream>
#include "queue.h"

// 前向声明
struct AVCodecContext;
struct AVFrame;
struct AVCodecParameters;
struct AVCodec;
struct AVPacket;

// 视频帧回调函数类型
typedef std::function<void(AVFrame *)> VideoFrameCallback;

// 视频解码器类
class VideoDecoder
{
private:
    // 解码器上下文
    AVCodecContext *codecContext;

    // 解码器
    const AVCodec *codec;

    // 输入队列引用
    VideoPacketQueue &packetQueue;

    // 解码后的帧队列引用
    VideoFrameQueue &decodedFrameQueue;

    // 线程控制
    std::thread decodeThread;
    std::atomic<bool> isRunning;
    std::atomic<bool> isPaused;

    // 帧回调函数
    VideoFrameCallback frameCallback;

    // YUV文件输出
    std::string yuvFilePath;
    std::ofstream yuvFile;
    bool saveToFile;

    // 直接YUV输出文件路径
    std::string directYuvOutput;

    // 私有方法
    bool initDecoder(AVCodecParameters *codecPar);
    void closeDecoder();
    void decodeThreadFunc();
    void saveFrameToYUV(AVFrame *frame);
    void writeFrameToYUVFile(AVFrame *frame, FILE *file);

public:
    // 构造函数和析构函数
    VideoDecoder(VideoPacketQueue &packetQueue, VideoFrameQueue &decodedFrameQueue);
    ~VideoDecoder();

    // 禁止拷贝和赋值
    VideoDecoder(const VideoDecoder &) = delete;
    VideoDecoder &operator=(const VideoDecoder &) = delete;

    // 公共方法
    bool init(AVCodecParameters *codecPar);
    void start();
    void stop();
    void pause(bool pause);

    // 设置帧回调
    void setFrameCallback(VideoFrameCallback callback);

    // 设置YUV文件输出
    bool setYUVOutput(const std::string &filePath);
    void closeYUVOutput();

    // 直接YUV输出方法
    bool setDirectYUVOutput(const std::string &filePath);

    // 获取解码器信息
    int getWidth() const;
    int getHeight() const;
    double getFrameRate() const;
    const char *getCodecName() const;

    // 获取解码后的帧
    AVFrame *getFrame();

    // 检查队列是否为空
    bool isQueueEmpty() const;
};

#endif // VIDEO_DECODER_H
