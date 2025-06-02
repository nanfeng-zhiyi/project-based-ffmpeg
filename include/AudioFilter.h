#ifndef AUDIO_FILTER_H
#define AUDIO_FILTER_H

#include <string>
#include <functional>

// 前向声明
struct AVFrame;
struct AVFilterContext;
struct AVFilterGraph;
struct AVFilterInOut;

// 音频滤镜回调函数类型
typedef std::function<void(AVFrame *)> AudioFilterCallback;

// 音频滤镜类
class AudioFilter
{
private:
    // 滤镜图表
    AVFilterGraph *filterGraph;

    // 输入和输出滤镜上下文
    AVFilterContext *bufferSrcContext;
    AVFilterContext *bufferSinkContext;

    // 输入和输出
    AVFilterInOut *inputs;
    AVFilterInOut *outputs;

    // 音频参数
    int sampleRate;
    int channels;
    uint64_t channelLayout;
    int sampleFormat;

    // 滤镜描述
    std::string filterDesc;

    // 播放速度
    double playbackSpeed;

    // 帧回调函数
    AudioFilterCallback frameCallback;

    // 私有方法
    bool initFilter();
    void closeFilter();
    std::string buildFilterString();

public:
    // 构造函数和析构函数
    AudioFilter();
    ~AudioFilter();

    // 禁止拷贝和赋值
    AudioFilter(const AudioFilter &) = delete;
    AudioFilter &operator=(const AudioFilter &) = delete;

    // 初始化滤镜
    bool init(int sampleRate, int channels, uint64_t channelLayout, int sampleFormat, const std::string &filterDesc);

    // 处理帧
    bool processFrame(AVFrame *inputFrame, AVFrame *outputFrame);

    // 设置帧回调
    void setFrameCallback(AudioFilterCallback callback);

    // 获取滤镜描述
    std::string getFilterDescription() const;

    // 设置播放速度
    bool setPlaybackSpeed(double speed);

    // 获取当前播放速度
    double getPlaybackSpeed() const;

    // 应用自定义滤镜
    bool applyCustomFilter(const std::string &customFilterDesc);
};

#endif // AUDIO_FILTER_H