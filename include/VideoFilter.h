#ifndef VIDEO_FILTER_H
#define VIDEO_FILTER_H

#include <string>
#include <functional>

// 前向声明
struct AVFrame;
struct AVFilterContext;
struct AVFilterGraph;
struct AVFilterInOut;

// 视频滤镜回调函数类型
typedef std::function<void(AVFrame *)> VideoFilterCallback;

// 旋转角度枚举
enum class RotationAngle
{
    ROTATE_0 = 0,     // 不旋转
    ROTATE_90 = 90,   // 顺时针旋转90度
    ROTATE_180 = 180, // 顺时针旋转180度
    ROTATE_270 = 270  // 顺时针旋转270度
};

// 视频滤镜类
class VideoFilter
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

    // 视频参数
    int width;
    int height;
    int pixFmt;
    double frameRate;

    // 滤镜描述
    std::string filterDesc;

    // 当前旋转角度
    RotationAngle currentRotation;

    // 播放速度
    double playbackSpeed;

    // 帧回调函数
    VideoFilterCallback frameCallback;

    // 私有方法
    bool initFilter();
    void closeFilter();
    std::string buildFilterString();

public:
    // 构造函数和析构函数
    VideoFilter();
    ~VideoFilter();

    // 禁止拷贝和赋值
    VideoFilter(const VideoFilter &) = delete;
    VideoFilter &operator=(const VideoFilter &) = delete;

    // 初始化滤镜
    bool init(int width, int height, int pixFmt, double frameRate, const std::string &filterDesc);

    // 处理帧
    bool processFrame(AVFrame *inputFrame, AVFrame *outputFrame);

    // 设置帧回调
    void setFrameCallback(VideoFilterCallback callback);

    // 获取滤镜描述
    std::string getFilterDescription() const;

    // 旋转视频
    bool setRotation(RotationAngle angle);

    // 获取当前旋转角度
    RotationAngle getRotation() const;

    // 设置播放速度
    bool setPlaybackSpeed(double speed);

    // 获取当前播放速度
    double getPlaybackSpeed() const;

    // 应用自定义滤镜
    bool applyCustomFilter(const std::string &customFilterDesc);
};

#endif // VIDEO_FILTER_H