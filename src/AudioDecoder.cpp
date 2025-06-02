#include "../include/AudioDecoder.h"
#include <iostream>
#include <vector>

// 引入FFmpeg头文件
extern "C"
{
#include "ffmpeg/include_ffmpeg/libavcodec/avcodec.h"
#include "ffmpeg/include_ffmpeg/libavutil/opt.h"
#include "ffmpeg/include_ffmpeg/libavutil/audio_fifo.h"
#include "ffmpeg/include_ffmpeg/libswresample/swresample.h"
}

// 构造函数
AudioDecoder::AudioDecoder(AudioPacketQueue &packetQueue, AudioFrameQueue &decodedFrameQueue)
    : codecContext(nullptr),
      codec(nullptr),
      swrContext(nullptr),
      audioFifo(nullptr),
      packetQueue(packetQueue),
      decodedFrameQueue(decodedFrameQueue),
      isRunning(false),
      isPaused(false),
      frameCallback(nullptr),
      saveToPCM(false),
      directPcmOutput("")
{
}

// 析构函数
AudioDecoder::~AudioDecoder()
{
    stop();
    closePCMOutput();
    closeDecoder();
}

// 初始化解码器
bool AudioDecoder::init(AVCodecParameters *codecPar)
{
    if (!codecPar)
    {
        std::cerr << "音频解码器: 无效的编解码器参数" << std::endl;
        return false;
    }

    return initDecoder(codecPar);
}

// 内部初始化解码器方法
bool AudioDecoder::initDecoder(AVCodecParameters *codecPar)
{
    // 查找解码器
    codec = avcodec_find_decoder(codecPar->codec_id);
    if (!codec)
    {
        std::cerr << "音频解码器: 找不到解码器" << std::endl;
        return false;
    }

    // 分配解码器上下文
    codecContext = avcodec_alloc_context3(codec);
    if (!codecContext)
    {
        std::cerr << "音频解码器: 无法分配解码器上下文" << std::endl;
        return false;
    }

    // 复制编解码器参数到上下文
    if (avcodec_parameters_to_context(codecContext, codecPar) < 0)
    {
        std::cerr << "音频解码器: 无法复制编解码器参数" << std::endl;
        closeDecoder();
        return false;
    }

    // 打开解码器
    if (avcodec_open2(codecContext, codec, nullptr) < 0)
    {
        std::cerr << "音频解码器: 无法打开解码器" << std::endl;
        closeDecoder();
        return false;
    }

    // 初始化重采样上下文
    swrContext = swr_alloc();
    if (!swrContext)
    {
        std::cerr << "音频解码器: 无法分配重采样上下文" << std::endl;
        closeDecoder();
        return false;
    }

    // 设置输入格式
    av_opt_set_int(swrContext, "in_channel_layout", codecContext->channel_layout, 0);
    av_opt_set_int(swrContext, "in_sample_rate", codecContext->sample_rate, 0);
    av_opt_set_sample_fmt(swrContext, "in_sample_fmt", codecContext->sample_fmt, 0);

    // 设置输出格式 (固定为立体声, 44100Hz, S16格式)
    av_opt_set_int(swrContext, "out_channel_layout", AV_CH_LAYOUT_STEREO, 0);
    av_opt_set_int(swrContext, "out_sample_rate", 44100, 0);
    av_opt_set_sample_fmt(swrContext, "out_sample_fmt", AV_SAMPLE_FMT_S16, 0);

    // 初始化重采样上下文
    if (swr_init(swrContext) < 0)
    {
        std::cerr << "音频解码器: 无法初始化重采样上下文" << std::endl;
        closeDecoder();
        return false;
    }

    // 创建音频FIFO
    audioFifo = av_audio_fifo_alloc(AV_SAMPLE_FMT_S16, 2, 1);
    if (!audioFifo)
    {
        std::cerr << "音频解码器: 无法创建音频FIFO" << std::endl;
        closeDecoder();
        return false;
    }

    std::cout << "音频解码器: 初始化成功" << std::endl;
    std::cout << "  解码器: " << codec->name << std::endl;
    std::cout << "  采样率: " << codecContext->sample_rate << " Hz" << std::endl;
    std::cout << "  通道数: " << codecContext->channels << std::endl;
    std::cout << "  采样格式: " << av_get_sample_fmt_name(codecContext->sample_fmt) << std::endl;

    return true;
}

// 关闭解码器
void AudioDecoder::closeDecoder()
{
    if (audioFifo)
    {
        av_audio_fifo_free(audioFifo);
        audioFifo = nullptr;
    }

    if (swrContext)
    {
        swr_free(&swrContext);
        swrContext = nullptr;
    }

    if (codecContext)
    {
        avcodec_free_context(&codecContext);
        codecContext = nullptr;
    }

    codec = nullptr;
}

// 启动解码线程
void AudioDecoder::start()
{
    if (isRunning || !codecContext)
    {
        return;
    }

    isRunning = true;
    isPaused = false;

    // 创建解码线程
    decodeThread = std::thread(&AudioDecoder::decodeThreadFunc, this);
}

// 停止解码线程
void AudioDecoder::stop()
{
    if (!isRunning)
    {
        return;
    }

    isRunning = false;

    // 等待线程结束
    if (decodeThread.joinable())
    {
        decodeThread.join();
    }
}

// 暂停/继续解码
void AudioDecoder::pause(bool pause)
{
    isPaused = pause;
}

// 设置帧回调
void AudioDecoder::setFrameCallback(AudioFrameCallback callback)
{
    frameCallback = callback;
}

// 设置PCM文件输出
bool AudioDecoder::setPCMOutput(const std::string &filePath)
{
    // 关闭之前的文件（如果有）
    closePCMOutput();

    // 保存文件路径
    pcmFilePath = filePath;

    // 打开文件
    pcmFile.open(pcmFilePath, std::ios::binary);
    if (!pcmFile.is_open())
    {
        std::cerr << "音频解码器: 无法打开PCM输出文件: " << pcmFilePath << std::endl;
        return false;
    }

    saveToPCM = true;
    std::cout << "音频解码器: PCM输出文件已设置: " << pcmFilePath << std::endl;
    return true;
}

// 关闭PCM文件输出
void AudioDecoder::closePCMOutput()
{
    if (pcmFile.is_open())
    {
        pcmFile.close();
    }
    saveToPCM = false;
}

// 保存PCM数据到文件
void AudioDecoder::savePCMData(const uint8_t *data, int size)
{
    if (!saveToPCM || !pcmFile.is_open() || !data || size <= 0)
    {
        return;
    }

    pcmFile.write(reinterpret_cast<const char *>(data), size);
    pcmFile.flush();
}

// 获取采样率
int AudioDecoder::getSampleRate() const
{
    return codecContext ? codecContext->sample_rate : 0;
}

// 获取通道数
int AudioDecoder::getChannels() const
{
    return codecContext ? codecContext->channels : 0;
}

// 获取解码器名称
const char *AudioDecoder::getCodecName() const
{
    if (!codec)
    {
        return "unknown";
    }

    return codec->name;
}

// 获取编解码器上下文
AVCodecContext *AudioDecoder::getCodecContext() const
{
    return codecContext;
}

// 检查队列是否为空
bool AudioDecoder::isQueueEmpty() const
{
    return packetQueue.isEmpty();
}

// 解码线程函数
void AudioDecoder::decodeThreadFunc()
{
    if (!codecContext || !swrContext || !audioFifo)
    {
        std::cerr << "音频解码线程: 解码器未正确初始化" << std::endl;
        return;
    }

    // 分配AVPacket
    AVPacket *packet = av_packet_alloc();
    if (!packet)
    {
        std::cerr << "音频解码线程: 无法分配AVPacket" << std::endl;
        return;
    }

    // 分配AVFrame
    AVFrame *frame = av_frame_alloc();
    if (!frame)
    {
        std::cerr << "音频解码线程: 无法分配AVFrame" << std::endl;
        av_packet_free(&packet);
        return;
    }

    // 分配重采样输出缓冲区
    uint8_t **resampledData = nullptr;
    int resampledLinesize;
    int resampledBufferSize = 0;

    std::cout << "音频解码线程: 开始" << std::endl;

    // 创建直接PCM输出文件（如果需要）
    FILE *directPcmFile = nullptr;
    if (!directPcmOutput.empty())
    {
        directPcmFile = fopen(directPcmOutput.c_str(), "wb");
        if (!directPcmFile)
        {
            std::cerr << "音频解码线程: 无法打开直接PCM输出文件: " << directPcmOutput << std::endl;
        }
        else
        {
            std::cout << "音频解码线程: 已打开直接PCM输出文件: " << directPcmOutput << std::endl;
        }
    }

    // 调试计数器
    int packetCount = 0;
    int frameDecoded = 0;
    int emptyPacketCount = 0;
    auto startTime = std::chrono::high_resolution_clock::now();
    bool receivedEOF = false;

    // 主循环
    while (isRunning)
    {
        // 处理暂停
        if (isPaused)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        // 从队列中获取数据包
        void *packetData = nullptr;
        if (!packetQueue.tryPop(packetData))
        {
            // 队列为空，等待一段时间
            emptyPacketCount++;
            if (emptyPacketCount % 20 == 0)
            {
                std::cout << "音频解码线程: 队列持续为空 " << emptyPacketCount / 10 << " 秒" << std::endl;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        // 重置空队列计数
        emptyPacketCount = 0;

        // 转换为AVPacket
        AVPacket *pkt = static_cast<AVPacket *>(packetData);
        packetCount++;

        // 检查是否为EOF标志包
        if (pkt->data == NULL && pkt->size == 0 && (pkt->flags & 0x100))
        {
            std::cout << "音频解码线程: 收到EOF标记包，执行最终解码刷新" << std::endl;
            receivedEOF = true;

            // 发送一个空包，告诉解码器刷新缓冲帧
            avcodec_send_packet(codecContext, NULL);

            // 继续接收所有缓冲的帧
            int ret = 0;
            while (ret >= 0)
            {
                ret = avcodec_receive_frame(codecContext, frame);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
                {
                    break;
                }
                else if (ret < 0)
                {
                    std::cerr << "音频解码线程: 刷新时接收帧失败" << std::endl;
                    break;
                }

                frameDecoded++;

                // 计算重采样后的样本数
                int outSamples = av_rescale_rnd(
                    swr_get_delay(swrContext, codecContext->sample_rate) + frame->nb_samples,
                    44100,
                    codecContext->sample_rate,
                    AV_ROUND_UP);

                // 确保重采样缓冲区足够大
                if (outSamples > resampledBufferSize)
                {
                    if (resampledData)
                    {
                        av_freep(&resampledData[0]);
                    }
                    av_freep(&resampledData);

                    // 分配新的缓冲区
                    int ret = av_samples_alloc_array_and_samples(
                        &resampledData,
                        &resampledLinesize,
                        2, // 立体声
                        outSamples,
                        AV_SAMPLE_FMT_S16,
                        0);

                    if (ret < 0)
                    {
                        std::cerr << "音频解码线程: 无法分配重采样缓冲区" << std::endl;
                        break;
                    }

                    resampledBufferSize = outSamples;
                }

                // 执行重采样
                int samplesOut = swr_convert(
                    swrContext,
                    resampledData,
                    outSamples,
                    (const uint8_t **)frame->data,
                    frame->nb_samples);

                if (samplesOut < 0)
                {
                    std::cerr << "音频解码线程: 重采样失败" << std::endl;
                    break;
                }

                // 计算输出数据大小
                int dataSize = samplesOut * 2 * 2; // 2通道，2字节每样本

                // 保存到PCM文件
                if (saveToPCM)
                {
                    savePCMData(resampledData[0], dataSize);
                }

                // 保存到直接PCM输出文件
                if (directPcmFile)
                {
                    writePCMToFile(resampledData[0], dataSize, directPcmFile);
                }

                // 调用回调函数
                if (frameCallback)
                {
                    frameCallback(resampledData[0], dataSize, 44100, 2);
                }

                // 处理音频帧，将其添加到缓冲区
                processAudioSamples(resampledData[0], samplesOut, frame->pts);
            }

            // 释放数据包
            av_packet_free(&pkt);

            std::cout << "音频解码线程: 刷新完成，准备退出" << std::endl;
            break; // 文件结束，退出解码循环
        }

        // 每处理100个包打印一次进度
        if (packetCount % 100 == 0)
        {
            auto now = std::chrono::high_resolution_clock::now();
            double elapsedSeconds = std::chrono::duration<double>(now - startTime).count();

            std::cout << "音频解码线程: 已处理 " << packetCount << " 个包，解码 "
                      << frameDecoded << " 帧" << std::endl;
        }

        // 发送数据包到解码器
        int ret = avcodec_send_packet(codecContext, pkt);

        // 释放数据包
        av_packet_free(&pkt);

        if (ret < 0)
        {
            char errBuff[AV_ERROR_MAX_STRING_SIZE] = {0};
            av_strerror(ret, errBuff, AV_ERROR_MAX_STRING_SIZE);
            std::cerr << "音频解码线程: 发送数据包到解码器失败 (" << errBuff << ")" << std::endl;
            continue;
        }

        // 接收解码后的帧
        bool frameReceived = false;
        while (ret >= 0)
        {
            ret = avcodec_receive_frame(codecContext, frame);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            {
                // 需要更多数据包或者到达文件末尾
                break;
            }
            else if (ret < 0)
            {
                char errBuff[AV_ERROR_MAX_STRING_SIZE] = {0};
                av_strerror(ret, errBuff, AV_ERROR_MAX_STRING_SIZE);
                std::cerr << "音频解码线程: 接收帧失败 (" << errBuff << ")" << std::endl;
                break;
            }

            frameReceived = true;
            frameDecoded++;

            // 计算重采样后的样本数
            int outSamples = av_rescale_rnd(
                swr_get_delay(swrContext, codecContext->sample_rate) + frame->nb_samples,
                44100,
                codecContext->sample_rate,
                AV_ROUND_UP);

            // 确保重采样缓冲区足够大
            if (outSamples > resampledBufferSize)
            {
                if (resampledData)
                {
                    av_freep(&resampledData[0]);
                }
                av_freep(&resampledData);

                // 分配新的缓冲区
                int ret = av_samples_alloc_array_and_samples(
                    &resampledData,
                    &resampledLinesize,
                    2, // 立体声
                    outSamples,
                    AV_SAMPLE_FMT_S16,
                    0);

                if (ret < 0)
                {
                    std::cerr << "音频解码线程: 无法分配重采样缓冲区" << std::endl;
                    break;
                }

                resampledBufferSize = outSamples;
            }

            // 执行重采样
            int samplesOut = swr_convert(
                swrContext,
                resampledData,
                outSamples,
                (const uint8_t **)frame->data,
                frame->nb_samples);

            if (samplesOut < 0)
            {
                std::cerr << "音频解码线程: 重采样失败" << std::endl;
                break;
            }

            // 计算输出数据大小
            int dataSize = samplesOut * 2 * 2; // 2通道，2字节每样本

            // 保存到PCM文件
            if (saveToPCM)
            {
                savePCMData(resampledData[0], dataSize);
            }

            // 保存到直接PCM输出文件
            if (directPcmFile)
            {
                writePCMToFile(resampledData[0], dataSize, directPcmFile);
            }

            // 调用回调函数
            if (frameCallback)
            {
                frameCallback(resampledData[0], dataSize, 44100, 2);
            }

            // 处理音频帧，将其添加到缓冲区
            processAudioSamples(resampledData[0], samplesOut, frame->pts);
        }

        // 如果没有收到帧但解码了很多包，可能是解码过程有问题
        if (!frameReceived && packetCount % 300 == 0 && packetCount > 0)
        {
            std::cout << "音频解码线程: 警告 - 已处理 " << packetCount
                      << " 个包但最近没有解码出新帧" << std::endl;
        }
    }

    // 关闭直接PCM输出文件
    if (directPcmFile)
    {
        fclose(directPcmFile);
        std::cout << "音频解码线程: 已关闭直接PCM输出文件: " << directPcmOutput << std::endl;
    }

    // 释放重采样缓冲区
    if (resampledData)
    {
        av_freep(&resampledData[0]);
        av_freep(&resampledData);
    }

    // 清理
    av_frame_free(&frame);
    av_packet_free(&packet);

    auto endTime = std::chrono::high_resolution_clock::now();
    double totalSeconds = std::chrono::duration<double>(endTime - startTime).count();
    std::cout << "音频解码线程: 结束，总共解码 " << frameDecoded << " 帧，耗时 "
              << totalSeconds << " 秒";

    if (receivedEOF)
    {
        std::cout << "，正常收到EOF标记";
    }

    std::cout << std::endl;
}

// 设置直接PCM输出文件路径
bool AudioDecoder::setDirectPCMOutput(const std::string &filePath)
{
    if (isRunning)
    {
        std::cerr << "音频解码器: 不能在解码线程运行时设置PCM输出" << std::endl;
        return false;
    }

    directPcmOutput = filePath;
    std::cout << "音频解码器: 已设置直接PCM输出文件: " << directPcmOutput << std::endl;
    return true;
}

// 直接写入PCM数据到文件
void AudioDecoder::writePCMToFile(const uint8_t *data, int size, FILE *file)
{
    if (!file || !data || size <= 0)
    {
        return;
    }

    fwrite(data, 1, size, file);
    fflush(file);
}

// 获取解码后的帧
AVFrame *AudioDecoder::getFrame()
{
    void *framePtr = nullptr;
    if (decodedFrameQueue.tryPop(framePtr))
    {
        return static_cast<AVFrame *>(framePtr);
    }
    return nullptr;
}

// 处理音频样本，将其添加到缓冲区并创建符合AC3要求的帧
void AudioDecoder::processAudioSamples(const uint8_t *data, int samplesCount, int64_t pts)
{
    // AC3编码器要求每个帧的样本数为1536
    static const int AC3_FRAME_SIZE = 1536;
    static std::vector<float> leftChannel;
    static std::vector<float> rightChannel;
    static int64_t lastPts = 0;

    if (pts > 0)
    {
        lastPts = pts;
    }

    // 将当前帧的样本添加到缓冲区
    for (int i = 0; i < samplesCount; i++)
    {
        int16_t *src = (int16_t *)data;
        leftChannel.push_back(src[i * 2] / 32768.0f);
        rightChannel.push_back(src[i * 2 + 1] / 32768.0f);
    }

    // 当缓冲区中的样本数达到或超过AC3_FRAME_SIZE时，创建帧并放入队列
    while (leftChannel.size() >= AC3_FRAME_SIZE)
    {
        AVFrame *outputFrame = av_frame_alloc();
        if (outputFrame)
        {
            // 使用FLTP格式，与AC3编码器期望的格式匹配
            outputFrame->format = AV_SAMPLE_FMT_FLTP;
            outputFrame->channel_layout = av_get_default_channel_layout(2); // 立体声
            outputFrame->sample_rate = 44100;
            outputFrame->nb_samples = AC3_FRAME_SIZE;

            int ret = av_frame_get_buffer(outputFrame, 0);
            if (ret >= 0)
            {
                // 复制缓冲区中的样本到输出帧
                float *leftDst = (float *)outputFrame->data[0];
                float *rightDst = (float *)outputFrame->data[1];

                for (int i = 0; i < AC3_FRAME_SIZE; i++)
                {
                    leftDst[i] = leftChannel[i];
                    rightDst[i] = rightChannel[i];
                }

                // 从缓冲区中移除已使用的样本
                leftChannel.erase(leftChannel.begin(), leftChannel.begin() + AC3_FRAME_SIZE);
                rightChannel.erase(rightChannel.begin(), rightChannel.begin() + AC3_FRAME_SIZE);

                // 设置帧的时间戳
                // 这里简化处理，实际应该根据样本数和采样率计算正确的时间戳
                static int64_t framePts = 0;
                outputFrame->pts = framePts;
                framePts += AC3_FRAME_SIZE;

                // 放入队列
                decodedFrameQueue.push(outputFrame);
            }
            else
            {
                av_frame_free(&outputFrame);
                std::cerr << "音频解码线程: 无法为输出帧分配缓冲区" << std::endl;
            }
        }
    }
}
