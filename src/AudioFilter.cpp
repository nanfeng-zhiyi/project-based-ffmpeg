#include "../include/AudioFilter.h"
#include <iostream>
#include <sstream>
#include <cmath>
#include <vector>

// 引入FFmpeg头文件
extern "C"
{
#include "ffmpeg/include_ffmpeg/libavfilter/avfilter.h"
#include "ffmpeg/include_ffmpeg/libavfilter/buffersink.h"
#include "ffmpeg/include_ffmpeg/libavfilter/buffersrc.h"
#include "ffmpeg/include_ffmpeg/libavutil/opt.h"
#include "ffmpeg/include_ffmpeg/libavutil/channel_layout.h"
#include "ffmpeg/include_ffmpeg/libavutil/samplefmt.h"
#include "ffmpeg/include_ffmpeg/libavutil/frame.h"
#include "ffmpeg/include_ffmpeg/libavutil/error.h"
}

// 构造函数
AudioFilter::AudioFilter()
    : filterGraph(nullptr),
      bufferSrcContext(nullptr),
      bufferSinkContext(nullptr),
      inputs(nullptr),
      outputs(nullptr),
      sampleRate(0),
      channels(0),
      channelLayout(0),
      sampleFormat(0),
      filterDesc("anull"),
      playbackSpeed(1.0),
      frameCallback(nullptr)
{
}

// 析构函数
AudioFilter::~AudioFilter()
{
    closeFilter();
}

// 初始化滤镜
bool AudioFilter::init(int sampleRate, int channels, uint64_t channelLayout, int sampleFormat, const std::string &filterDesc)
{
    // 参数验证
    if (sampleRate <= 0)
    {
        std::cerr << "音频滤镜: 无效的采样率: " << sampleRate << std::endl;
        return false;
    }

    if (channels <= 0)
    {
        std::cerr << "音频滤镜: 无效的通道数: " << channels << std::endl;
        return false;
    }

    if (channelLayout == 0)
    {
        std::cerr << "音频滤镜: 警告 - 无效的通道布局，将根据通道数设置默认值" << std::endl;
        // 根据通道数设置默认通道布局
        if (channels == 1)
            channelLayout = AV_CH_LAYOUT_MONO;
        else if (channels == 2)
            channelLayout = AV_CH_LAYOUT_STEREO;
        else
            channelLayout = AV_CH_LAYOUT_STEREO; // 默认使用立体声
    }

    // 保存参数
    this->sampleRate = sampleRate;
    this->channels = channels;
    this->channelLayout = channelLayout;
    this->sampleFormat = sampleFormat;
    this->filterDesc = filterDesc;

    // 初始化滤镜
    return initFilter();
}

// 构建滤镜字符串
std::string AudioFilter::buildFilterString()
{
    std::string finalFilterDesc = filterDesc;

    std::cout << "【调试】音频滤镜: 开始构建滤镜字符串，基础滤镜: " << filterDesc << std::endl;

    // 如果播放速度不是1.0，添加倍速播放滤镜
    if (playbackSpeed != 1.0)
    {
        // 如果已有滤镜，添加逗号分隔
        if (finalFilterDesc != "anull" && finalFilterDesc != "")
        {
            finalFilterDesc += ",";
        }
        else
        {
            // 如果是null滤镜，替换它
            finalFilterDesc = "";
        }

        // 添加音频倍速处理滤镜
        std::ostringstream speedFilter;

        // 处理不同倍速范围的音频
        double speed = playbackSpeed;

        // 首先添加重采样滤镜，确保高质量的音频处理
        speedFilter << "aresample=48000,";

        if (speed > 2.0)
        {
            // 高倍速播放 (>2.0)：使用更优化的atempo级联和改进的音频质量保护
            std::cout << "【调试】音频滤镜: 高倍速处理 (" << speed << "倍)，采用改进的级联策略" << std::endl;

            // 使用更稳定的音频处理流程
            // 首先添加aresample确保高质量的重采样
            speedFilter << "aresample=48000:filter_type=kaiser:kaiser_beta=9,";

            // 将速度分解为更小的阶段，提高音频连续性
            double remainingSpeed = speed;
            std::vector<double> stages;

            // 使用更平滑的分解策略
            if (remainingSpeed > 4.0)
            {
                // 对于超高速，使用更多的小阶段
                while (remainingSpeed > 1.5)
                {
                    // 使用1.5倍速作为基本单位，比2.0更平滑
                    stages.push_back(1.5);
                    remainingSpeed /= 1.5;
                }

                if (remainingSpeed > 1.01)
                {
                    stages.push_back(remainingSpeed);
                }
            }
            else
            {
                // 对于中高速，使用传统的2.0倍速分解
                while (remainingSpeed >= 2.0)
                {
                    stages.push_back(2.0);
                    remainingSpeed /= 2.0;
                }

                if (remainingSpeed > 1.01)
                {
                    stages.push_back(remainingSpeed);
                }
            }

            // 构建改进的atempo级联
            bool isFirst = true;
            for (double stageSpeed : stages)
            {
                if (!isFirst)
                {
                    speedFilter << ",";
                }
                speedFilter << "atempo=" << stageSpeed;
                isFirst = false;

                std::cout << "【调试】音频滤镜: 添加atempo阶段: " << stageSpeed << "倍" << std::endl;
            }

            // 添加apad避免某些音频在处理后出现截断
            speedFilter << ",apad";

            // 高倍速时进行更智能的音频后处理
            if (speed > 3.0)
            {
                // 添加bandpass过滤掉一些可能的噪声频率
                speedFilter << ",bandpass=f=1500:width_type=h:w=2500";
            }
            else
            {
                // 使用适当的低通滤镜
                speedFilter << ",lowpass=f=10000";
            }

            // 添加增强的音量归一化
            speedFilter << ",dynaudnorm=f=120:g=15:p=0.75:m=10";
            std::cout << "【调试】音频滤镜: 添加增强型动态音量归一化" << std::endl;
        }
        else if (speed > 1.0 && speed <= 2.0)
        {
            // 中等倍速播放 (1.0-2.0)：直接使用atempo
            speedFilter << "atempo=" << speed;
            std::cout << "【调试】音频滤镜: 中倍速处理 (" << speed << "倍)，使用单个atempo，音频将变快" << std::endl;

            // 添加轻度音量归一化
            speedFilter << ",dynaudnorm=f=100:g=10:p=0.6";
        }
        else if (speed >= 0.5 && speed < 1.0)
        {
            // 慢速播放 (0.5-1.0)：直接使用atempo
            speedFilter << "atempo=" << speed;
            std::cout << "【调试】音频滤镜: 慢速处理 (" << speed << "倍)，使用单个atempo，音频将变慢" << std::endl;

            // 添加轻度降噪，提高慢速播放的音质
            speedFilter << ",anlmdn=s=0.0001:p=0.01:r=0.01";
        }
        else // speed < 0.5
        {
            // 超慢速播放 (<0.5)：使用atempo级联
            std::cout << "【调试】音频滤镜: 超慢速处理 (" << speed << "倍)" << std::endl;

            // 分解速度
            double remainingSpeed = speed;
            std::vector<double> stages;

            // 首先尝试使用0.5倍速（atempo的最小值）尽可能多次
            while (remainingSpeed <= 0.5)
            {
                stages.push_back(0.5);
                remainingSpeed /= 0.5; // 相当于乘以2
            }

            // 处理剩余的速度（如果有）
            if (remainingSpeed < 0.99)
            { // 允许一点误差
                stages.push_back(remainingSpeed);
            }

            // 构建atempo级联
            bool isFirst = true;
            for (double stageSpeed : stages)
            {
                if (!isFirst)
                {
                    speedFilter << ",";
                }
                speedFilter << "atempo=" << stageSpeed;
                isFirst = false;

                std::cout << "【调试】音频滤镜: 添加atempo阶段: " << stageSpeed << "倍" << std::endl;
            }

            // 添加更强的降噪和音质增强
            speedFilter << ",anlmdn=s=0.0005:p=0.02:r=0.02";
            speedFilter << ",highpass=f=50"; // 去除低频噪音
            std::cout << "【调试】音频滤镜: 添加降噪和高通滤镜增强音质" << std::endl;
        }

        finalFilterDesc += speedFilter.str();
    }

    // 如果最终没有滤镜，使用anull滤镜
    if (finalFilterDesc.empty())
    {
        finalFilterDesc = "anull";
    }

    std::cout << "【调试】音频滤镜: 最终滤镜字符串: " << finalFilterDesc << std::endl;
    return finalFilterDesc;
}

// 初始化滤镜
bool AudioFilter::initFilter()
{
    int ret;
    char args[512];
    const AVFilter *abuffersrc = avfilter_get_by_name("abuffer");
    const AVFilter *abuffersink = avfilter_get_by_name("abuffersink");

    // 检查滤镜是否可用
    if (!abuffersrc || !abuffersink)
    {
        std::cerr << "音频滤镜: 找不到必要的滤镜" << std::endl;
        return false;
    }

    // 关闭之前的滤镜
    closeFilter();

    // 创建滤镜图
    filterGraph = avfilter_graph_alloc();
    if (!filterGraph)
    {
        std::cerr << "音频滤镜: 无法分配滤镜图" << std::endl;
        return false;
    }

    // 获取采样格式名称
    const char *sample_fmt_name = av_get_sample_fmt_name((AVSampleFormat)sampleFormat);
    if (!sample_fmt_name)
    {
        std::cerr << "音频滤镜: 无效的采样格式: " << sampleFormat << std::endl;
        closeFilter();
        return false;
    }

    // 准备输入参数
    snprintf(args, sizeof(args),
             "time_base=1/%d:sample_rate=%d:sample_fmt=%s:channel_layout=0x%" PRIx64,
             sampleRate, sampleRate, sample_fmt_name, channelLayout);

    // 创建输入缓冲源
    ret = avfilter_graph_create_filter(&bufferSrcContext, abuffersrc, "in", args, nullptr, filterGraph);
    if (ret < 0)
    {
        std::cerr << "音频滤镜: 无法创建音频缓冲源" << std::endl;
        closeFilter();
        return false;
    }

    // 创建输出缓冲接收器
    ret = avfilter_graph_create_filter(&bufferSinkContext, abuffersink, "out", nullptr, nullptr, filterGraph);
    if (ret < 0)
    {
        std::cerr << "音频滤镜: 无法创建音频缓冲接收器" << std::endl;
        closeFilter();
        return false;
    }

    // 设置输出采样格式
    ret = av_opt_set_bin(bufferSinkContext, "sample_fmts",
                         (uint8_t *)&sampleFormat, sizeof(sampleFormat),
                         AV_OPT_SEARCH_CHILDREN);
    if (ret < 0)
    {
        std::cerr << "音频滤镜: 无法设置输出采样格式" << std::endl;
        closeFilter();
        return false;
    }

    // 设置输出通道布局
    ret = av_opt_set_bin(bufferSinkContext, "channel_layouts",
                         (uint8_t *)&channelLayout, sizeof(channelLayout),
                         AV_OPT_SEARCH_CHILDREN);
    if (ret < 0)
    {
        std::cerr << "音频滤镜: 无法设置输出通道布局" << std::endl;
        closeFilter();
        return false;
    }

    // 设置输出采样率
    ret = av_opt_set_bin(bufferSinkContext, "sample_rates",
                         (uint8_t *)&sampleRate, sizeof(sampleRate),
                         AV_OPT_SEARCH_CHILDREN);
    if (ret < 0)
    {
        std::cerr << "音频滤镜: 无法设置输出采样率" << std::endl;
        closeFilter();
        return false;
    }

    // 创建输入输出对象
    outputs = avfilter_inout_alloc();
    inputs = avfilter_inout_alloc();
    if (!outputs || !inputs)
    {
        std::cerr << "音频滤镜: 无法分配滤镜输入输出" << std::endl;
        closeFilter();
        return false;
    }

    // 配置输出
    outputs->name = av_strdup("in");
    outputs->filter_ctx = bufferSrcContext;
    outputs->pad_idx = 0;
    outputs->next = nullptr;

    // 配置输入
    inputs->name = av_strdup("out");
    inputs->filter_ctx = bufferSinkContext;
    inputs->pad_idx = 0;
    inputs->next = nullptr;

    // 构建滤镜字符串
    std::string filterString = buildFilterString();

    // 解析滤镜图
    ret = avfilter_graph_parse_ptr(filterGraph, filterString.c_str(), &inputs, &outputs, nullptr);
    if (ret < 0)
    {
        std::cerr << "音频滤镜: 无法解析滤镜图: " << filterString << std::endl;
        closeFilter();
        return false;
    }

    // 配置滤镜图
    ret = avfilter_graph_config(filterGraph, nullptr);
    if (ret < 0)
    {
        std::cerr << "音频滤镜: 无法配置滤镜图" << std::endl;
        closeFilter();
        return false;
    }

    std::cout << "音频滤镜: 成功初始化滤镜" << std::endl;
    return true;
}

// 关闭滤镜
void AudioFilter::closeFilter()
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
bool AudioFilter::processFrame(AVFrame *inputFrame, AVFrame *outputFrame)
{
    if (!filterGraph || !bufferSrcContext || !bufferSinkContext || !inputFrame || !outputFrame)
    {
        return false;
    }

    int ret;

    // 处理可能的帧间隙
    static int64_t lastPts = AV_NOPTS_VALUE;
    if (playbackSpeed != 1.0 && lastPts != AV_NOPTS_VALUE && inputFrame->pts != AV_NOPTS_VALUE)
    {
        // 计算预期的PTS差值
        int64_t expectedPtsDiff = inputFrame->nb_samples;
        int64_t actualPtsDiff = inputFrame->pts - lastPts;

        // 检测是否有大的间隙
        if (actualPtsDiff > expectedPtsDiff * 2)
        {
            std::cout << "【调试】音频滤镜: 检测到音频帧间隙，预期差值="
                      << expectedPtsDiff << "，实际差值=" << actualPtsDiff << std::endl;

            // 在倍速播放时，避免音频不连续
            if (playbackSpeed > 2.0)
            {
                // 对于高倍速，调整PTS以避免大间隙
                inputFrame->pts = lastPts + expectedPtsDiff;
                std::cout << "【调试】音频滤镜: 已调整音频帧PTS以保持连续性" << std::endl;
            }
        }

        // 添加对慢速播放的特殊处理
        if (playbackSpeed < 1.0)
        {
            // 确保慢速播放时音频帧的时间戳正确
            // 注意：atempo滤镜会处理音频内容，但可能不会正确调整时间戳
            // 这里我们记录下来，以便在调试时观察

            // 不要在这里修改PTS，因为atempo滤镜会处理音频内容和时间戳
            // 但我们可以在调试模式下记录预期的输出PTS
            if (inputFrame->pts % 1000 == 0)
            {
                std::cout << "【调试】音频滤镜: 慢速播放(" << playbackSpeed << "倍)，音频帧PTS="
                          << inputFrame->pts << "，预期输出PTS约为="
                          << static_cast<int64_t>(inputFrame->pts * (1.0 / playbackSpeed)) << std::endl;
            }
        }
    }
    lastPts = inputFrame->pts;

    // 将帧发送到滤镜图
    ret = av_buffersrc_add_frame_flags(bufferSrcContext, inputFrame, AV_BUFFERSRC_FLAG_KEEP_REF);
    if (ret < 0)
    {
        std::cerr << "音频滤镜: 无法将帧发送到滤镜图: " << ret << std::endl;
        return false;
    }

    // 从滤镜图获取帧
    ret = av_buffersink_get_frame(bufferSinkContext, outputFrame);
    if (ret < 0)
    {
        if (ret != AVERROR(EAGAIN) && ret != AVERROR_EOF)
        {
            std::cerr << "音频滤镜: 无法从滤镜获取帧: " << ret << std::endl;
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

        std::cout << "【调试】音频滤镜: 处理第 " << frameCount << " 帧，输入PTS="
                  << inputFrame->pts
                  << "，输出PTS=" << outputFrame->pts
                  << "，播放速度=" << playbackSpeed << "倍" << std::endl;
    }

    // 确保在慢速播放时输出帧的时间戳正确
    // 注意：atempo滤镜会改变音频内容，但可能不会正确调整时间戳
    // 这里我们不直接修改时间戳，因为这会在Muxer中处理
    // 但我们可以在调试模式下检查时间戳是否合理
    if (playbackSpeed < 1.0 && outputFrame->pts != AV_NOPTS_VALUE && inputFrame->pts != AV_NOPTS_VALUE)
    {
        // 计算预期的输出PTS（考虑到播放速度）
        // 对于慢速播放，预期比率应该是1.0/playbackSpeed（而不是playbackSpeed）
        double expectedRatio = 1.0 / playbackSpeed;
        double actualRatio = 0.0;

        if (inputFrame->pts > 0)
        {
            actualRatio = static_cast<double>(outputFrame->pts) / inputFrame->pts;
        }

        // 如果比率差异太大，记录警告
        if (std::abs(actualRatio - expectedRatio) > 0.5 && frameCount % 100 == 0)
        {
            std::cout << "【警告】音频滤镜: 慢速播放时间戳比率异常，预期="
                      << expectedRatio << "，实际=" << actualRatio
                      << "，这可能导致音频播放速度不正确" << std::endl;
        }
    }

    // 如果有回调函数，调用它
    if (frameCallback)
    {
        frameCallback(outputFrame);
    }

    return true;
}

// 设置帧回调
void AudioFilter::setFrameCallback(AudioFilterCallback callback)
{
    frameCallback = callback;
}

// 获取滤镜描述
std::string AudioFilter::getFilterDescription() const
{
    return filterDesc;
}

// 设置播放速度
bool AudioFilter::setPlaybackSpeed(double speed)
{
    if (speed <= 0)
    {
        std::cerr << "音频滤镜: 无效的播放速度: " << speed << std::endl;
        return false;
    }

    // 保存新的播放速度
    double oldSpeed = playbackSpeed;
    playbackSpeed = speed;

    std::cout << "【调试】音频滤镜: 设置播放速度从 " << oldSpeed << " 变为 " << playbackSpeed << "倍速" << std::endl;

    // 重新初始化滤镜
    return initFilter();
}

// 获取当前播放速度
double AudioFilter::getPlaybackSpeed() const
{
    return playbackSpeed;
}

// 应用自定义滤镜
bool AudioFilter::applyCustomFilter(const std::string &customFilterDesc)
{
    if (customFilterDesc.empty())
    {
        std::cerr << "音频滤镜: 无效的滤镜描述" << std::endl;
        return false;
    }

    // 保存新的滤镜描述
    filterDesc = customFilterDesc;

    // 重新初始化滤镜
    return initFilter();
}