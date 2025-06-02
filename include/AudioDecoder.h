#ifndef AUDIO_DECODER_H
#define AUDIO_DECODER_H

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
struct SwrContext;
struct AVAudioFifo;

// 音频帧回调函数类型
typedef std::function<void(const uint8_t *data, int size, int sampleRate, int channels)> AudioFrameCallback;

// 音频解码器类
class AudioDecoder
{
private:
    // 解码器上下文
    AVCodecContext *codecContext;

    // 解码器
    const AVCodec *codec;

    // 重采样上下文
    SwrContext *swrContext;

    // 音频FIFO
    AVAudioFifo *audioFifo;

    // 输入队列引用
    AudioPacketQueue &packetQueue;

    // 解码后的帧队列引用
    AudioFrameQueue &decodedFrameQueue;

    // 线程控制
    std::thread decodeThread;
    std::atomic<bool> isRunning;
    std::atomic<bool> isPaused;

    // 帧回调函数
    AudioFrameCallback frameCallback;

    // PCM文件输出
    std::string pcmFilePath;
    std::ofstream pcmFile;
    bool saveToPCM;

    // 直接PCM输出
    std::string directPcmOutput;

    // 私有方法
    bool initDecoder(AVCodecParameters *codecPar);
    void closeDecoder();
    void decodeThreadFunc();
    void savePCMData(const uint8_t *data, int size);
    void writePCMToFile(const uint8_t *data, int size, FILE *file);
    void processAudioSamples(const uint8_t *data, int samplesCount, int64_t pts);

public:
    // 构造函数和析构函数
    AudioDecoder(AudioPacketQueue &packetQueue, AudioFrameQueue &decodedFrameQueue);
    ~AudioDecoder();

    // 禁止拷贝和赋值
    AudioDecoder(const AudioDecoder &) = delete;
    AudioDecoder &operator=(const AudioDecoder &) = delete;

    // 公共方法
    bool init(AVCodecParameters *codecPar);
    void start();
    void stop();
    void pause(bool pause);

    // 设置帧回调
    void setFrameCallback(AudioFrameCallback callback);

    // 设置PCM文件输出
    bool setPCMOutput(const std::string &filePath);
    void closePCMOutput();

    // 直接PCM输出方法
    bool setDirectPCMOutput(const std::string &filePath);

    // 获取解码器信息
    int getSampleRate() const;
    int getChannels() const;
    const char *getCodecName() const;

    // 获取编解码器上下文
    AVCodecContext *getCodecContext() const;

    // 获取解码后的帧
    AVFrame *getFrame();

    // 检查队列是否为空
    bool isQueueEmpty() const;
};

#endif // AUDIO_DECODER_H
