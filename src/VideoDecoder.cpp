#include "../include/VideoDecoder.h"
#include <iostream>

// 引入FFmpeg头文件
extern "C"
{
#include "ffmpeg/include_ffmpeg/libavcodec/avcodec.h"
#include "ffmpeg/include_ffmpeg/libavutil/imgutils.h"
#include "ffmpeg/include_ffmpeg/libavutil/time.h"
#include "ffmpeg/include_ffmpeg/libavutil/error.h"
#include "ffmpeg/include_ffmpeg/libavutil/frame.h"
#include "ffmpeg/include_ffmpeg/libswscale/swscale.h"
}

// 构造函数
VideoDecoder::VideoDecoder(VideoPacketQueue &packetQueue, VideoFrameQueue &decodedFrameQueue)
    : codecContext(nullptr),
      codec(nullptr),
      packetQueue(packetQueue),
      decodedFrameQueue(decodedFrameQueue),
      isRunning(false),
      isPaused(false),
      frameCallback(nullptr),
      saveToFile(false)
{
    std::cout << "视频解码器: 创建实例" << std::endl;
}

// 析构函数
VideoDecoder::~VideoDecoder()
{
    std::cout << "视频解码器: 销毁实例" << std::endl;
    stop();
    closeYUVOutput();
    closeDecoder();
}

// 初始化解码器
bool VideoDecoder::init(AVCodecParameters *codecPar)
{
    if (!codecPar)
    {
        std::cerr << "视频解码器: 无效的编解码器参数" << std::endl;
        return false;
    }

    std::cout << "视频解码器: 开始初始化" << std::endl;
    return initDecoder(codecPar);
}

// 内部初始化解码器方法
bool VideoDecoder::initDecoder(AVCodecParameters *codecPar)
{
    // 查找解码器
    codec = avcodec_find_decoder(codecPar->codec_id);
    if (!codec)
    {
        std::cerr << "视频解码器: 找不到解码器" << std::endl;
        return false;
    }

    std::cout << "视频解码器: 找到解码器 " << codec->name << std::endl;

    // 分配解码器上下文
    codecContext = avcodec_alloc_context3(codec);
    if (!codecContext)
    {
        std::cerr << "视频解码器: 无法分配解码器上下文" << std::endl;
        return false;
    }

    std::cout << "视频解码器: 已分配解码器上下文" << std::endl;

    // 复制编解码器参数到上下文
    if (avcodec_parameters_to_context(codecContext, codecPar) < 0)
    {
        std::cerr << "视频解码器: 无法复制编解码器参数" << std::endl;
        closeDecoder();
        return false;
    }

    std::cout << "视频解码器: 已复制编解码器参数到上下文" << std::endl;

    // 打开解码器
    if (avcodec_open2(codecContext, codec, nullptr) < 0)
    {
        std::cerr << "视频解码器: 无法打开解码器" << std::endl;
        closeDecoder();
        return false;
    }

    std::cout << "视频解码器: 初始化成功" << std::endl;
    std::cout << "  解码器: " << codec->name << std::endl;
    std::cout << "  分辨率: " << codecContext->width << "x" << codecContext->height << std::endl;
    std::cout << "  像素格式: " << av_get_pix_fmt_name(codecContext->pix_fmt) << std::endl;
    if (codecContext->framerate.num != 0 && codecContext->framerate.den != 0)
    {
        std::cout << "  帧率: " << codecContext->framerate.num / codecContext->framerate.den << " fps" << std::endl;
    }
    std::cout << "  比特率: " << codecContext->bit_rate / 1000 << " kbps" << std::endl;

    return true;
}

// 关闭解码器
void VideoDecoder::closeDecoder()
{
    if (codecContext)
    {
        std::cout << "视频解码器: 关闭解码器上下文" << std::endl;
        avcodec_free_context(&codecContext);
        codecContext = nullptr;
    }
    codec = nullptr;
}

// 启动解码线程
void VideoDecoder::start()
{
    if (isRunning || !codecContext)
    {
        std::cout << "视频解码器: 已经在运行或未初始化，无法启动" << std::endl;
        return;
    }

    isRunning = true;
    isPaused = false;

    std::cout << "视频解码器: 启动解码线程" << std::endl;
    // 创建解码线程
    decodeThread = std::thread(&VideoDecoder::decodeThreadFunc, this);
}

// 停止解码线程
void VideoDecoder::stop()
{
    if (!isRunning)
    {
        std::cout << "视频解码器: 未运行，无需停止" << std::endl;
        return;
    }

    std::cout << "视频解码器: 停止解码线程" << std::endl;
    isRunning = false;

    // 等待线程结束
    if (decodeThread.joinable())
    {
        std::cout << "视频解码器: 等待解码线程结束" << std::endl;
        decodeThread.join();
    }
    std::cout << "视频解码器: 解码线程已停止" << std::endl;
}

// 暂停/继续解码
void VideoDecoder::pause(bool pause)
{
    isPaused = pause;
    std::cout << "视频解码器: " << (pause ? "暂停" : "继续") << std::endl;
}

// 设置帧回调
void VideoDecoder::setFrameCallback(VideoFrameCallback callback)
{
    frameCallback = callback;
}

// 设置YUV文件输出
bool VideoDecoder::setYUVOutput(const std::string &filePath)
{
    // 关闭之前的文件（如果有）
    closeYUVOutput();

    // 保存文件路径
    yuvFilePath = filePath;

    // 打开文件
    yuvFile.open(yuvFilePath, std::ios::binary);
    if (!yuvFile.is_open())
    {
        std::cerr << "视频解码器: 无法打开YUV输出文件: " << yuvFilePath << std::endl;
        return false;
    }

    saveToFile = true;
    std::cout << "视频解码器: YUV输出文件已设置: " << yuvFilePath << std::endl;
    return true;
}

// 关闭YUV文件输出
void VideoDecoder::closeYUVOutput()
{
    if (yuvFile.is_open())
    {
        yuvFile.close();
    }
    saveToFile = false;
}

// 保存帧到YUV文件
void VideoDecoder::saveFrameToYUV(AVFrame *frame)
{
    if (!saveToFile || !yuvFile.is_open() || !frame)
    {
        return;
    }

    // 确保帧格式为YUV420P
    if (frame->format != AV_PIX_FMT_YUV420P)
    {
        std::cerr << "视频解码器: 不支持的帧格式，只支持YUV420P" << std::endl;
        return;
    }

    // 写入Y平面 - 逐行写入，避免linesize问题
    for (int y = 0; y < frame->height; y++)
    {
        yuvFile.write(reinterpret_cast<const char *>(frame->data[0] + y * frame->linesize[0]),
                      frame->width);
    }

    // 写入U平面 - 逐行写入
    for (int y = 0; y < frame->height / 2; y++)
    {
        yuvFile.write(reinterpret_cast<const char *>(frame->data[1] + y * frame->linesize[1]),
                      frame->width / 2);
    }

    // 写入V平面 - 逐行写入
    for (int y = 0; y < frame->height / 2; y++)
    {
        yuvFile.write(reinterpret_cast<const char *>(frame->data[2] + y * frame->linesize[2]),
                      frame->width / 2);
    }

    // 刷新文件缓冲区
    yuvFile.flush();
}

// 获取视频宽度
int VideoDecoder::getWidth() const
{
    return codecContext ? codecContext->width : 0;
}

// 获取视频高度
int VideoDecoder::getHeight() const
{
    return codecContext ? codecContext->height : 0;
}

// 获取帧率
double VideoDecoder::getFrameRate() const
{
    if (!codecContext)
    {
        std::cout << "视频解码器: 获取帧率失败，编解码器上下文为空，返回默认值25" << std::endl;
        return 25.0; // 返回默认帧率
    }

    if (codecContext->framerate.num != 0 && codecContext->framerate.den != 0)
    {
        double fps = (double)codecContext->framerate.num / (double)codecContext->framerate.den;
        std::cout << "视频解码器: 获取帧率 " << fps << " fps (从framerate)" << std::endl;
        return fps;
    }

    // 尝试从时基和ticks_per_frame获取帧率
    if (codecContext->time_base.num != 0 && codecContext->time_base.den != 0)
    {
        double fps = (double)codecContext->time_base.den /
                     ((double)codecContext->time_base.num * codecContext->ticks_per_frame);
        std::cout << "视频解码器: 获取帧率 " << fps << " fps (从time_base)" << std::endl;
        return fps;
    }

    // 如果都获取不到，使用默认帧率
    std::cout << "视频解码器: 无法获取帧率，返回默认值25" << std::endl;
    return 25.0; // 返回默认帧率
}

// 获取解码器名称
const char *VideoDecoder::getCodecName() const
{
    if (!codec)
    {
        return "unknown";
    }

    return codec->name;
}

// 解码线程函数
void VideoDecoder::decodeThreadFunc()
{
    if (!codecContext)
    {
        std::cerr << "视频解码线程: 解码器上下文为空" << std::endl;
        return;
    }

    // 分配AVPacket
    AVPacket *packet = av_packet_alloc();
    if (!packet)
    {
        std::cerr << "视频解码线程: 无法分配AVPacket" << std::endl;
        return;
    }

    // 分配AVFrame
    AVFrame *frame = av_frame_alloc();
    if (!frame)
    {
        std::cerr << "视频解码线程: 无法分配AVFrame" << std::endl;
        av_packet_free(&packet);
        return;
    }

    std::cout << "视频解码线程: 开始" << std::endl;

    // 调试计数器
    int packetCount = 0;
    int frameDecoded = 0;
    int emptyPacketCount = 0;
    int queuedFrameCount = 0;
    auto startTime = std::chrono::high_resolution_clock::now();
    bool receivedEOF = false;

    // 创建直接YUV输出文件
    FILE *directYuvFile = nullptr;
    if (!directYuvOutput.empty())
    {
        directYuvFile = fopen(directYuvOutput.c_str(), "wb");
        if (!directYuvFile)
        {
            std::cerr << "视频解码线程: 无法打开直接YUV输出文件: " << directYuvOutput << std::endl;
        }
        else
        {
            std::cout << "视频解码线程: 已打开直接YUV输出文件: " << directYuvOutput << std::endl;
        }
    }

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
                std::cout << "视频解码线程: 队列持续为空 " << emptyPacketCount / 10 << " 秒" << std::endl;
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
            std::cout << "视频解码线程: 收到EOF标记包，执行最终解码刷新" << std::endl;
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
                    std::cerr << "视频解码线程: 刷新时接收帧失败" << std::endl;
                    break;
                }

                frameDecoded++;

                // 保存帧到YUV文件
                if (saveToFile)
                {
                    saveFrameToYUV(frame);
                }

                // 保存到直接YUV输出文件
                if (directYuvFile)
                {
                    writeFrameToYUVFile(frame, directYuvFile);
                }

                // 将解码后的帧放入帧缓冲队列
                AVFrame *frameCopy = av_frame_alloc();
                if (frameCopy)
                {
                    av_frame_ref(frameCopy, frame);
                    decodedFrameQueue.push(frameCopy);
                    queuedFrameCount++;
                    std::cout << "视频解码线程: 将解码帧 #" << queuedFrameCount << " 放入队列 (刷新阶段)" << std::endl;
                }

                // 处理解码后的帧
                if (frameCallback)
                {
                    // 调用回调函数处理帧
                    frameCallback(frame);
                }

                // 重置帧以便重用
                av_frame_unref(frame);
            }

            // 释放数据包
            av_packet_free(&pkt);

            // 向帧队列发送EOF标记
            AVFrame *eofFrame = av_frame_alloc();
            if (eofFrame)
            {
                eofFrame->data[0] = nullptr;
                eofFrame->pts = AV_NOPTS_VALUE;
                eofFrame->pkt_dts = AV_NOPTS_VALUE;
                eofFrame->pkt_duration = 0;
                eofFrame->width = 0;
                eofFrame->height = 0;
                eofFrame->key_frame = 0;
                eofFrame->pict_type = AV_PICTURE_TYPE_NONE;
                eofFrame->format = -1;
                decodedFrameQueue.push(eofFrame);
                std::cout << "视频解码线程: 已向帧队列发送EOF标记" << std::endl;
            }

            std::cout << "视频解码线程: 刷新完成，准备退出" << std::endl;
            break; // 文件结束，退出解码循环
        }

        // 每处理100个包打印一次进度
        if (packetCount % 100 == 0)
        {
            auto now = std::chrono::high_resolution_clock::now();
            double elapsedSeconds = std::chrono::duration<double>(now - startTime).count();
            double fps = (frameDecoded > 0 && elapsedSeconds > 0) ? frameDecoded / elapsedSeconds : 0;

            std::cout << "视频解码线程: 已处理 " << packetCount << " 个包，解码 "
                      << frameDecoded << " 帧，解码速度: " << fps << " fps" << std::endl;
            std::cout << "视频解码线程: 已将 " << queuedFrameCount << " 帧放入队列" << std::endl;
        }

        // 发送数据包到解码器
        int ret = avcodec_send_packet(codecContext, pkt);

        // 释放数据包
        av_packet_free(&pkt);

        if (ret < 0)
        {
            char errBuff[AV_ERROR_MAX_STRING_SIZE] = {0};
            av_strerror(ret, errBuff, AV_ERROR_MAX_STRING_SIZE);
            std::cerr << "视频解码线程: 发送数据包到解码器失败 (" << errBuff << ")" << std::endl;
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
                std::cerr << "视频解码线程: 接收帧失败 (" << errBuff << ")" << std::endl;
                break;
            }

            frameReceived = true;
            frameDecoded++;

            // 保存帧到YUV文件
            if (saveToFile)
            {
                saveFrameToYUV(frame);
            }

            // 保存到直接YUV输出文件
            if (directYuvFile)
            {
                writeFrameToYUVFile(frame, directYuvFile);
            }

            // 将解码后的帧放入帧缓冲队列
            AVFrame *frameCopy = av_frame_alloc();
            if (frameCopy)
            {
                av_frame_ref(frameCopy, frame);
                decodedFrameQueue.push(frameCopy);
                queuedFrameCount++;

                // 每10帧打印一次
                if (queuedFrameCount % 10 == 0)
                {
                    std::cout << "视频解码线程: 将解码帧 #" << queuedFrameCount << " 放入队列" << std::endl;
                }
            }

            // 处理解码后的帧
            if (frameCallback)
            {
                // 调用回调函数处理帧
                frameCallback(frame);
            }

            // 重置帧以便重用
            av_frame_unref(frame);
        }

        // 如果没有收到帧但解码了很多包，可能是解码过程有问题
        if (!frameReceived && packetCount % 300 == 0 && packetCount > 0)
        {
            std::cout << "视频解码线程: 警告 - 已处理 " << packetCount
                      << " 个包但最近没有解码出新帧" << std::endl;
        }
    }

    // 关闭直接YUV输出文件
    if (directYuvFile)
    {
        fclose(directYuvFile);
        std::cout << "视频解码线程: 已关闭直接YUV输出文件: " << directYuvOutput << std::endl;
    }

    // 清理
    av_frame_free(&frame);
    av_packet_free(&packet);

    auto endTime = std::chrono::high_resolution_clock::now();
    double totalSeconds = std::chrono::duration<double>(endTime - startTime).count();
    std::cout << "视频解码线程: 结束，总共解码 " << frameDecoded << " 帧，耗时 "
              << totalSeconds << " 秒";

    if (totalSeconds > 0)
    {
        std::cout << "，平均解码速度: " << frameDecoded / totalSeconds << " fps";
    }

    std::cout << "，总共将 " << queuedFrameCount << " 帧放入队列";

    if (receivedEOF)
    {
        std::cout << "，正常收到EOF标记";
    }

    std::cout << std::endl;
}

// 向文件直接写入YUV数据
void VideoDecoder::writeFrameToYUVFile(AVFrame *frame, FILE *file)
{
    if (!file || !frame)
    {
        return;
    }

    // 确保帧格式为YUV420P
    if (frame->format != AV_PIX_FMT_YUV420P)
    {
        std::cerr << "视频解码器: 不支持的帧格式，只支持YUV420P" << std::endl;
        return;
    }

    // 写入Y平面
    for (int y = 0; y < frame->height; y++)
    {
        fwrite(frame->data[0] + y * frame->linesize[0], 1, frame->width, file);
    }

    // 写入U平面
    for (int y = 0; y < frame->height / 2; y++)
    {
        fwrite(frame->data[1] + y * frame->linesize[1], 1, frame->width / 2, file);
    }

    // 写入V平面
    for (int y = 0; y < frame->height / 2; y++)
    {
        fwrite(frame->data[2] + y * frame->linesize[2], 1, frame->width / 2, file);
    }

    // 刷新文件
    fflush(file);
}

// 设置直接YUV输出文件路径
bool VideoDecoder::setDirectYUVOutput(const std::string &filePath)
{
    if (isRunning)
    {
        std::cerr << "视频解码器: 不能在解码线程运行时设置YUV输出" << std::endl;
        return false;
    }

    directYuvOutput = filePath;
    std::cout << "视频解码器: 已设置直接YUV输出文件: " << directYuvOutput << std::endl;
    return true;
}

// 获取解码后的帧
AVFrame *VideoDecoder::getFrame()
{
    // 分配一个新的AVFrame
    AVFrame *frame = av_frame_alloc();
    if (!frame)
    {
        std::cerr << "视频解码器: 无法分配AVFrame" << std::endl;
        return nullptr;
    }

    // 尝试从解码器获取一帧
    int ret = avcodec_receive_frame(codecContext, frame);
    if (ret < 0)
    {
        // 如果需要更多数据或到达文件末尾，释放帧并返回nullptr
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
        {
            av_frame_free(&frame);
            return nullptr;
        }

        // 其他错误
        char errBuff[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_strerror(ret, errBuff, AV_ERROR_MAX_STRING_SIZE);
        std::cerr << "视频解码器: 接收帧失败 (" << errBuff << ")" << std::endl;
        av_frame_free(&frame);
        return nullptr;
    }

    return frame;
}

// 检查队列是否为空
bool VideoDecoder::isQueueEmpty() const
{
    return packetQueue.isEmpty();
}
