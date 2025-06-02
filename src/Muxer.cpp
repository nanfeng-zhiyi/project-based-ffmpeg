#include "../include/Muxer.h"
#include <iostream>
#include <chrono>
#include <iomanip> // 用于格式化输出
#include <cmath>   // 用于数学计算

// 引入FFmpeg头文件
extern "C"
{
#include "../ffmpeg/include_ffmpeg/libavformat/avformat.h"
#include "../ffmpeg/include_ffmpeg/libavcodec/avcodec.h"
#include "../ffmpeg/include_ffmpeg/libavutil/avutil.h"
#include "../ffmpeg/include_ffmpeg/libavutil/time.h"
#include "../ffmpeg/include_ffmpeg/libavutil/mathematics.h"
}

// 构造函数
Muxer::Muxer(VideoPacketQueue &videoQueue, AudioPacketQueue &audioQueue)
    : formatContext(nullptr),
      videoStream(nullptr),
      videoCodecContext(nullptr),
      audioStream(nullptr),
      audioCodecContext(nullptr),
      videoPacketQueue(videoQueue),
      audioPacketQueue(audioQueue),
      isRunning(false),
      isPaused(false),
      outputFile(""),
      videoPacketCount(0),
      audioPacketCount(0),
      playbackSpeed(1.0),
      lastVideoPts(AV_NOPTS_VALUE),
      lastVideoDts(AV_NOPTS_VALUE),
      lastAudioPts(AV_NOPTS_VALUE),
      lastAudioDts(AV_NOPTS_VALUE)
{
}

// 析构函数
Muxer::~Muxer()
{
    stop();
    closeMuxer();
}

// 初始化复用器
bool Muxer::init(const std::string &outputFile, AVCodecContext *videoCodecCtx, AVCodecContext *audioCodecCtx)
{
    // 保存输出文件路径
    this->outputFile = outputFile;

    // 保存编码器上下文
    this->videoCodecContext = videoCodecCtx;
    this->audioCodecContext = audioCodecCtx;

    // 初始化复用器
    return initMuxer();
}

// 私有方法：初始化复用器
bool Muxer::initMuxer()
{
    int ret;

    // 根据输出文件名推断格式
    ret = avformat_alloc_output_context2(&formatContext, nullptr, nullptr, outputFile.c_str());
    if (ret < 0 || !formatContext)
    {
        std::cerr << "无法创建输出格式上下文: " << outputFile << std::endl;
        return false;
    }

    // 对于MP4格式，设置movflags以优化文件结构
    if (formatContext->oformat && strcmp(formatContext->oformat->name, "mp4") == 0)
    {
        // 设置faststart标志，将moov atom放在文件前面
        av_dict_set(&formatContext->metadata, "movflags", "faststart", 0);

        // 也可以通过AVOptions设置
        AVDictionary *opts = nullptr;
        av_dict_set(&opts, "movflags", "faststart", 0);
        formatContext->flags |= AVFMT_FLAG_GENPTS;

        // 如果需要，可以在写入文件头时使用这些选项
        // 保存选项以便在avformat_write_header中使用
        av_dict_free(&opts);
    }

    // 添加视频流（如果有视频编码器）
    if (videoCodecContext)
    {
        videoStream = avformat_new_stream(formatContext, nullptr);
        if (!videoStream)
        {
            std::cerr << "无法创建输出视频流" << std::endl;
            closeMuxer();
            return false;
        }

        // 设置视频流参数
        ret = avcodec_parameters_from_context(videoStream->codecpar, videoCodecContext);
        if (ret < 0)
        {
            std::cerr << "无法复制视频编码器参数" << std::endl;
            closeMuxer();
            return false;
        }

        // 设置时间基
        videoStream->time_base = videoCodecContext->time_base;
        std::cout << "视频流时间基: " << videoStream->time_base.num << "/" << videoStream->time_base.den << std::endl;
    }

    // 添加音频流（如果有音频编码器）
    if (audioCodecContext)
    {
        audioStream = avformat_new_stream(formatContext, nullptr);
        if (!audioStream)
        {
            std::cerr << "无法创建输出音频流" << std::endl;
            closeMuxer();
            return false;
        }

        // 设置音频流参数
        ret = avcodec_parameters_from_context(audioStream->codecpar, audioCodecContext);
        if (ret < 0)
        {
            std::cerr << "无法复制音频编码器参数" << std::endl;
            closeMuxer();
            return false;
        }

        // 设置时间基
        audioStream->time_base = audioCodecContext->time_base;
        std::cout << "音频流时间基: " << audioStream->time_base.num << "/" << audioStream->time_base.den << std::endl;
    }

    // 打印输出格式信息
    av_dump_format(formatContext, 0, outputFile.c_str(), 1);

    // 打开输出文件
    if (!(formatContext->oformat->flags & AVFMT_NOFILE))
    {
        // 创建选项字典
        AVDictionary *opts = nullptr;
        av_dict_set(&opts, "movflags", "faststart", 0);

        ret = avio_open(&formatContext->pb, outputFile.c_str(), AVIO_FLAG_WRITE);
        if (ret < 0)
        {
            std::cerr << "无法打开输出文件: " << outputFile << std::endl;
            av_dict_free(&opts);
            closeMuxer();
            return false;
        }

        // 写入文件头
        ret = avformat_write_header(formatContext, &opts);
        av_dict_free(&opts);
    }
    else
    {
        // 写入文件头
        ret = avformat_write_header(formatContext, nullptr);
    }

    if (ret < 0)
    {
        std::cerr << "无法写入输出文件头" << std::endl;
        closeMuxer();
        return false;
    }

    // 重置时间戳跟踪变量
    lastVideoPts = AV_NOPTS_VALUE;
    lastVideoDts = AV_NOPTS_VALUE;
    lastAudioPts = AV_NOPTS_VALUE;
    lastAudioDts = AV_NOPTS_VALUE;

    std::cout << "复用器初始化成功，输出文件: " << outputFile << std::endl;
    return true;
}

// 私有方法：关闭复用器
void Muxer::closeMuxer()
{
    if (formatContext)
    {
        // 写入文件尾（如果文件已经打开）
        if (formatContext->pb)
        {
            // 确保写入文件尾
            av_write_trailer(formatContext);

            // 刷新缓冲区
            avio_flush(formatContext->pb);
        }

        // 关闭输出文件
        if (formatContext->pb && !(formatContext->oformat->flags & AVFMT_NOFILE))
        {
            avio_closep(&formatContext->pb);
        }

        // 释放格式上下文
        avformat_free_context(formatContext);
        formatContext = nullptr;
    }

    // 重置流指针
    videoStream = nullptr;
    audioStream = nullptr;

    // 重置编码器上下文指针（不需要释放，因为这些是外部传入的）
    videoCodecContext = nullptr;
    audioCodecContext = nullptr;
}

// 启动复用线程
void Muxer::start()
{
    if (isRunning)
    {
        return;
    }

    isRunning = true;
    isPaused = false;

    // 创建复用线程
    muxThread = std::thread(&Muxer::muxThreadFunc, this);
}

// 停止复用线程
void Muxer::stop()
{
    if (!isRunning)
    {
        return;
    }

    isRunning = false;

    // 等待线程结束
    if (muxThread.joinable())
    {
        // 设置超时，防止无限等待
        auto timeout = std::chrono::seconds(5);
        auto start = std::chrono::steady_clock::now();

        while (muxThread.joinable() &&
               std::chrono::steady_clock::now() - start < timeout)
        {
            // 尝试等待一小段时间
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        // 如果线程仍在运行，强制结束
        if (muxThread.joinable())
        {
            std::cerr << "警告: 复用线程未能在超时时间内结束，强制结束" << std::endl;
            // 在实际生产环境中，这里应该使用更安全的方法来终止线程
            // 但在C++中没有标准的安全方法来强制终止线程
            // 这里我们仍然调用join()，但已经设置了isRunning为false，线程应该会退出
            muxThread.join();
        }
    }

    // 关闭复用器
    closeMuxer();
}

// 暂停/恢复复用
void Muxer::pause(bool pause)
{
    isPaused = pause;
}

// 复用线程函数
void Muxer::muxThreadFunc()
{
    AVPacket *packet = nullptr;
    bool videoFinished = !videoStream;
    bool audioFinished = !audioStream;

    // 添加超时机制，防止无限等待
    int emptyQueueCount = 0;
    const int MAX_EMPTY_COUNT = 100; // 如果连续100次队列为空，则认为处理完成

    // 记录上一个音频包的时间戳，用于检测不连续性
    int64_t lastAudioPts = AV_NOPTS_VALUE;
    int64_t lastVideoPts = AV_NOPTS_VALUE;

    // 记录最后一个音频包的时间（秒）
    double lastAudioTimeSec = 0.0;
    double lastVideoTimeSec = 0.0;

    // 记录音频流中断的检测变量
    int audioSilenceCount = 0;
    const int MAX_AUDIO_SILENCE = 50; // 如果连续50次没有音频包，检查是否中断
    bool audioStreamInterrupted = false;

    // 交替处理视频和音频包的计数器
    int packetCounter = 0;

    // 音视频同步相关变量
    double audioVideoSyncThreshold = 0.5; // 音视频同步阈值（秒）
    bool needSync = false;

    // 调试信息：打印倍速设置
    std::cout << "【调试】复用线程启动，当前播放速度: " << playbackSpeed << "倍速" << std::endl;

    // 调试信息：记录处理速度
    auto startTime = std::chrono::steady_clock::now();
    int packetProcessedCount = 0;
    const int REPORT_INTERVAL = 500; // 每处理500个包报告一次

    while (isRunning && (!videoFinished || !audioFinished))
    {
        // 如果暂停，则等待
        if (isPaused)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        bool processedPacket = false;
        packetCounter++;

        // 调试信息：定期打印队列状态
        if (packetCounter % 1000 == 0)
        {
            std::cout << "【调试】队列状态 - 视频: " << videoPacketQueue.getSize()
                      << " 包, 音频: " << audioPacketQueue.getSize() << " 包" << std::endl;
        }

        // 交替处理视频和音频包，确保音频包得到及时处理
        // 在倍速播放时，优先处理音频包，以保持音频流畅
        bool tryAudioFirst = (playbackSpeed != 1.0) || (packetCounter % 2 == 0);

        // 音视频同步相关变量
        // 根据播放速度调整同步阈值，倍速播放时允许更大的差异
        double adjustedSyncThreshold = audioVideoSyncThreshold;
        if (playbackSpeed > 1.0)
        {
            // 倍速播放时，增加同步阈值，避免过度调整
            // 使用改进的阈值计算方法，更加平滑
            adjustedSyncThreshold = audioVideoSyncThreshold * sqrt(playbackSpeed);

            // 在高速播放时，额外增加阈值容忍度
            if (playbackSpeed > 4.0)
            {
                adjustedSyncThreshold *= 1.5;
            }

            // 打印更详细的同步阈值信息
            if (packetCounter % 500 == 0)
            {
                std::cout << "【调试】当前音视频同步阈值: " << adjustedSyncThreshold
                          << " 秒 (基准值: " << audioVideoSyncThreshold
                          << "，播放速度: " << playbackSpeed << ")" << std::endl;
            }
        }

        // 检查音视频同步
        if (lastVideoTimeSec > 0 && lastAudioTimeSec > 0 &&
            fabs(lastAudioTimeSec - lastVideoTimeSec) > adjustedSyncThreshold)
        {
            std::cout << "【调试】音视频不同步，视频时间: " << lastVideoTimeSec
                      << "秒, 音频时间: " << lastAudioTimeSec << "秒, 差值: "
                      << (lastAudioTimeSec - lastVideoTimeSec) << "秒, 调整阈值: "
                      << adjustedSyncThreshold << "秒" << std::endl;

            // 根据差值大小调整处理策略
            if (lastAudioTimeSec > lastVideoTimeSec + adjustedSyncThreshold)
            {
                // 音频时间超前，优先处理视频包
                tryAudioFirst = false;
            }
            else if (lastVideoTimeSec > lastAudioTimeSec + adjustedSyncThreshold)
            {
                // 视频时间超前，优先处理音频包
                tryAudioFirst = true;
            }

            needSync = true;
        }

        // 尝试处理音频包（如果应该先处理音频）
        if (tryAudioFirst && !audioFinished && !audioPacketQueue.isEmpty())
        {
            packet = static_cast<AVPacket *>(audioPacketQueue.pop());
            if (packet)
            {
                if (packet->data)
                {
                    // 重置音频静默计数
                    audioSilenceCount = 0;

                    // 更新最后一个音频包的时间（秒）
                    if (audioCodecContext && packet->pts != AV_NOPTS_VALUE)
                    {
                        double currentAudioTimeSec = packet->pts * av_q2d(audioCodecContext->time_base);

                        // 调试信息：打印音频时间戳跳跃
                        if (lastAudioTimeSec > 0 && fabs(currentAudioTimeSec - lastAudioTimeSec) > 0.1)
                        {
                            std::cout << "【调试】音频时间戳跳跃: " << lastAudioTimeSec
                                      << " -> " << currentAudioTimeSec
                                      << " (差值: " << (currentAudioTimeSec - lastAudioTimeSec) << "秒)"
                                      << std::endl;
                        }

                        lastAudioTimeSec = currentAudioTimeSec;

                        // 检查音视频同步
                        if (lastVideoTimeSec > 0 && fabs(lastAudioTimeSec - lastVideoTimeSec) > audioVideoSyncThreshold)
                        {
                            std::cout << "【调试】音视频不同步，音频时间: " << lastAudioTimeSec
                                      << "秒, 视频时间: " << lastVideoTimeSec << "秒, 差值: "
                                      << (lastAudioTimeSec - lastVideoTimeSec) << "秒" << std::endl;
                            needSync = true;
                        }
                    }

                    // 写入音频包
                    if (writePacket(packet, false))
                    {
                        audioPacketCount++;
                        packetProcessedCount++;
                    }
                    processedPacket = true;
                }
                else
                {
                    // 空包表示音频流结束
                    audioFinished = true;
                    std::cout << "【调试】音频流结束标记已处理" << std::endl;
                }
                av_packet_free(&packet);
            }
        }
        // 处理视频包
        else if (!videoFinished && !videoPacketQueue.isEmpty())
        {
            packet = static_cast<AVPacket *>(videoPacketQueue.pop());
            if (packet)
            {
                if (packet->data)
                {
                    // 如果有音频流但长时间没有音频包，增加计数
                    if (!audioFinished && audioStream)
                    {
                        audioSilenceCount++;

                        // 检测音频流是否中断
                        if (audioSilenceCount >= MAX_AUDIO_SILENCE && !audioStreamInterrupted)
                        {
                            audioStreamInterrupted = true;
                            std::cout << "【调试】检测到音频流中断！已处理 " << audioSilenceCount
                                      << " 个视频包但没有音频包" << std::endl;
                        }
                    }

                    // 更新最后一个视频包的时间（秒）
                    if (videoCodecContext && packet->pts != AV_NOPTS_VALUE)
                    {
                        double currentVideoTimeSec = packet->pts * av_q2d(videoCodecContext->time_base);

                        // 调试信息：打印视频时间戳跳跃
                        if (lastVideoTimeSec > 0 && fabs(currentVideoTimeSec - lastVideoTimeSec) > 0.1)
                        {
                            std::cout << "【调试】视频时间戳跳跃: " << lastVideoTimeSec
                                      << " -> " << currentVideoTimeSec
                                      << " (差值: " << (currentVideoTimeSec - lastVideoTimeSec) << "秒)"
                                      << std::endl;
                        }

                        lastVideoTimeSec = currentVideoTimeSec;

                        // 检查音视频同步
                        if (lastAudioTimeSec > 0 && fabs(lastVideoTimeSec - lastAudioTimeSec) > audioVideoSyncThreshold)
                        {
                            std::cout << "【调试】音视频不同步，视频时间: " << lastVideoTimeSec
                                      << "秒, 音频时间: " << lastAudioTimeSec << "秒, 差值: "
                                      << (lastVideoTimeSec - lastAudioTimeSec) << "秒" << std::endl;
                            needSync = true;
                        }
                    }

                    // 写入视频包
                    if (writePacket(packet, true))
                    {
                        videoPacketCount++;
                        packetProcessedCount++;
                    }
                    processedPacket = true;
                }
                else
                {
                    // 空包表示视频流结束
                    videoFinished = true;
                    std::cout << "【调试】视频流结束标记已处理" << std::endl;
                }
                av_packet_free(&packet);
            }
        }
        // 如果两个队列都为空，等待一段时间
        else
        {
            emptyQueueCount++;
            if (emptyQueueCount >= MAX_EMPTY_COUNT)
            {
                std::cout << "【调试】复用器: 队列长时间为空，可能已处理完所有数据" << std::endl;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        // 如果处理了数据包，重置空队列计数
        if (processedPacket)
        {
            emptyQueueCount = 0;

            // 调试信息：定期报告处理速度
            if (packetProcessedCount % REPORT_INTERVAL == 0)
            {
                auto currentTime = std::chrono::steady_clock::now();
                auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - startTime).count();
                double packetsPerSecond = (packetProcessedCount * 1000.0) / elapsedMs;

                std::cout << "【调试】处理速度: " << std::fixed << std::setprecision(2)
                          << packetsPerSecond << " 包/秒, 已处理 " << packetProcessedCount
                          << " 个包，用时 " << (elapsedMs / 1000.0) << " 秒" << std::endl;
            }
        }
    }

    // 完成复用
    finalizeFile();

    // 调试信息：打印最终统计
    auto endTime = std::chrono::steady_clock::now();
    auto totalElapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();
    double avgPacketsPerSecond = ((videoPacketCount + audioPacketCount) * 1000.0) / totalElapsedMs;

    std::cout << "【调试】复用线程结束，共处理 " << videoPacketCount << " 个视频包和 "
              << audioPacketCount << " 个音频包，平均处理速度: " << std::fixed
              << std::setprecision(2) << avgPacketsPerSecond << " 包/秒" << std::endl;

    if (needSync)
    {
        std::cout << "【调试】警告：在处理过程中检测到音视频同步问题，这可能导致播放卡顿" << std::endl;
    }
}

// 写入数据包
bool Muxer::writePacket(AVPacket *packet, bool isVideo)
{
    if (!formatContext || !packet)
    {
        return false;
    }

    AVStream *stream = isVideo ? videoStream : audioStream;
    if (!stream)
    {
        return false;
    }

    // 设置流索引
    packet->stream_index = stream->index;

    // 转换时间戳
    AVRational srcTimeBase = isVideo ? videoCodecContext->time_base : audioCodecContext->time_base;
    AVRational dstTimeBase = stream->time_base;

    // 保存原始时间戳用于调试
    int64_t origPts = packet->pts;
    int64_t origDts = packet->dts;
    int64_t origDuration = packet->duration;

    // 调试信息：详细记录时间戳处理过程（每500个包记录一次）
    bool detailedLog = (isVideo && videoPacketCount % 500 == 0) || (!isVideo && audioPacketCount % 500 == 0);

    if (detailedLog)
    {
        std::cout << "【调试-详细】" << (isVideo ? "视频" : "音频") << "包 #"
                  << (isVideo ? videoPacketCount : audioPacketCount)
                  << " 时间戳处理开始: PTS=" << packet->pts
                  << ", DTS=" << packet->dts
                  << ", 持续时间=" << packet->duration
                  << ", 源时间基=" << srcTimeBase.num << "/" << srcTimeBase.den
                  << ", 目标时间基=" << dstTimeBase.num << "/" << dstTimeBase.den
                  << ", 播放速度=" << playbackSpeed
                  << std::endl;
    }

    // 处理播放速度 - 在时间基转换前应用
    if (playbackSpeed != 1.0)
    {
        // 修改：对视频和音频包都应用时间戳调整
        // 注释：音频内容通过atempo滤镜处理了速度，但时间戳也需要调整以保持同步

        // 对PTS进行调整 - 使用改进的计算方法
        if (packet->pts != AV_NOPTS_VALUE)
        {
            // 注意：这里需要与VideoFilter中的setpts滤镜保持一致
            // 在VideoFilter中，对于playbackSpeed > 1.0，使用setpts=PTS/TB/playbackSpeed*TB
            // 对于playbackSpeed < 1.0，使用setpts=PTS/TB*(1.0/playbackSpeed)*TB
            double adjustedPts;
            if (playbackSpeed > 1.0)
            {
                // 快速播放：缩短时间戳
                adjustedPts = static_cast<double>(packet->pts) / playbackSpeed;
            }
            else
            {
                // 慢速播放：延长时间戳
                adjustedPts = static_cast<double>(packet->pts) * (1.0 / playbackSpeed);
            }

            // 避免四舍五入导致的时间戳跳跃
            packet->pts = static_cast<int64_t>(adjustedPts);

            if (detailedLog)
            {
                std::cout << "【调试-详细】应用播放速度后 PTS: " << origPts << " -> " << packet->pts << std::endl;
            }
        }

        // 对DTS进行调整 - 使用改进的计算方法
        if (packet->dts != AV_NOPTS_VALUE)
        {
            // 使用更精确的计算方法
            double adjustedDts;
            if (playbackSpeed > 1.0)
            {
                // 快速播放：缩短时间戳
                adjustedDts = static_cast<double>(packet->dts) / playbackSpeed;
            }
            else
            {
                // 慢速播放：延长时间戳
                adjustedDts = static_cast<double>(packet->dts) * (1.0 / playbackSpeed);
            }

            // 避免四舍五入导致的时间戳跳跃
            packet->dts = static_cast<int64_t>(adjustedDts);

            if (detailedLog)
            {
                std::cout << "【调试-详细】应用播放速度后 DTS: " << origDts << " -> " << packet->dts << std::endl;
            }
        }

        // 调整持续时间 - 使用更精确的计算
        if (packet->duration > 0)
        {
            double adjustedDuration;
            if (playbackSpeed > 1.0)
            {
                // 快速播放：缩短持续时间
                adjustedDuration = static_cast<double>(packet->duration) / playbackSpeed;
            }
            else
            {
                // 慢速播放：延长持续时间
                adjustedDuration = static_cast<double>(packet->duration) * (1.0 / playbackSpeed);
            }
            packet->duration = static_cast<int64_t>(adjustedDuration);

            if (detailedLog)
            {
                std::cout << "【调试-详细】应用播放速度后持续时间: " << origDuration
                          << " -> " << packet->duration << std::endl;
            }
        }
    }

    // 时间基转换
    if (packet->pts != AV_NOPTS_VALUE)
    {
        int64_t ptsBeforeRescale = packet->pts;
        packet->pts = rescaleTimestamp(packet->pts, srcTimeBase, dstTimeBase, isVideo);

        if (detailedLog)
        {
            std::cout << "【调试-详细】时间基转换后 PTS: " << ptsBeforeRescale << " -> " << packet->pts << std::endl;
        }
    }

    if (packet->dts != AV_NOPTS_VALUE)
    {
        int64_t dtsBeforeRescale = packet->dts;
        packet->dts = rescaleTimestamp(packet->dts, srcTimeBase, dstTimeBase, isVideo);

        if (detailedLog)
        {
            std::cout << "【调试-详细】时间基转换后 DTS: " << dtsBeforeRescale << " -> " << packet->dts << std::endl;
        }
    }
    else
    {
        // 如果DTS无效，使用PTS
        packet->dts = packet->pts;

        if (detailedLog)
        {
            std::cout << "【调试-详细】DTS无效，使用PTS: " << packet->pts << std::endl;
        }
    }

    // 确保时间戳单调递增
    int64_t &lastPts = isVideo ? lastVideoPts : lastAudioPts;
    int64_t &lastDts = isVideo ? lastVideoDts : lastAudioDts;

    // 检查并修正PTS
    if (lastPts != AV_NOPTS_VALUE && packet->pts <= lastPts)
    {
        int64_t ptsBeforeCorrection = packet->pts;
        packet->pts = lastPts + 1;

        if (detailedLog || ptsBeforeCorrection < lastPts - 1000) // 如果差距很大，总是记录
        {
            std::cout << "【调试-警告】" << (isVideo ? "视频" : "音频") << "PTS不单调递增: "
                      << ptsBeforeCorrection << " <= " << lastPts
                      << "，已修正为: " << packet->pts << std::endl;
        }
    }

    // 检查并修正DTS
    if (lastDts != AV_NOPTS_VALUE && packet->dts <= lastDts)
    {
        int64_t dtsBeforeCorrection = packet->dts;
        packet->dts = lastDts + 1;

        if (detailedLog || dtsBeforeCorrection < lastDts - 1000) // 如果差距很大，总是记录
        {
            std::cout << "【调试-警告】" << (isVideo ? "视频" : "音频") << "DTS不单调递增: "
                      << dtsBeforeCorrection << " <= " << lastDts
                      << "，已修正为: " << packet->dts << std::endl;
        }
    }

    // 确保DTS不大于PTS（这是MP4容器的要求）
    if (packet->dts > packet->pts)
    {
        int64_t dtsBeforeCorrection = packet->dts;
        packet->dts = packet->pts;

        if (detailedLog)
        {
            std::cout << "【调试-详细】DTS大于PTS，已修正: " << dtsBeforeCorrection
                      << " -> " << packet->dts << std::endl;
        }
    }

    // 更新上一个时间戳
    lastPts = packet->pts;
    lastDts = packet->dts;

    // 转换持续时间
    if (packet->duration > 0)
    {
        int64_t durationBeforeRescale = packet->duration;
        packet->duration = rescaleTimestamp(packet->duration, srcTimeBase, dstTimeBase, isVideo);

        if (detailedLog)
        {
            std::cout << "【调试-详细】时间基转换后持续时间: " << durationBeforeRescale
                      << " -> " << packet->duration << std::endl;
        }
    }

    // 打印调试信息（每100个包打印一次）
    if ((isVideo && videoPacketCount % 100 == 0) || (!isVideo && audioPacketCount % 100 == 0))
    {
        std::cout << "【调试】" << (isVideo ? "视频" : "音频") << "包 #" << (isVideo ? videoPacketCount : audioPacketCount)
                  << ": PTS=" << packet->pts
                  << ", DTS=" << packet->dts
                  << ", 原始PTS=" << origPts
                  << ", 原始DTS=" << origDts
                  << ", 播放速度=" << playbackSpeed
                  << std::endl;
    }

    // 写入数据包
    int ret = av_interleaved_write_frame(formatContext, packet);
    if (ret < 0)
    {
        char errBuf[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_strerror(ret, errBuf, AV_ERROR_MAX_STRING_SIZE);
        std::cerr << "【调试-错误】写入" << (isVideo ? "视频" : "音频") << "包失败: " << errBuf
                  << " (PTS=" << packet->pts << ", DTS=" << packet->dts << ")" << std::endl;
        return false;
    }

    if (detailedLog)
    {
        std::cout << "【调试-详细】" << (isVideo ? "视频" : "音频") << "包 #"
                  << (isVideo ? videoPacketCount : audioPacketCount)
                  << " 成功写入，最终时间戳: PTS=" << packet->pts
                  << ", DTS=" << packet->dts
                  << ", 持续时间=" << packet->duration
                  << std::endl;
    }

    return true;
}

// 完成文件
bool Muxer::finalizeFile()
{
    if (!formatContext)
    {
        return false;
    }

    std::cout << "正在完成文件..." << std::endl;

    // 写入文件尾
    int ret = av_write_trailer(formatContext);
    if (ret < 0)
    {
        char errBuf[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_strerror(ret, errBuf, AV_ERROR_MAX_STRING_SIZE);
        std::cerr << "写入文件尾失败: " << errBuf << std::endl;
        return false;
    }

    // 确保所有数据都写入磁盘
    if (formatContext->pb)
    {
        avio_flush(formatContext->pb);
    }

    std::cout << "复用完成，输出文件: " << outputFile << std::endl;
    std::cout << "视频包数量: " << videoPacketCount << ", 音频包数量: " << audioPacketCount << std::endl;

    return true;
}

// 时间基转换
int64_t Muxer::rescaleTimestamp(int64_t timestamp, const AVRational &srcTimeBase, const AVRational &dstTimeBase, bool isVideo)
{
    // 添加对播放速度的支持，对视频和音频时间戳都进行调整
    if (playbackSpeed != 1.0 && timestamp != AV_NOPTS_VALUE)
    {
        // 对于倍速播放，需要调整时间戳
        // 注意：这里需要与VideoFilter中的setpts滤镜保持一致
        // 在VideoFilter中，对于playbackSpeed > 1.0，使用setpts=PTS/TB/playbackSpeed*TB
        // 对于playbackSpeed < 1.0，使用setpts=PTS/TB*(1.0/playbackSpeed)*TB
        double adjustedTimestamp;
        if (playbackSpeed > 1.0)
        {
            // 快速播放：缩短时间戳
            adjustedTimestamp = static_cast<double>(timestamp) / playbackSpeed;
        }
        else
        {
            // 慢速播放：延长时间戳
            adjustedTimestamp = static_cast<double>(timestamp) * (1.0 / playbackSpeed);
        }

        // 避免四舍五入导致的时间戳跳跃
        timestamp = static_cast<int64_t>(adjustedTimestamp);

        // 在调试模式下额外增加的日志
        static int rescaleCount = 0;
        if (++rescaleCount % 1000 == 0)
        {
            std::cout << "【调试】rescaleTimestamp: 原始值=" << (playbackSpeed > 1.0 ? timestamp * playbackSpeed : timestamp / playbackSpeed)
                      << "，调整后=" << timestamp
                      << "，播放速度=" << playbackSpeed
                      << "，类型=" << (isVideo ? "视频" : "音频") << std::endl;
        }
    }

    // 使用FFmpeg的时间基转换函数
    return av_rescale_q(timestamp, srcTimeBase, dstTimeBase);
}

// 获取视频包数量
int Muxer::getVideoPacketCount() const
{
    return videoPacketCount;
}

// 获取音频包数量
int Muxer::getAudioPacketCount() const
{
    return audioPacketCount;
}

// 获取输出文件路径
std::string Muxer::getOutputFile() const
{
    return outputFile;
}

// 检查复用器是否活动
bool Muxer::isActive() const
{
    return isRunning && !isPaused && formatContext != nullptr;
}

// 设置播放速度
void Muxer::setPlaybackSpeed(double speed)
{
    if (speed <= 0)
    {
        std::cerr << "复用器: 无效的播放速度: " << speed << std::endl;
        return;
    }

    // 保存旧的播放速度用于日志
    double oldSpeed = playbackSpeed;
    playbackSpeed = speed;

    std::cout << "【调试】复用器: 已设置播放速度从 " << oldSpeed << " 变为 " << playbackSpeed << "倍速" << std::endl;

    // 重置时间戳跟踪变量，确保在速度变化后能够正确处理时间戳
    lastVideoPts = AV_NOPTS_VALUE;
    lastVideoDts = AV_NOPTS_VALUE;
    lastAudioPts = AV_NOPTS_VALUE;
    lastAudioDts = AV_NOPTS_VALUE;
}

// 获取当前播放速度
double Muxer::getPlaybackSpeed() const
{
    return playbackSpeed;
}
