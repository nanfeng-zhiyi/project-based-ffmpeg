#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <atomic>
#include <csignal>
#include <cmath>
#include <iomanip>   // 添加这个头文件，用于std::setprecision
#include <vector>    // 添加这个头文件，用于std::vector
#include <fstream>   // 添加这个头文件，用于文件操作
#include <algorithm> // 添加这个头文件，用于std::transform

// 引入FFmpeg头文件
extern "C"
{
#include "ffmpeg/include_ffmpeg/libavformat/avformat.h"
#include "ffmpeg/include_ffmpeg/libavcodec/avcodec.h"
#include "ffmpeg/include_ffmpeg/libavutil/avutil.h"
#include "ffmpeg/include_ffmpeg/libavutil/pixfmt.h"
#include "ffmpeg/include_ffmpeg/libavutil/imgutils.h"
#include "ffmpeg/include_ffmpeg/libavutil/time.h"
#include "ffmpeg/include_ffmpeg/libavutil/channel_layout.h"
}

// 引入自定义头文件
#include "include/Demux.h"
#include "include/VideoDecoder.h"
#include "include/AudioDecoder.h"
#include "include/VideoFilter.h"
#include "include/AudioFilter.h"
#include "include/VideoEncoder.h"
#include "include/AudioEncoder.h"
#include "include/Muxer.h"
#include "include/queue.h"

// 全局变量
std::atomic<bool> g_running(true);
bool g_debugMode = false;
int g_videoFrameCount = 0;
int g_audioFrameCount = 0;
int g_totalFrames = 0;
std::chrono::steady_clock::time_point g_startTime;

// 信号处理函数
void signalHandler(int signum)
{
    std::cout << "\n接收到中断信号 (" << signum << ")，准备退出..." << std::endl;
    g_running = false;
}

// 视频帧回调函数
void handleVideoFrame(AVFrame *frame)
{
    g_videoFrameCount++;

    // 每10帧打印一次信息
    if (g_videoFrameCount % 10 == 0 || g_debugMode)
    {
        double elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now() - g_startTime)
                             .count() /
                         1000.0;

        double fps = (elapsed > 0) ? g_videoFrameCount / elapsed : 0;
        double progress = (g_totalFrames > 0) ? (g_videoFrameCount * 100.0 / g_totalFrames) : 0;

        std::cout << "\r解码进度: " << g_videoFrameCount;
        if (g_totalFrames > 0)
        {
            std::cout << "/" << g_totalFrames << " (" << std::fixed << std::setprecision(1) << progress << "%)";
        }
        std::cout << " 帧, 耗时: " << std::fixed << std::setprecision(1) << elapsed << "s";
        std::cout << ", 速度: " << std::fixed << std::setprecision(1) << fps << " fps";
        std::cout << "    " << std::flush;
    }
}

// 视频滤镜回调函数
void handleFilteredVideoFrame(AVFrame *frame)
{
    // 在调试模式下打印滤镜处理后的帧信息
    if (g_debugMode && g_videoFrameCount % 10 == 0)
    {
        std::cout << "视频滤镜处理帧 #" << g_videoFrameCount
                  << ", 分辨率: " << frame->width << "x" << frame->height
                  << ", 格式: " << frame->format << std::endl;
    }
}

// 音频滤镜回调函数
void handleFilteredAudioFrame(AVFrame *frame)
{
    // 在调试模式下打印滤镜处理后的帧信息
    if (g_debugMode && g_audioFrameCount % 10 == 0)
    {
        std::cout << "音频滤镜处理帧 #" << g_audioFrameCount
                  << ", 采样数: " << frame->nb_samples
                  << ", 通道数: " << frame->channels
                  << ", 格式: " << frame->format << std::endl;
    }
}

// 视频编码回调函数
void handleEncodedVideoPacket(AVPacket *packet)
{
    // 在调试模式下打印编码后的包信息
    if (g_debugMode && packet->size > 0 && packet->size % 100 == 0)
    {
        std::cout << "视频编码包: 大小=" << packet->size
                  << ", pts=" << packet->pts
                  << ", dts=" << packet->dts << std::endl;
    }
}

// 音频编码回调函数
void handleEncodedAudioPacket(AVPacket *packet)
{
    // 在调试模式下打印编码后的包信息
    if (g_debugMode && packet->size > 0 && packet->size % 100 == 0)
    {
        std::cout << "音频编码包: 大小=" << packet->size
                  << ", pts=" << packet->pts
                  << ", dts=" << packet->dts << std::endl;
    }
}

// 音频帧回调函数
void handleAudioFrame(const uint8_t *data, int size, int sampleRate, int channels)
{
    g_audioFrameCount++;

    // 在调试模式下打印音频帧信息
    if (g_debugMode && g_audioFrameCount % 100 == 0)
    {
        std::cout << "音频帧 #" << g_audioFrameCount
                  << ", 大小: " << size << " 字节"
                  << ", 采样率: " << sampleRate
                  << ", 通道数: " << channels << std::endl;
    }

    // 如果我们需要将音频帧传递给音频滤镜和编码器，可以在这里处理
    // 但由于我们的AudioDecoder直接输出PCM数据而不是AVFrame，
    // 我们需要在这里创建一个AVFrame并填充数据
    if (size > 0 && data != nullptr)
    {
        // 这里可以添加将PCM数据转换为AVFrame的代码
        // 然后将AVFrame传递给音频滤镜和编码器
    }
}

// 打印使用方法
void printUsage(const char *programName)
{
    std::cout << "使用方法: " << programName << " <输入文件> [选项]" << std::endl;
    std::cout << "选项:" << std::endl;
    std::cout << "  -v <文件路径>       将解码后的视频保存为YUV文件" << std::endl;
    std::cout << "  -a <文件路径>       将解码后的音频保存为PCM文件" << std::endl;
    std::cout << "  -o <文件路径>       指定输出文件路径" << std::endl;
    std::cout << "  -r <角度>           旋转视频 (可选值: 0, 90, 180, 270)" << std::endl;
    std::cout << "  -f <滤镜描述>       应用自定义视频滤镜" << std::endl;
    std::cout << "  -af <滤镜描述>      应用自定义音频滤镜" << std::endl;
    std::cout << "  -s <速度>           设置播放速度 (例如: 0.5=半速, 1.0=正常, 2.0=两倍速)" << std::endl;
    std::cout << "  -d, --debug         启用调试模式" << std::endl;
    std::cout << "  --direct-video      使用直接YUV输出模式" << std::endl;
    std::cout << "  --direct-audio      使用直接PCM输出模式" << std::endl;
    std::cout << "  -h, --help          显示此帮助信息" << std::endl;
    std::cout << std::endl;
    std::cout << "示例:" << std::endl;
    std::cout << "  " << programName << " input.mp4" << std::endl;
    std::cout << "  " << programName << " input.mp4 -v output.yuv -a output.pcm" << std::endl;
    std::cout << "  " << programName << " input.mp4 -o output.mp4 -r 90" << std::endl;
    std::cout << "  " << programName << " input.mp4 -f \"eq=brightness=0.1:contrast=1.2\"" << std::endl;
    std::cout << "  " << programName << " input.mp4 -af \"volume=2.0\"" << std::endl;
    std::cout << "  " << programName << " input.mp4 -s 2.0" << std::endl;
}

int main(int argc, char *argv[])
{
    std::cout << "【调试-重要】程序开始执行 ======================" << std::endl;
    std::cout.flush();

    // 注册信号处理函数
    signal(SIGINT, signalHandler);

    // 检查命令行参数
    if (argc < 2)
    {
        printUsage(argv[0]);
        return 1;
    }

    // 解析命令行参数
    std::string inputFile;
    std::string videoOutputFile;
    std::string audioOutputFile;
    std::string outputFile = "output.mp4";
    std::string customVideoFilter;
    std::string customAudioFilter;
    int rotationAngle = 0;
    double playbackSpeed = 1.0; // 默认播放速度为1.0（正常速度）
    bool useDirectVideo = false;
    bool useDirectAudio = false;

    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0)
        {
            printUsage(argv[0]);
            return 0;
        }
        else if (strcmp(argv[i], "-v") == 0 && i + 1 < argc)
        {
            videoOutputFile = argv[++i];
        }
        else if (strcmp(argv[i], "-a") == 0 && i + 1 < argc)
        {
            audioOutputFile = argv[++i];
        }
        else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc)
        {
            outputFile = argv[++i];
        }
        else if (strcmp(argv[i], "-r") == 0 && i + 1 < argc)
        {
            rotationAngle = std::stoi(argv[++i]);
            // 确保角度是有效的
            if (rotationAngle != 0 && rotationAngle != 90 && rotationAngle != 180 && rotationAngle != 270)
            {
                std::cerr << "错误: 旋转角度必须是 0, 90, 180 或 270" << std::endl;
                return 1;
            }
        }
        else if (strcmp(argv[i], "-f") == 0 && i + 1 < argc)
        {
            customVideoFilter = argv[++i];
        }
        else if (strcmp(argv[i], "-af") == 0 && i + 1 < argc)
        {
            customAudioFilter = argv[++i];
        }
        else if (strcmp(argv[i], "-s") == 0 && i + 1 < argc)
        {
            playbackSpeed = std::stod(argv[++i]);
            if (playbackSpeed <= 0)
            {
                std::cerr << "错误: 播放速度必须大于0" << std::endl;
                return 1;
            }
            std::cout << "【调试】设置播放速度为: " << playbackSpeed << "倍速" << std::endl;
        }
        else if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--debug") == 0)
        {
            g_debugMode = true;
        }
        else if (strcmp(argv[i], "--direct-video") == 0)
        {
            useDirectVideo = true;
        }
        else if (strcmp(argv[i], "--direct-audio") == 0)
        {
            useDirectAudio = true;
        }
        else if (inputFile.empty())
        {
            inputFile = argv[i];
        }
        else
        {
            std::cerr << "未知参数: " << argv[i] << std::endl;
            printUsage(argv[0]);
            return 1;
        }
    }

    if (inputFile.empty())
    {
        std::cerr << "错误: 未指定输入文件" << std::endl;
        printUsage(argv[0]);
        return 1;
    }

    std::cout << "输入文件: " << inputFile << std::endl;
    if (!videoOutputFile.empty())
    {
        std::cout << "视频输出文件: " << videoOutputFile << std::endl;
    }
    if (!audioOutputFile.empty())
    {
        std::cout << "音频输出文件: " << audioOutputFile << std::endl;
    }
    if (!outputFile.empty())
    {
        std::cout << "转码输出文件: " << outputFile << std::endl;
    }

    // 创建队列
    VideoPacketQueue videoQueue;
    AudioPacketQueue audioQueue;
    VideoFrameQueue videoFrameQueue;
    AudioFrameQueue audioFrameQueue;
    VideoPacketQueue encodedVideoQueue;
    AudioPacketQueue encodedAudioQueue;

    // 创建解复用器
    Demux demux(inputFile, videoQueue, audioQueue);

    // 初始化解复用器
    if (!demux.init())
    {
        std::cerr << "初始化解复用器失败，无法打开输入文件: " << inputFile << std::endl;
        return 1;
    }

    // 获取媒体信息
    MediaInfo mediaInfo = demux.getMediaInfo();
    if (mediaInfo.duration > 0)
    {
        std::cout << "媒体时长: " << mediaInfo.duration << " 秒" << std::endl;
    }
    if (mediaInfo.videoStreamIndex >= 0)
    {
        std::cout << "视频流: " << mediaInfo.width << "x" << mediaInfo.height << ", " << mediaInfo.fps << " fps" << std::endl;
        g_totalFrames = static_cast<int>(mediaInfo.fps * mediaInfo.duration);
    }
    if (mediaInfo.audioStreamIndex >= 0)
    {
        std::cout << "音频流: " << mediaInfo.sampleRate << " Hz, " << mediaInfo.channels << " 通道" << std::endl;
    }

    // 记录开始时间
    g_startTime = std::chrono::steady_clock::now();

    // 创建视频解码器
    VideoDecoder videoDecoder(videoQueue, videoFrameQueue);

    // 如果有视频流，初始化视频解码器
    bool hasVideo = false;
    if (mediaInfo.videoStreamIndex >= 0)
    {
        if (videoDecoder.init(mediaInfo.videoCodecPar))
        {
            hasVideo = true;
            videoDecoder.setFrameCallback(handleVideoFrame);

            // 设置YUV输出
            if (!videoOutputFile.empty())
            {
                if (useDirectVideo)
                {
                    if (!videoDecoder.setDirectYUVOutput(videoOutputFile))
                    {
                        std::cerr << "设置直接YUV输出失败" << std::endl;
                    }
                }
                else
                {
                    if (!videoDecoder.setYUVOutput(videoOutputFile))
                    {
                        std::cerr << "设置YUV输出失败" << std::endl;
                    }
                }
            }

            std::cout << "视频解码器: " << videoDecoder.getCodecName() << std::endl;
        }
        else
        {
            std::cerr << "初始化视频解码器失败" << std::endl;
        }
    }

    // 创建视频滤镜
    VideoFilter *videoFilter = nullptr;
    if (hasVideo)
    {
        videoFilter = new VideoFilter();
        if (!videoFilter->init(mediaInfo.width, mediaInfo.height, AV_PIX_FMT_YUV420P, mediaInfo.fps, "null"))
        {
            std::cerr << "初始化视频滤镜失败" << std::endl;
            delete videoFilter;
            videoFilter = nullptr;
        }
        else
        {
            // 设置旋转角度
            if (rotationAngle != 0)
            {
                RotationAngle angle;
                switch (rotationAngle)
                {
                case 90:
                    angle = RotationAngle::ROTATE_90;
                    break;
                case 180:
                    angle = RotationAngle::ROTATE_180;
                    break;
                case 270:
                    angle = RotationAngle::ROTATE_270;
                    break;
                default:
                    angle = RotationAngle::ROTATE_0;
                    break;
                }
                videoFilter->setRotation(angle);
            }

            // 设置播放速度
            if (playbackSpeed != 1.0)
            {
                if (videoFilter->setPlaybackSpeed(playbackSpeed))
                {
                    std::cout << "【调试】视频滤镜: 已设置播放速度为 " << playbackSpeed << "倍速" << std::endl;
                }
                else
                {
                    std::cerr << "【调试】视频滤镜: 设置播放速度失败" << std::endl;
                }
            }

            // 应用自定义滤镜
            if (!customVideoFilter.empty())
            {
                videoFilter->applyCustomFilter(customVideoFilter);
            }

            // 设置滤镜回调
            videoFilter->setFrameCallback(handleFilteredVideoFrame);
        }
    }

    // 创建视频编码器
    VideoEncoder videoEncoder(videoFrameQueue, encodedVideoQueue);
    bool hasEncoder = false;

    // 如果有视频滤镜，初始化视频编码器
    if (hasVideo && videoFilter)
    {
        // 尝试不同的编码器
        std::vector<std::string> encoders = {"libx264", "h264_nvenc", "h264_qsv", "h264_vaapi", "mpeg4"};
        bool encoderInitialized = false;

        for (const auto &encoder : encoders)
        {
            if (videoEncoder.init(mediaInfo.width, mediaInfo.height, mediaInfo.fps, 2000000, encoder))
            {
                encoderInitialized = true;
                hasEncoder = true;

                // 设置视频滤镜
                if (videoFilter)
                {
                    if (videoEncoder.setVideoFilter(videoFilter))
                    {
                        std::cout << "视频编码器: 已设置视频滤镜" << std::endl;
                    }
                    else
                    {
                        std::cerr << "视频编码器: 设置视频滤镜失败" << std::endl;
                    }
                }

                videoEncoder.setEncodeCallback(handleEncodedVideoPacket);
                std::cout << "视频编码器: 已初始化，编码器: " << videoEncoder.getCodecName() << std::endl;
                break;
            }
            else
            {
                std::cerr << "使用 " << encoder << " 初始化视频编码器失败，尝试下一个编码器" << std::endl;
            }
        }

        if (!encoderInitialized)
        {
            std::cerr << "所有编码器初始化都失败，无法进行视频转码" << std::endl;
            delete videoFilter;
            videoFilter = nullptr;
        }
    }

    // 创建音频解码器
    AudioDecoder audioDecoder(audioQueue, audioFrameQueue);

    // 如果有音频流，初始化音频解码器
    bool hasAudio = false;
    if (mediaInfo.audioStreamIndex >= 0)
    {
        if (audioDecoder.init(mediaInfo.audioCodecPar))
        {
            hasAudio = true;

            // 仍然保留回调函数用于显示进度，但主要数据流通过队列
            audioDecoder.setFrameCallback(handleAudioFrame);

            // 设置PCM输出
            if (!audioOutputFile.empty())
            {
                if (useDirectAudio)
                {
                    if (!audioDecoder.setDirectPCMOutput(audioOutputFile))
                    {
                        std::cerr << "设置直接PCM输出失败" << std::endl;
                    }
                }
                else
                {
                    if (!audioDecoder.setPCMOutput(audioOutputFile))
                    {
                        std::cerr << "设置PCM输出失败" << std::endl;
                    }
                }
            }

            std::cout << "音频解码器: " << audioDecoder.getCodecName() << std::endl;
        }
        else
        {
            std::cerr << "初始化音频解码器失败" << std::endl;
        }
    }

    // 创建音频滤镜
    AudioFilter *audioFilter = nullptr;
    if (hasAudio)
    {
        audioFilter = new AudioFilter();
        if (!audioFilter->init(mediaInfo.sampleRate, mediaInfo.channels,
                               av_get_default_channel_layout(mediaInfo.channels),
                               AV_SAMPLE_FMT_FLTP,
                               customAudioFilter.empty() ? "anull" : customAudioFilter))
        {
            std::cerr << "初始化音频滤镜失败" << std::endl;
            delete audioFilter;
            audioFilter = nullptr;
        }
        else
        {
            // 设置播放速度
            if (playbackSpeed != 1.0)
            {
                if (audioFilter->setPlaybackSpeed(playbackSpeed))
                {
                    std::cout << "【调试】音频滤镜: 已设置播放速度为 " << playbackSpeed << "倍速" << std::endl;
                }
                else
                {
                    std::cerr << "【调试】音频滤镜: 设置播放速度失败" << std::endl;
                }
            }

            // 设置滤镜回调
            audioFilter->setFrameCallback(handleFilteredAudioFrame);
            std::cout << "音频滤镜: 已初始化，滤镜: " << audioFilter->getFilterDescription() << std::endl;
        }
    }

    // 创建音频编码器
    AudioEncoder audioEncoder(audioFrameQueue, encodedAudioQueue);
    bool hasAudioEncoder = false;

    // 如果有音频，初始化音频编码器
    if (hasAudio)
    {
        // 初始化音频编码器，使用AC3格式
        if (audioEncoder.init(audioDecoder.getSampleRate(),
                              audioDecoder.getChannels(),
                              av_get_default_channel_layout(audioDecoder.getChannels()),
                              192000, // 比特率
                              "ac3")) // 编码器名称
        {
            hasAudioEncoder = true;

            // 设置音频滤镜
            if (audioFilter)
            {
                if (audioEncoder.setAudioFilter(audioFilter))
                {
                    std::cout << "音频编码器: 已设置音频滤镜" << std::endl;
                }
                else
                {
                    std::cerr << "音频编码器: 设置音频滤镜失败" << std::endl;
                }
            }

            audioEncoder.setEncodeCallback(handleEncodedAudioPacket);
            std::cout << "音频编码器: 已初始化，编码器: " << audioEncoder.getCodecName() << std::endl;
        }
        else
        {
            std::cerr << "初始化音频编码器失败，无法进行音频转码" << std::endl;
            if (audioFilter)
            {
                delete audioFilter;
                audioFilter = nullptr;
            }
        }
    }

    // 如果没有视频也没有音频，退出
    if (!hasVideo && !hasAudio)
    {
        std::cerr << "没有可解码的媒体流" << std::endl;
        return 1;
    }

    // 创建复用器
    Muxer muxer(encodedVideoQueue, encodedAudioQueue);
    bool hasMuxer = false;
    if ((hasEncoder || hasAudioEncoder) && !outputFile.empty())
    {
        AVCodecContext *videoCodecCtx = hasEncoder ? videoEncoder.getCodecContext() : nullptr;
        AVCodecContext *audioCodecCtx = hasAudioEncoder ? audioEncoder.getCodecContext() : nullptr;

        // 保持原始输出文件名
        std::string originalOutputFile = outputFile;
        std::cout << "【调试】使用MP4输出格式，输出文件: " << outputFile << std::endl;

        // 如果是AC3音频编码器，打印警告信息
        if (hasAudioEncoder && audioCodecCtx && audioCodecCtx->codec_id == AV_CODEC_ID_AC3)
        {
            std::cout << "【调试】警告: 使用AC3音频编码器与MP4容器，可能存在兼容性问题" << std::endl;
            std::cout << "【调试】将尝试特殊处理以提高兼容性" << std::endl;
        }

        // 尝试多次初始化复用器，以提高成功率
        bool muxerInitialized = false;
        for (int attempt = 1; attempt <= 3 && !muxerInitialized; attempt++)
        {
            std::cout << "【调试】复用器: 尝试初始化 (第" << attempt << "次), 输出文件: " << outputFile << std::endl;
            if (muxer.init(outputFile, videoCodecCtx, audioCodecCtx))
            {
                muxerInitialized = true;
                hasMuxer = true;
                std::cout << "【调试】复用器: 已成功初始化，输出文件: " << outputFile << std::endl;

                // 设置播放速度
                if (playbackSpeed != 1.0)
                {
                    muxer.setPlaybackSpeed(playbackSpeed);
                    std::cout << "【调试】复用器: 已设置播放速度为 " << playbackSpeed << "倍速" << std::endl;
                }

                break;
            }
            else
            {
                std::cerr << "【调试】复用器: 初始化失败 (第" << attempt << "次)" << std::endl;
                if (attempt < 3)
                {
                    std::cout << "【调试】复用器: 等待1秒后重试..." << std::endl;
                    std::this_thread::sleep_for(std::chrono::seconds(1));
                }
            }
        }

        if (!muxerInitialized)
        {
            std::cerr << "【调试】复用器: 多次尝试初始化失败，无法创建输出文件" << std::endl;
        }
    }

    // 开始解复用-解码模块
    // 启动解复用
    demux.start();

    // 启动解码器
    if (hasVideo)
    {
        videoDecoder.start();
    }

    if (hasAudio)
    {
        audioDecoder.start();
    }

    // 启动编码器
    if (hasEncoder)
    {
        videoEncoder.start();
    }

    if (hasAudioEncoder)
    {
        audioEncoder.start();
    }

    // 启动复用器
    if (hasMuxer)
    {
        std::cout << "【调试】启动复用器..." << std::endl;
        muxer.start();

        // 验证复用器是否成功启动
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        if (!muxer.isActive())
        {
            std::cerr << "【警告】复用器启动失败，可能无法正确写入输出文件" << std::endl;
        }
        else
        {
            std::cout << "【调试】复用器已成功启动" << std::endl;
        }
    }

    // 等待处理完成
    std::cout << "开始处理媒体文件..." << std::endl;

    // 等待解复用完成
    while (g_running && !demux.isFinished())
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    std::cout << "\n解复用完成" << std::endl;

    // 等待解码器队列清空
    while (g_running &&
           ((hasVideo && (!videoDecoder.isQueueEmpty() || !videoFrameQueue.isEmpty())) ||
            (hasAudio && (!audioDecoder.isQueueEmpty() || !audioFrameQueue.isEmpty()))))
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    std::cout << "解码完成" << std::endl;

    // 停止解码器
    if (hasVideo)
    {
        videoDecoder.stop();
    }

    if (hasAudio)
    {
        audioDecoder.stop();
    }

    // 等待编码器队列清空
    if (hasEncoder || hasAudioEncoder)
    {
        std::cout << "等待编码完成..." << std::endl;

        // 等待编码器队列清空
        while (g_running &&
               ((hasEncoder && !encodedVideoQueue.isEmpty()) ||
                (hasAudioEncoder && !encodedAudioQueue.isEmpty())))
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        std::this_thread::sleep_for(std::chrono::seconds(1)); // 给编码器一些额外时间处理剩余帧
    }

    // 停止编码器
    if (hasEncoder)
    {
        videoEncoder.flush();
        videoEncoder.stop();
    }

    if (hasAudioEncoder)
    {
        audioEncoder.flush();
        audioEncoder.stop();
    }

    // 清理资源
    if (videoFilter)
    {
        delete videoFilter;
        videoFilter = nullptr;
    }

    if (audioFilter)
    {
        delete audioFilter;
        audioFilter = nullptr;
    }

    // 打印处理结果
    double totalTime = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::steady_clock::now() - g_startTime)
                           .count() /
                       1000.0;

    std::cout << "\n处理完成，总耗时: " << std::fixed << std::setprecision(2) << totalTime << " 秒" << std::endl;

    if (hasVideo)
    {
        std::cout << "处理视频帧: " << g_videoFrameCount << " 帧" << std::endl;
    }

    if (hasAudio)
    {
        std::cout << "处理音频帧: " << g_audioFrameCount << " 帧" << std::endl;
    }

    // 等待复用器完成
    if (hasMuxer)
    {
        std::cout << "【调试】等待复用完成..." << std::endl;

        // 等待一段时间，确保所有数据包都被处理
        std::this_thread::sleep_for(std::chrono::seconds(2));

        // 检查队列是否为空
        int waitCount = 0;
        while ((!encodedVideoQueue.isEmpty() || !encodedAudioQueue.isEmpty()) && waitCount < 10)
        {
            std::cout << "【调试】警告: 编码队列仍有数据包未处理，等待额外时间... ("
                      << (waitCount + 1) << "/10)" << std::endl;
            std::this_thread::sleep_for(std::chrono::seconds(1));
            waitCount++;
        }

        // 检查复用器是否仍在运行
        if (muxer.isActive())
        {
            std::cout << "【调试】复用器仍在运行，等待额外时间..." << std::endl;
            std::this_thread::sleep_for(std::chrono::seconds(3));
        }

        // 停止复用器
        std::cout << "【调试】正在停止复用器..." << std::endl;
        muxer.stop();

        // 验证输出文件
        std::cout << "【调试】验证输出文件: " << outputFile << std::endl;
        std::ifstream outputFileCheck(outputFile, std::ios::binary);
        if (outputFileCheck.good())
        {
            // 获取文件大小
            outputFileCheck.seekg(0, std::ios::end);
            std::streamsize fileSize = outputFileCheck.tellg();
            outputFileCheck.close();

            if (fileSize > 0)
            {
                std::cout << "【调试】复用完成: 输出文件 '" << outputFile << "' 已成功创建，大小: "
                          << fileSize / 1024 / 1024 << " MB" << std::endl;

                // 打印成功信息
                std::cout << "\n转码完成！" << std::endl;
                std::cout << "输出文件: " << outputFile << std::endl;
                std::cout << "文件大小: " << fileSize / 1024 / 1024 << " MB" << std::endl;
                std::cout << "视频帧数: " << g_videoFrameCount << std::endl;
                std::cout << "音频帧数: " << g_audioFrameCount << std::endl;

                // 如果使用了倍速播放，打印相关信息
                if (playbackSpeed != 1.0)
                {
                    std::cout << "播放速度: " << playbackSpeed << "倍速" << std::endl;
                }
            }
            else
            {
                std::cerr << "【调试】警告: 输出文件 '" << outputFile << "' 大小为0" << std::endl;
                std::cerr << "【调试】可能原因: 复用器没有正确处理数据包或文件没有正确关闭" << std::endl;

                // 尝试使用ffmpeg修复文件
                std::cout << "【调试】尝试使用ffmpeg修复文件..." << std::endl;
                std::string repairCmd = "ffmpeg -i " + outputFile + " -c copy " + outputFile + ".fixed.mp4";
                std::cout << "【调试】修复命令: " << repairCmd << std::endl;
                std::cout << "【调试】您可以手动运行上述命令尝试修复文件" << std::endl;
            }
        }
        else
        {
            std::cerr << "【调试】错误: 无法打开输出文件 '" << outputFile << "' 进行验证" << std::endl;
        }
    }

    // 确保所有资源都被释放
    std::cout << "【调试】清理资源..." << std::endl;

    // 等待所有线程结束
    std::this_thread::sleep_for(std::chrono::seconds(1));

    std::cout << "【调试】程序正常退出" << std::endl;
    return 0;
}
