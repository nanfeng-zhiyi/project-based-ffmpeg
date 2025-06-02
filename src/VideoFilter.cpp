#include "../include/VideoFilter.h"
#include <iostream>
#include <sstream>
#include <cmath> // 添加数学库，提供M_PI常量

// 引入FFmpeg头文件
extern "C"
{
#include "ffmpeg/include_ffmpeg/libavfilter/avfilter.h"
#include "ffmpeg/include_ffmpeg/libavfilter/buffersink.h"
#include "ffmpeg/include_ffmpeg/libavfilter/buffersrc.h"
#include "ffmpeg/include_ffmpeg/libavutil/opt.h"
#include "ffmpeg/include_ffmpeg/libavutil/pixdesc.h"
#include "ffmpeg/include_ffmpeg/libavutil/imgutils.h"
#include "ffmpeg/include_ffmpeg/libavutil/frame.h"
#include "ffmpeg/include_ffmpeg/libavutil/error.h"
#include "ffmpeg/include_ffmpeg/libavutil/mathematics.h" // 添加数学函数库
}

// 构造函数
VideoFilter::VideoFilter()
    : filterGraph(nullptr),
      bufferSrcContext(nullptr),
      bufferSinkContext(nullptr),
      inputs(nullptr),
      outputs(nullptr),
      width(0),
      height(0),
      pixFmt(0),
      frameRate(0.0),
      filterDesc("null"),
      currentRotation(RotationAngle::ROTATE_0),
      playbackSpeed(1.0),
      frameCallback(nullptr)
{
}

// 析构函数
VideoFilter::~VideoFilter()
{
    closeFilter();
}

// 初始化滤镜
bool VideoFilter::init(int width, int height, int pixFmt, double frameRate, const std::string &filterDesc)
{
    // 参数验证
    if (width <= 0 || height <= 0)
    {
        std::cerr << "视频滤镜: 无效的宽度或高度: " << width << "x" << height << std::endl;
        return false;
    }

    if (pixFmt < 0)
    {
        std::cerr << "视频滤镜: 无效的像素格式: " << pixFmt << std::endl;
        return false;
    }

    if (frameRate <= 0)
    {
        std::cerr << "视频滤镜: 警告 - 无效的帧率: " << frameRate << "，将使用默认值25" << std::endl;
        frameRate = 25.0; // 设置默认帧率
    }

    // 保存参数
    this->width = width;
    this->height = height;
    this->pixFmt = pixFmt;
    this->frameRate = frameRate;
    this->filterDesc = filterDesc;

    // 初始化滤镜
    return initFilter();
}

// 构建滤镜字符串
std::string VideoFilter::buildFilterString()
{
    std::cout << "【调试-重要】视频滤镜: buildFilterString 开始执行 ======================" << std::endl;
    std::cout.flush();

    std::string finalFilterDesc = filterDesc;

    std::cout << "【调试】视频滤镜: 开始构建滤镜字符串，基础滤镜: " << filterDesc << std::endl;
    std::cout.flush();

    // 如果有旋转角度，添加旋转滤镜
    if (currentRotation != RotationAngle::ROTATE_0)
    {
        // 将角度转换为弧度
        double angle = static_cast<int>(currentRotation) * M_PI / 180.0;

        // 如果已有滤镜，添加逗号分隔
        if (finalFilterDesc != "null" && finalFilterDesc != "")
        {
            finalFilterDesc += ",";
        }
        else
        {
            // 如果是null滤镜，替换它
            finalFilterDesc = "";
        }

        // 添加旋转滤镜
        std::ostringstream rotateFilter;
        rotateFilter << "rotate=" << angle;

        // 对于90度和270度旋转，需要调整输出大小
        if (currentRotation == RotationAngle::ROTATE_90 || currentRotation == RotationAngle::ROTATE_270)
        {
            // 交换宽高
            rotateFilter << ":ow=" << height << ":oh=" << width;
        }

        finalFilterDesc += rotateFilter.str();
        std::cout << "【调试】视频滤镜: 添加旋转滤镜，角度: " << static_cast<int>(currentRotation) << "度" << std::endl;
    }

    // 如果播放速度不是1.0，添加倍速播放滤镜
    if (playbackSpeed != 1.0)
    {
        // 如果已有滤镜，添加逗号分隔
        if (finalFilterDesc != "null" && finalFilterDesc != "")
        {
            finalFilterDesc += ",";
        }
        else
        {
            // 如果是null滤镜，替换它
            finalFilterDesc = "";
        }

        std::ostringstream speedFilter;
        std::cout << "playspeeddd" << playbackSpeed << std::endl;

        // 根据不同的倍速范围使用不同的策略
        if (playbackSpeed > 4.0)
        {
            // 改进高倍速播放 (>4.0)处理：不只保留I帧，而是智能地选择关键帧和部分非关键帧
            // 这样可以保持更好的视频连续性，同时大幅减少处理帧数
            speedFilter << "select='if(eq(pict_type,I),1,if(not(mod(n,"
                        << std::max(2, static_cast<int>(playbackSpeed / 2)) << ")),1,0))',";
            speedFilter << "setpts=PTS/TB/" << playbackSpeed << "*TB";

            // 使用更可靠的帧率控制
            double targetFps = std::min(frameRate / 2, 30.0); // 限制最大输出帧率为30fps
            speedFilter << ",fps=" << targetFps;

            // 添加mpdecimate去除视觉上冗余的帧，进一步提高效率
            speedFilter << ",mpdecimate=max=6:hi=64*12:lo=64*3:frac=0.33";

            std::cout << "【调试】视频滤镜: 添加改进的高倍速滤镜 (>" << playbackSpeed
                      << "倍)，智能帧选择，目标帧率: " << targetFps << " fps" << std::endl;
        }
        else if (playbackSpeed > 2.0)
        {
            // 中等倍速播放 (2.0-4.0)：优化帧选择策略
            speedFilter << "select='if(eq(pict_type,I),1,if(not(mod(n,"
                        << std::max(2, static_cast<int>(playbackSpeed / 1.5)) << ")),1,0))',";
            speedFilter << "setpts=PTS/TB/" << playbackSpeed << "*TB";

            // 改进帧率控制
            double targetFps = std::min(frameRate / 1.5, 60.0);
            speedFilter << ",fps=" << targetFps;

            std::cout << "【调试】视频滤镜: 添加优化中倍速滤镜 (" << playbackSpeed
                      << "倍)，改进帧选择，目标帧率: " << targetFps << " fps" << std::endl;
        }
        else if (playbackSpeed > 1.0)
        {
            // 快速播放 (>1.0)：保留所有帧，只调整时间戳
            // 这样可以保持较好的视频质量
            speedFilter << "setpts=PTS/TB/" << playbackSpeed << "*TB";

            std::cout << "【调试】视频滤镜: 添加快速播放滤镜 (" << playbackSpeed
                      << "倍)，保留所有帧，只调整时间戳" << std::endl;
        }
        else
        {
            // 慢速播放 (<1.0)：使用setpts调整时间戳，可能需要插帧以保持流畅
            speedFilter << "setpts=PTS/TB*" << (1.0 / playbackSpeed) << "*TB";

            // 对于非常慢的播放速度，考虑添加帧插值以增加流畅度
            if (playbackSpeed < 0.5)
            {
                // 使用minterpolate滤镜进行帧插值，增加流畅度
                speedFilter << ",minterpolate='mi_mode=mci:mc_mode=aobmc:me_mode=bidir:mb_size=16:vsbmc=1'";
                std::cout << "【调试】视频滤镜: 添加超慢速滤镜 (" << playbackSpeed
                          << "倍)，使用帧插值增加流畅度" << std::endl;
            }
            else
            {
                std::cout << "【调试】视频滤镜: 添加慢速滤镜 (" << playbackSpeed
                          << "倍)，调整时间戳" << std::endl;
            }
        }

        finalFilterDesc += speedFilter.str();
    }

    // 如果最终没有滤镜，使用null滤镜
    if (finalFilterDesc.empty())
    {
        finalFilterDesc = "null";
    }

    std::cout << "【调试】视频滤镜: 最终滤镜字符串: " << finalFilterDesc << std::endl;
    std::cout.flush();
    std::cout << "【调试-重要】视频滤镜: buildFilterString 执行结束，最终滤镜: " << finalFilterDesc << " ======================" << std::endl;
    std::cout.flush();
    return finalFilterDesc;
}

// 内部初始化滤镜方法
bool VideoFilter::initFilter()
{
    std::cout << "【调试-重要】视频滤镜: initFilter 开始执行 ======================" << std::endl;
    std::cout.flush();

    char args[512];
    int ret;
    const AVFilter *bufferSrc = avfilter_get_by_name("buffer");
    const AVFilter *bufferSink = avfilter_get_by_name("buffersink");

    // 检查滤镜是否可用
    if (!bufferSrc || !bufferSink)
    {
        std::cerr << "视频滤镜: 无法找到必要的滤镜" << std::endl;
        return false;
    }

    // 关闭之前的滤镜图表（如果有）
    closeFilter();

    // 创建滤镜图表
    filterGraph = avfilter_graph_alloc();
    if (!filterGraph)
    {
        std::cerr << "视频滤镜: 无法分配滤镜图表" << std::endl;
        return false;
    }

    // 准备输入参数
    // 确保frameRate不为0，如果为0则使用默认值25
    int timeBaseRate = (frameRate > 0) ? static_cast<int>(frameRate) : 25;
    snprintf(args, sizeof(args),
             "video_size=%dx%d:pix_fmt=%d:time_base=1/%d:pixel_aspect=1/1",
             width, height, pixFmt, timeBaseRate);

    // 创建输入缓冲区源滤镜
    ret = avfilter_graph_create_filter(&bufferSrcContext, bufferSrc, "in",
                                       args, nullptr, filterGraph);
    if (ret < 0)
    {
        char errBuff[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_strerror(ret, errBuff, AV_ERROR_MAX_STRING_SIZE);
        std::cerr << "视频滤镜: 无法创建缓冲区源滤镜 (" << errBuff << ")" << std::endl;
        closeFilter();
        return false;
    }

    // 创建输出缓冲区接收滤镜
    ret = avfilter_graph_create_filter(&bufferSinkContext, bufferSink, "out",
                                       nullptr, nullptr, filterGraph);
    if (ret < 0)
    {
        char errBuff[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_strerror(ret, errBuff, AV_ERROR_MAX_STRING_SIZE);
        std::cerr << "视频滤镜: 无法创建缓冲区接收滤镜 (" << errBuff << ")" << std::endl;
        closeFilter();
        return false;
    }

    // 设置输出像素格式
    enum AVPixelFormat pix_fmts[] = {static_cast<AVPixelFormat>(pixFmt), AV_PIX_FMT_NONE};
    ret = av_opt_set_int_list(bufferSinkContext, "pix_fmts", pix_fmts,
                              AV_PIX_FMT_NONE, AV_OPT_SEARCH_CHILDREN);
    if (ret < 0)
    {
        std::cerr << "视频滤镜: 无法设置输出像素格式" << std::endl;
        closeFilter();
        return false;
    }

    // 创建输入输出端点
    outputs = avfilter_inout_alloc();
    inputs = avfilter_inout_alloc();
    if (!outputs || !inputs)
    {
        std::cerr << "视频滤镜: 无法分配滤镜输入输出" << std::endl;
        closeFilter();
        return false;
    }

    // 设置输出端点
    outputs->name = av_strdup("in");
    outputs->filter_ctx = bufferSrcContext;
    outputs->pad_idx = 0;
    outputs->next = nullptr;

    // 设置输入端点
    inputs->name = av_strdup("out");
    inputs->filter_ctx = bufferSinkContext;
    inputs->pad_idx = 0;
    inputs->next = nullptr;

    // 构建最终的滤镜字符串
    std::cout << "【调试-重要】视频滤镜: 即将调用 buildFilterString ======================" << std::endl;
    std::cout.flush();
    std::string finalFilterDesc = buildFilterString();
    std::cout << "【调试-重要】视频滤镜: buildFilterString 调用完成 ======================" << std::endl;
    std::cout.flush();

    // 解析滤镜描述并添加到图表
    ret = avfilter_graph_parse_ptr(filterGraph, finalFilterDesc.c_str(),
                                   &inputs, &outputs, nullptr);
    if (ret < 0)
    {
        char errBuff[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_strerror(ret, errBuff, AV_ERROR_MAX_STRING_SIZE);
        std::cerr << "视频滤镜: 无法解析滤镜描述 '" << finalFilterDesc << "' (" << errBuff << ")" << std::endl;
        closeFilter();
        return false;
    }

    // 配置滤镜图表
    ret = avfilter_graph_config(filterGraph, nullptr);
    if (ret < 0)
    {
        char errBuff[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_strerror(ret, errBuff, AV_ERROR_MAX_STRING_SIZE);
        std::cerr << "视频滤镜: 无法配置滤镜图表 (" << errBuff << ")" << std::endl;
        closeFilter();
        return false;
    }

    std::cout << "视频滤镜: 初始化成功" << std::endl;
    std::cout << "  滤镜描述: " << finalFilterDesc << std::endl;

    return true;
}

// 关闭滤镜
void VideoFilter::closeFilter()
{
    if (filterGraph)
    {
        avfilter_graph_free(&filterGraph);
        filterGraph = nullptr;
    }

    if (inputs)
    {
        avfilter_inout_free(&inputs);
        inputs = nullptr;
    }

    if (outputs)
    {
        avfilter_inout_free(&outputs);
        outputs = nullptr;
    }

    bufferSrcContext = nullptr;
    bufferSinkContext = nullptr;
}

// 处理帧
bool VideoFilter::processFrame(AVFrame *inputFrame, AVFrame *outputFrame)
{
    if (!filterGraph || !bufferSrcContext || !bufferSinkContext || !inputFrame || !outputFrame)
    {
        return false;
    }

    int ret;

    // 将帧发送到滤镜图
    ret = av_buffersrc_add_frame_flags(bufferSrcContext, inputFrame, AV_BUFFERSRC_FLAG_KEEP_REF);
    if (ret < 0)
    {
        std::cerr << "视频滤镜: 无法将帧发送到滤镜图: " << ret << std::endl;
        return false;
    }

    // 从滤镜图获取帧
    ret = av_buffersink_get_frame(bufferSinkContext, outputFrame);
    if (ret < 0)
    {
        if (ret != AVERROR(EAGAIN) && ret != AVERROR_EOF)
        {
            std::cerr << "视频滤镜: 无法从滤镜获取帧: " << ret << std::endl;
        }
        return false;
    }

    // 调试信息：每100帧打印一次时间戳信息
    static int frameCount = 0;
    frameCount++;
    if (frameCount % 100 == 0)
    {
        // AVFrame没有time_base成员，使用固定的时间基或者不计算秒数
        // double inputPtsInSec = inputFrame->pts * av_q2d(inputFrame->time_base);
        // double outputPtsInSec = outputFrame->pts * av_q2d(outputFrame->time_base);

        std::cout << "【调试】视频滤镜: 处理第 " << frameCount << " 帧，输入PTS="
                  << inputFrame->pts
                  << "，输出PTS=" << outputFrame->pts
                  << "，播放速度=" << playbackSpeed << "倍" << std::endl;
    }

    // 如果有回调函数，调用它
    if (frameCallback)
    {
        frameCallback(outputFrame);
    }

    return true;
}

// 设置帧回调
void VideoFilter::setFrameCallback(VideoFilterCallback callback)
{
    frameCallback = callback;
}

// 获取滤镜描述
std::string VideoFilter::getFilterDescription() const
{
    return filterDesc;
}

// 设置旋转角度
bool VideoFilter::setRotation(RotationAngle angle)
{
    // 保存旋转角度
    currentRotation = angle;

    // 重新初始化滤镜
    return initFilter();
}

// 获取当前旋转角度
RotationAngle VideoFilter::getRotation() const
{
    return currentRotation;
}

// 应用自定义滤镜
bool VideoFilter::applyCustomFilter(const std::string &customFilterDesc)
{
    // 保存自定义滤镜描述
    filterDesc = customFilterDesc;

    // 重新初始化滤镜
    return initFilter();
}

// 设置播放速度
bool VideoFilter::setPlaybackSpeed(double speed)
{
    std::cout << "【调试-重要】视频滤镜: setPlaybackSpeed 开始执行，参数 speed = " << speed << " ======================" << std::endl;
    std::cout.flush();

    if (speed <= 0)
    {
        std::cerr << "视频滤镜: 无效的播放速度: " << speed << std::endl;
        std::cerr.flush();
        return false;
    }

    // 保存新的播放速度
    double oldSpeed = playbackSpeed;
    playbackSpeed = speed;

    std::cout << "【调试】视频滤镜: 设置播放速度从 " << oldSpeed << " 变为 " << playbackSpeed << "倍速" << std::endl;
    std::cout.flush();

    // 重新初始化滤镜
    std::cout << "【调试-重要】视频滤镜: 即将调用 initFilter ======================" << std::endl;
    std::cout.flush();
    bool result = initFilter();
    std::cout << "【调试-重要】视频滤镜: initFilter 调用完成，结果 = " << (result ? "成功" : "失败") << " ======================" << std::endl;
    std::cout.flush();
    return result;
}

// 获取当前播放速度
double VideoFilter::getPlaybackSpeed() const
{
    return playbackSpeed;
}
