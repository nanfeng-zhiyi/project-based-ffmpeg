#include "../include/Demux.h"
#include <iostream>

// 引入FFmpeg头文件
extern "C"
{
#include "../ffmpeg/include_ffmpeg/libavformat/avformat.h"
#include "../ffmpeg/include_ffmpeg/libavcodec/avcodec.h"
#include "../ffmpeg/include_ffmpeg/libavutil/time.h"
#include "../ffmpeg/include_ffmpeg/libavutil/imgutils.h"
}

// 构造函数
Demux::Demux(const std::string &inputFile, VideoPacketQueue &videoQueue, AudioPacketQueue &audioQueue)
    : inputFile(inputFile),
      formatContext(nullptr),
      videoQueue(videoQueue),
      audioQueue(audioQueue),
      isRunning(false),
      isPaused(false),
      isEOF(false)
{
}

// 析构函数
Demux::~Demux()
{
    stop();
    closeInputFile();
}

// 初始化解复用器
bool Demux::init()
{
    // 打开输入文件
    if (!openInputFile())
    {
        std::cerr << "无法打开输入文件: " << inputFile << std::endl;
        return false;
    }

    return true;
}

// 打开输入文件
bool Demux::openInputFile()
{
    // 分配AVFormatContext
    formatContext = avformat_alloc_context();
    if (!formatContext)
    {
        std::cerr << "无法分配AVFormatContext" << std::endl;
        return false;
    }

    // 打开输入文件
    if (avformat_open_input(&formatContext, inputFile.c_str(), nullptr, nullptr) != 0)
    {
        std::cerr << "无法打开输入文件" << std::endl;
        return false;
    }

    // 读取流信息
    if (avformat_find_stream_info(formatContext, nullptr) < 0)
    {
        std::cerr << "无法找到流信息" << std::endl;
        return false;
    }

    // 查找视频流和音频流
    mediaInfo.videoStreamIndex = -1;
    mediaInfo.audioStreamIndex = -1;

    for (unsigned int i = 0; i < formatContext->nb_streams; i++)
    {
        AVStream *stream = formatContext->streams[i];

        if (stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && mediaInfo.videoStreamIndex < 0)
        {
            mediaInfo.videoStreamIndex = i;
            mediaInfo.width = stream->codecpar->width;
            mediaInfo.height = stream->codecpar->height;

            // 计算帧率
            if (stream->avg_frame_rate.num != 0 && stream->avg_frame_rate.den != 0)
            {
                mediaInfo.fps = stream->avg_frame_rate.num / stream->avg_frame_rate.den;
            }

            // 保存编解码器参数
            mediaInfo.videoCodecPar = avcodec_parameters_alloc();
            avcodec_parameters_copy(mediaInfo.videoCodecPar, stream->codecpar);
        }
        else if (stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO && mediaInfo.audioStreamIndex < 0)
        {
            mediaInfo.audioStreamIndex = i;
            mediaInfo.sampleRate = stream->codecpar->sample_rate;

            // 获取通道数
            mediaInfo.channels = stream->codecpar->channels;

            // 保存编解码器参数
            mediaInfo.audioCodecPar = avcodec_parameters_alloc();
            avcodec_parameters_copy(mediaInfo.audioCodecPar, stream->codecpar);
        }
    }

    // 获取总时长
    if (formatContext->duration != AV_NOPTS_VALUE)
    {
        mediaInfo.duration = formatContext->duration / (double)AV_TIME_BASE;
    }

    // 打印媒体信息
    std::cout << "媒体信息:" << std::endl;
    std::cout << "  时长: " << mediaInfo.duration << " 秒" << std::endl;

    if (mediaInfo.videoStreamIndex >= 0)
    {
        std::cout << "  视频: " << mediaInfo.width << "x" << mediaInfo.height
                  << ", " << mediaInfo.fps << " fps" << std::endl;
    }
    else
    {
        std::cout << "  无视频流" << std::endl;
    }

    if (mediaInfo.audioStreamIndex >= 0)
    {
        std::cout << "  音频: " << mediaInfo.sampleRate << " Hz, "
                  << mediaInfo.channels << " 声道" << std::endl;
    }
    else
    {
        std::cout << "  无音频流" << std::endl;
    }

    return (mediaInfo.videoStreamIndex >= 0 || mediaInfo.audioStreamIndex >= 0);
}

// 关闭输入文件
void Demux::closeInputFile()
{
    if (mediaInfo.videoCodecPar)
    {
        avcodec_parameters_free(&mediaInfo.videoCodecPar);
    }

    if (mediaInfo.audioCodecPar)
    {
        avcodec_parameters_free(&mediaInfo.audioCodecPar);
    }

    if (formatContext)
    {
        avformat_close_input(&formatContext);
        avformat_free_context(formatContext);
        formatContext = nullptr;
    }
}

// 启动解复用线程
void Demux::start()
{
    if (isRunning)
    {
        return;
    }

    isRunning = true;
    isPaused = false;

    // 创建解复用线程
    demuxThread = std::thread(&Demux::demuxThreadFunc, this);
}

// 停止解复用线程
void Demux::stop()
{
    if (!isRunning)
    {
        return;
    }

    isRunning = false;

    // 等待线程结束
    if (demuxThread.joinable())
    {
        demuxThread.join();
    }
}

// 暂停/继续解复用
void Demux::pause(bool pause)
{
    isPaused = pause;
}

// 获取媒体信息
const MediaInfo &Demux::getMediaInfo() const
{
    return mediaInfo;
}

// 解复用线程函数
void Demux::demuxThreadFunc()
{
    if (!formatContext)
    {
        std::cerr << "解复用线程: 格式上下文为空" << std::endl;
        return;
    }

    AVPacket *packet = av_packet_alloc();
    if (!packet)
    {
        std::cerr << "解复用线程: 无法分配AVPacket" << std::endl;
        return;
    }

    std::cout << "解复用线程: 开始" << std::endl;

    // 用于调试的计数器
    int packetCount = 0;
    int videoPacketCount = 0;
    int audioPacketCount = 0;
    auto startTime = std::chrono::high_resolution_clock::now();

    // 主循环
    while (isRunning)
    {
        // // 处理暂停s
        // if (isPaused)
        // {
        //     std::this_thread::sleep_for(std::chrono::milliseconds(10));
        //     continue;
        // }

        // // 检查队列大小，避免内存占用过多
        // if (mediaInfo.videoStreamIndex >= 0 && videoQueue.getSize() > 500)
        // {
        //     std::this_thread::sleep_for(std::chrono::milliseconds(10));
        //     continue;
        // }

        // if (mediaInfo.audioStreamIndex >= 0 && audioQueue.getSize() > 100)
        // {
        //     std::this_thread::sleep_for(std::chrono::milliseconds(10));
        //     continue;
        // }

        // 读取下一个数据包
        int ret = av_read_frame(formatContext, packet);

        // 定期打印解复用状态
        packetCount++;
        if (packetCount % 100 == 0)
        {
            auto now = std::chrono::high_resolution_clock::now();
            double elapsedSeconds = std::chrono::duration<double>(now - startTime).count();
            std::cout << "解复用线程: 已读取 " << packetCount << " 个数据包 (视频: "
                      << videoPacketCount << ", 音频: " << audioPacketCount
                      << "), 队列大小: " << videoQueue.getSize()
                      << ", 耗时: " << elapsedSeconds << "秒" << std::endl;
        }

        if (ret < 0)
        {
            char errBuff[AV_ERROR_MAX_STRING_SIZE] = {0};
            av_strerror(ret, errBuff, AV_ERROR_MAX_STRING_SIZE);
            std::cerr << "解复用线程: 读取帧错误，错误码: " << ret
                      << "，错误信息: " << errBuff << std::endl;
            // 文件结束或错误
            if (ret == AVERROR_EOF)
            {
                std::cout << "解复用线程: 文件结束，已读取 " << packetCount
                          << " 个数据包 (视频: " << videoPacketCount
                          << ", 音频: " << audioPacketCount << ")" << std::endl;

                // 发送文件结束标记包到队列
                if (mediaInfo.videoStreamIndex >= 0)
                {
                    // 创建一个空数据包作为文件结束标记
                    AVPacket *eofPkt = av_packet_alloc();
                    av_packet_unref(eofPkt); // 确保内容为空
                    eofPkt->data = NULL;
                    eofPkt->size = 0;
                    eofPkt->stream_index = mediaInfo.videoStreamIndex;
                    // 用一个特殊的 flags 标记这是EOF包
                    eofPkt->flags = AV_PKT_FLAG_KEY | 0x100; // 自定义标记
                    videoQueue.push(eofPkt);
                    std::cout << "解复用线程: 已发送视频EOF标记包" << std::endl;
                }

                if (mediaInfo.audioStreamIndex >= 0)
                {
                    AVPacket *eofPkt = av_packet_alloc();
                    av_packet_unref(eofPkt);
                    eofPkt->data = NULL;
                    eofPkt->size = 0;
                    eofPkt->stream_index = mediaInfo.audioStreamIndex;
                    eofPkt->flags = AV_PKT_FLAG_KEY | 0x100; // 自定义标记
                    audioQueue.push(eofPkt);
                    std::cout << "解复用线程: 已发送音频EOF标记包" << std::endl;
                }

                isEOF = true;
            }
            else
            {
                // 检查是否是由于视频结构复杂导致的无法读取
                if (ret == AVERROR(EAGAIN))
                {
                    std::cout << "解复用线程: 需要更多数据，继续" << std::endl;
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    continue;
                }
                else if (ret == AVERROR_INVALIDDATA)
                {
                    std::cerr << "解复用线程: 无效数据，跳过" << std::endl;
                    continue;
                }
            }
            break;
        }

        // 处理数据包
        if (packet->stream_index == mediaInfo.videoStreamIndex)
        {
            // 复制数据包并放入视频队列
            AVPacket *videoPkt = av_packet_alloc();
            av_packet_ref(videoPkt, packet);
            videoQueue.push(videoPkt);
            videoPacketCount++;

            // 检查是否是关键帧
            if (packet->flags & AV_PKT_FLAG_KEY)
            {
                if (videoPacketCount % 10 == 0)
                {
                    std::cout << "解复用线程: 读取到视频关键帧，PTS: " << packet->pts
                              << ", 总包数: " << videoPacketCount << std::endl;
                }
            }
        }
        else if (packet->stream_index == mediaInfo.audioStreamIndex)
        {
            // 复制数据包并放入音频队列
            AVPacket *audioPkt = av_packet_alloc();
            av_packet_ref(audioPkt, packet);
            audioQueue.push(audioPkt);
            audioPacketCount++;
        }

        // 释放原始数据包
        av_packet_unref(packet);
    }

    // 清理
    av_packet_free(&packet);

    // 如果线程正常结束，设置EOF标志
    isEOF = true;

    // 打印最终统计
    auto endTime = std::chrono::high_resolution_clock::now();
    double totalSeconds = std::chrono::duration<double>(endTime - startTime).count();
    std::cout << "解复用线程: 结束，总共处理 " << packetCount << " 个数据包，耗时 "
              << totalSeconds << " 秒" << std::endl;
}

// 检查解复用是否完成
bool Demux::isFinished() const
{
    // 如果已经设置了EOF标志，或者线程已经停止，则认为解复用已完成
    return isEOF || !isRunning;
}
