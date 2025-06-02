#include "../include/VideoEncoder.h"
#include <iostream>

// 引入FFmpeg头文件
extern "C"
{
#include "ffmpeg/include_ffmpeg/libavcodec/avcodec.h"
#include "ffmpeg/include_ffmpeg/libavutil/opt.h"
#include "ffmpeg/include_ffmpeg/libavutil/imgutils.h"
#include "ffmpeg/include_ffmpeg/libavutil/frame.h"
#include "ffmpeg/include_ffmpeg/libavutil/error.h"
#include "ffmpeg/include_ffmpeg/libavutil/mathematics.h"
}

// 构造函数
VideoEncoder::VideoEncoder(VideoFrameQueue &frameQueue, VideoPacketQueue &packetQueue)
    : codecContext(nullptr),
      codec(nullptr),
      frameQueue(frameQueue),
      packetQueue(packetQueue),
      isRunning(false),
      isPaused(false),
      frameCount(0),
      encodeCallback(nullptr),
      width(0),
      height(0),
      frameRate(0),
      bitRate(0),
      codecName(""),
      useFilter(false),
      videoFilter(nullptr)
{
    std::cout << "视频编码器: 创建实例" << std::endl;
}

// 析构函数
VideoEncoder::~VideoEncoder()
{
    std::cout << "视频编码器: 销毁实例" << std::endl;
    stop();
    closeEncoder();
}

// 初始化编码器
bool VideoEncoder::init(int width, int height, int frameRate, int bitRate, const std::string &codecName)
{
    // 保存参数
    this->width = width;
    this->height = height;
    this->frameRate = frameRate;
    this->bitRate = bitRate;
    this->codecName = codecName;

    std::cout << "视频编码器: 开始初始化 " << width << "x" << height << " @ " << frameRate << "fps, " << bitRate / 1000 << "kbps, 编码器: " << codecName << std::endl;

    // 初始化编码器
    return initEncoder();
}

// 内部初始化编码器方法
bool VideoEncoder::initEncoder()
{
    // 打印FFmpeg版本信息
    std::cout << "视频编码器: FFmpeg版本信息:" << std::endl;
    std::cout << "  libavcodec: " << LIBAVCODEC_VERSION_MAJOR << "." << LIBAVCODEC_VERSION_MINOR << "." << LIBAVCODEC_VERSION_MICRO << std::endl;
    std::cout << "  libavutil: " << LIBAVUTIL_VERSION_MAJOR << "." << LIBAVUTIL_VERSION_MINOR << "." << LIBAVUTIL_VERSION_MICRO << std::endl;

    // 检查参数有效性
    if (width <= 0 || height <= 0 || frameRate <= 0 || bitRate <= 0)
    {
        std::cerr << "视频编码器: 无效的参数 - 宽度: " << width
                  << ", 高度: " << height
                  << ", 帧率: " << frameRate
                  << ", 比特率: " << bitRate << std::endl;
        return false;
    }

    std::cout << "视频编码器: 尝试初始化 - 宽度: " << width
              << ", 高度: " << height
              << ", 帧率: " << frameRate
              << ", 比特率: " << bitRate
              << ", 编码器: " << codecName << std::endl;

    // 查找编码器
    codec = avcodec_find_encoder_by_name(codecName.c_str());
    if (!codec)
    {
        std::cerr << "视频编码器: 找不到编码器 " << codecName << std::endl;

        // 尝试使用编码器ID
        std::cout << "视频编码器: 尝试使用编码器ID查找编码器..." << std::endl;
        if (codecName == "mpeg4")
        {
            codec = avcodec_find_encoder(AV_CODEC_ID_MPEG4);
        }
        else if (codecName == "libx264" || codecName == "h264")
        {
            codec = avcodec_find_encoder(AV_CODEC_ID_H264);
        }
        else if (codecName == "h265" || codecName == "hevc")
        {
            codec = avcodec_find_encoder(AV_CODEC_ID_HEVC);
        }
        else
        {
            // 默认尝试MPEG4
            codec = avcodec_find_encoder(AV_CODEC_ID_MPEG4);
            codecName = "mpeg4";
        }

        if (!codec)
        {
            std::cerr << "视频编码器: 通过ID也找不到编码器，尝试使用最基本的MPEG1编码器" << std::endl;
            codec = avcodec_find_encoder(AV_CODEC_ID_MPEG1VIDEO);
            if (codec)
            {
                codecName = "mpeg1video";
            }
            else
            {
                std::cerr << "视频编码器: 无法找到任何可用的编码器" << std::endl;
                return false;
            }
        }
    }

    std::cout << "视频编码器: 找到编码器 " << codec->name << std::endl;

    // 分配编码器上下文
    codecContext = avcodec_alloc_context3(codec);
    if (!codecContext)
    {
        std::cerr << "视频编码器: 无法分配编码器上下文" << std::endl;
        return false;
    }

    std::cout << "视频编码器: 已分配编码器上下文" << std::endl;

    // 设置编码参数
    codecContext->width = width;
    codecContext->height = height;

    // 确保帧率有效
    int num = frameRate;
    int den = 1;

    // 对于一些常见的帧率，使用更精确的分数表示
    if (fabs(frameRate - 23.976) < 0.01)
    {
        num = 24000;
        den = 1001;
    }
    else if (fabs(frameRate - 29.97) < 0.01)
    {
        num = 30000;
        den = 1001;
    }
    else if (fabs(frameRate - 59.94) < 0.01)
    {
        num = 60000;
        den = 1001;
    }

    codecContext->time_base = AVRational{den, num};
    codecContext->framerate = AVRational{num, den};

    std::cout << "视频编码器: 设置帧率 " << num << "/" << den
              << " = " << (double)num / den << " fps" << std::endl;
    std::cout << "视频编码器: 设置时基 " << den << "/" << num
              << " = " << (double)den / num << " 秒" << std::endl;

    // 使用最基本的编码器设置
    codecContext->pix_fmt = AV_PIX_FMT_YUV420P; // 最常用的像素格式
    codecContext->bit_rate = bitRate;
    codecContext->gop_size = 10;    // 较小的GOP大小
    codecContext->max_b_frames = 0; // 不使用B帧
    codecContext->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    std::cout << "视频编码器: 已设置基本编码参数" << std::endl;

    // 对于H.264编码器的特殊设置
    if (codecName == "libx264")
    {
        // 设置更兼容的参数
        av_opt_set(codecContext->priv_data, "preset", "medium", 0);
        av_opt_set(codecContext->priv_data, "tune", "film", 0);
        av_opt_set(codecContext->priv_data, "profile", "main", 0); // 使用main profile提高兼容性
        av_opt_set(codecContext->priv_data, "level", "3.1", 0);    // 降低level提高兼容性
        av_opt_set(codecContext->priv_data, "crf", "23", 0);       // 设置恒定质量因子
        std::cout << "视频编码器: 已设置H.264特殊参数 (preset=medium, tune=film, profile=main, level=3.1, crf=23)" << std::endl;
    }
    else if (codecName == "h264_nvenc")
    {
        // NVIDIA GPU加速编码器的特殊设置
        av_opt_set(codecContext->priv_data, "preset", "medium", 0);
        av_opt_set(codecContext->priv_data, "profile", "main", 0);
        av_opt_set(codecContext->priv_data, "level", "3.1", 0);
        av_opt_set(codecContext->priv_data, "rc", "vbr", 0); // 可变比特率
        av_opt_set(codecContext->priv_data, "cq", "23", 0);  // 质量参数
        std::cout << "视频编码器: 已设置H.264 NVENC特殊参数 (preset=medium, profile=main, level=3.1, rc=vbr, cq=23)" << std::endl;
    }

    // 打开编码器
    std::cout << "视频编码器: 尝试打开编码器 " << codec->name << "..." << std::endl;

    // 打印编码器支持的像素格式
    if (codec->pix_fmts)
    {
        std::cout << "视频编码器: 支持的像素格式: ";
        for (int i = 0; codec->pix_fmts[i] != AV_PIX_FMT_NONE; i++)
        {
            std::cout << av_get_pix_fmt_name(codec->pix_fmts[i]) << " ";
        }
        std::cout << std::endl;

        // 确保使用支持的像素格式
        bool formatSupported = false;
        for (int i = 0; codec->pix_fmts[i] != AV_PIX_FMT_NONE; i++)
        {
            if (codec->pix_fmts[i] == codecContext->pix_fmt)
            {
                formatSupported = true;
                break;
            }
        }

        if (!formatSupported && codec->pix_fmts[0] != AV_PIX_FMT_NONE)
        {
            std::cout << "视频编码器: 当前像素格式 " << av_get_pix_fmt_name(codecContext->pix_fmt)
                      << " 不被支持，切换到 " << av_get_pix_fmt_name(codec->pix_fmts[0]) << std::endl;
            codecContext->pix_fmt = codec->pix_fmts[0];
        }
    }

    // 尝试打开编码器
    int ret = avcodec_open2(codecContext, codec, nullptr);
    if (ret < 0)
    {
        char errBuff[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_strerror(ret, errBuff, AV_ERROR_MAX_STRING_SIZE);
        std::cerr << "视频编码器: 无法打开编码器 (" << errBuff << ")" << std::endl;

        // 尝试调整参数后重试
        std::cout << "视频编码器: 尝试调整参数后重试..." << std::endl;

        // 重置一些可能导致问题的参数
        codecContext->bit_rate_tolerance = 0;
        codecContext->rc_min_rate = 0;
        codecContext->rc_max_rate = 0;
        codecContext->rc_buffer_size = 0;

        // 确保像素格式兼容
        if (codec->pix_fmts)
        {
            codecContext->pix_fmt = codec->pix_fmts[0]; // 使用编码器支持的第一个像素格式
            std::cout << "视频编码器: 调整像素格式为 " << av_get_pix_fmt_name(codecContext->pix_fmt) << std::endl;
        }

        // 重试打开编码器
        ret = avcodec_open2(codecContext, codec, nullptr);
        if (ret < 0)
        {
            av_strerror(ret, errBuff, AV_ERROR_MAX_STRING_SIZE);
            std::cerr << "视频编码器: 调整参数后仍然无法打开编码器 (" << errBuff << ")" << std::endl;
            closeEncoder();
            return false;
        }

        std::cout << "视频编码器: 调整参数后成功打开编码器" << std::endl;
    }
    else
    {
        std::cout << "视频编码器: 成功打开编码器" << std::endl;
    }

    std::cout << "视频编码器: 初始化成功" << std::endl;
    std::cout << "  编码器: " << codec->name << std::endl;
    std::cout << "  分辨率: " << width << "x" << height << std::endl;
    std::cout << "  帧率: " << (double)num / den << " fps" << std::endl;
    std::cout << "  比特率: " << bitRate / 1000 << " kbps" << std::endl;
    std::cout << "  像素格式: " << av_get_pix_fmt_name(codecContext->pix_fmt) << std::endl;
    std::cout << "  GOP大小: " << codecContext->gop_size << std::endl;
    std::cout << "  B帧数量: " << codecContext->max_b_frames << std::endl;

    return true;
}

// 关闭编码器
void VideoEncoder::closeEncoder()
{
    if (codecContext)
    {
        std::cout << "视频编码器: 关闭编码器上下文" << std::endl;
        avcodec_free_context(&codecContext);
        codecContext = nullptr;
    }
    codec = nullptr;
}

// 启动编码线程
void VideoEncoder::start()
{
    if (isRunning)
    {
        std::cout << "视频编码器: 已经在运行，无法再次启动" << std::endl;
        return;
    }

    if (!codecContext)
    {
        std::cerr << "视频编码器: 编码器上下文为空，无法启动" << std::endl;
        return;
    }

    isRunning = true;
    isPaused = false;

    std::cout << "视频编码器: 启动编码线程" << std::endl;
    // 创建编码线程
    encodeThread = std::thread(&VideoEncoder::encodeThreadFunc, this);
}

// 停止编码线程
void VideoEncoder::stop()
{
    if (!isRunning)
    {
        std::cout << "视频编码器: 未运行，无需停止" << std::endl;
        return;
    }

    std::cout << "视频编码器: 停止编码线程" << std::endl;
    isRunning = false;

    // 等待线程结束
    if (encodeThread.joinable())
    {
        std::cout << "视频编码器: 等待编码线程结束" << std::endl;
        encodeThread.join();
    }
    std::cout << "视频编码器: 编码线程已停止" << std::endl;
}

// 暂停/继续编码
void VideoEncoder::pause(bool pause)
{
    isPaused = pause;
    std::cout << "视频编码器: " << (pause ? "暂停" : "继续") << std::endl;
}

// 编码单帧
bool VideoEncoder::encode(AVFrame *frame)
{
    if (!codecContext || !frame)
    {
        std::cerr << "视频编码器: 无效的编码器上下文或帧" << std::endl;
        return false;
    }

    std::cout << "视频编码器: 编码单帧 (pts=" << frame->pts << ")" << std::endl;
    // 直接编码帧
    return encodeFrame(frame);
}

// 内部编码帧方法
bool VideoEncoder::encodeFrame(AVFrame *frame)
{
    if (!codecContext)
    {
        std::cerr << "视频编码器: 无效的编码器上下文，无法编码帧" << std::endl;
        return false;
    }

    // 设置帧的时间戳
    if (frame)
    {
        frame->pts = frameCount++;

        // 如果是MPEG4编码器，确保不使用B帧
        if (codecName == "mpeg4")
        {
            // 强制设置为P帧或I帧（每15帧一个I帧）
            if (frameCount % 15 == 0)
            {
                frame->pict_type = AV_PICTURE_TYPE_I;
                frame->key_frame = 1;
                std::cout << "视频编码器: 设置I帧 #" << frameCount << std::endl;
            }
            else
            {
                frame->pict_type = AV_PICTURE_TYPE_P;
                frame->key_frame = 0;
            }
        }
    }
    else
    {
        std::cout << "视频编码器: 发送NULL帧以刷新编码器" << std::endl;
    }

    // 发送帧到编码器
    int ret = avcodec_send_frame(codecContext, frame);
    if (ret < 0)
    {
        char errBuff[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_strerror(ret, errBuff, AV_ERROR_MAX_STRING_SIZE);
        std::cerr << "视频编码器: 发送帧到编码器失败 (" << errBuff << ")" << std::endl;

        // 如果是MPEG4编码器，尝试更改帧格式后重试
        if (codecName == "mpeg4" && frame)
        {
            std::cout << "视频编码器: 尝试调整帧参数后重试..." << std::endl;

            // 强制设置为I帧
            frame->pict_type = AV_PICTURE_TYPE_I;
            frame->key_frame = 1;

            // 重试发送帧
            ret = avcodec_send_frame(codecContext, frame);
            if (ret < 0)
            {
                av_strerror(ret, errBuff, AV_ERROR_MAX_STRING_SIZE);
                std::cerr << "视频编码器: 重试发送帧到编码器仍然失败 (" << errBuff << ")" << std::endl;
                return false;
            }
            else
            {
                std::cout << "视频编码器: 调整帧参数后重试成功" << std::endl;
            }
        }
        else
        {
            return false;
        }
    }

    // 接收编码后的包
    bool packetReceived = false;
    while (ret >= 0)
    {
        // 分配AVPacket
        AVPacket *packet = av_packet_alloc();
        if (!packet)
        {
            std::cerr << "视频编码器: 无法分配AVPacket" << std::endl;
            return false;
        }

        ret = avcodec_receive_packet(codecContext, packet);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
        {
            // 需要更多输入或已到达文件末尾
            av_packet_free(&packet);
            if (ret == AVERROR_EOF)
            {
                std::cout << "视频编码器: 已到达编码器EOF" << std::endl;
            }
            break;
        }
        else if (ret < 0)
        {
            char errBuff[AV_ERROR_MAX_STRING_SIZE] = {0};
            av_strerror(ret, errBuff, AV_ERROR_MAX_STRING_SIZE);
            std::cerr << "视频编码器: 接收包失败 (" << errBuff << ")" << std::endl;
            av_packet_free(&packet);
            return false;
        }

        packetReceived = true;

        // 调用回调函数
        if (encodeCallback)
        {
            try
            {
                encodeCallback(packet);
            }
            catch (const std::exception &e)
            {
                std::cerr << "视频编码器: 回调函数发生异常: " << e.what() << std::endl;
            }
            catch (...)
            {
                std::cerr << "视频编码器: 回调函数发生未知异常" << std::endl;
            }
        }

        // 将包添加到队列
        packetQueue.push(packet);
        std::cout << "视频编码器: 将编码包放入队列 (pts=" << packet->pts << ", dts=" << packet->dts << ", size=" << packet->size << " bytes)" << std::endl;
    }

    return packetReceived;
}

// 发送EOF标记
void VideoEncoder::sendEOF()
{
    if (!codecContext)
    {
        std::cerr << "视频编码器: 无效的编码器上下文，无法发送EOF标记" << std::endl;

        // 即使编码器上下文无效，也创建一个EOF包并添加到队列中，确保复用器能够正确结束
        AVPacket *eofPacket = av_packet_alloc();
        if (eofPacket)
        {
            eofPacket->data = nullptr;
            eofPacket->size = 0;
            eofPacket->flags |= 0x100; // 自定义EOF标志
            packetQueue.push(eofPacket);
            std::cout << "视频编码器: 已发送EOF标记（无编码器上下文）" << std::endl;
        }
        return;
    }

    std::cout << "视频编码器: 发送EOF标记" << std::endl;

    // 尝试发送NULL帧表示结束
    try
    {
        encodeFrame(nullptr);
    }
    catch (const std::exception &e)
    {
        std::cerr << "视频编码器: 发送NULL帧时发生异常: " << e.what() << std::endl;
    }
    catch (...)
    {
        std::cerr << "视频编码器: 发送NULL帧时发生未知异常" << std::endl;
    }

    // 创建一个特殊的EOF包
    AVPacket *eofPacket = av_packet_alloc();
    if (eofPacket)
    {
        eofPacket->data = nullptr;
        eofPacket->size = 0;
        eofPacket->flags |= 0x100; // 自定义EOF标志

        // 将EOF包添加到队列
        packetQueue.push(eofPacket);

        std::cout << "视频编码器: 已发送EOF标记" << std::endl;
    }
}

// 刷新编码器
void VideoEncoder::flush()
{
    if (!codecContext)
    {
        std::cerr << "视频编码器: 无效的编码器上下文，无法刷新编码器" << std::endl;
        // 即使编码器上下文无效，也发送EOF标记
        sendEOF();
        return;
    }

    std::cout << "视频编码器: 刷新编码器" << std::endl;

    // 尝试发送NULL帧表示结束编码
    try
    {
        encodeFrame(nullptr);
    }
    catch (const std::exception &e)
    {
        std::cerr << "视频编码器: 刷新编码器时发生异常: " << e.what() << std::endl;
    }
    catch (...)
    {
        std::cerr << "视频编码器: 刷新编码器时发生未知异常" << std::endl;
    }

    // 发送EOF标记
    sendEOF();
}

// 编码线程函数
void VideoEncoder::encodeThreadFunc()
{
    std::cout << "视频编码线程: 开始" << std::endl;

    // 分配输出帧（用于滤镜处理后的帧）
    AVFrame *filteredFrame = nullptr;
    if (useFilter && videoFilter)
    {
        filteredFrame = av_frame_alloc();
        if (!filteredFrame)
        {
            std::cerr << "视频编码线程: 无法分配滤镜输出帧" << std::endl;
            return;
        }
        std::cout << "视频编码线程: 已分配滤镜输出帧" << std::endl;
    }

    int emptyQueueCount = 0;
    int processedFrames = 0;
    int encodedPackets = 0;
    int filterFailCount = 0;
    bool receivedEOF = false;
    auto startTime = std::chrono::high_resolution_clock::now();

    std::cout << "视频编码线程: " << (useFilter ? "使用" : "不使用") << "滤镜处理" << std::endl;

    // 线程主循环
    while (isRunning && !receivedEOF)
    {
        // 处理暂停
        if (isPaused)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        // 从帧队列中获取解码后的帧
        void *frameData = nullptr;
        if (!frameQueue.tryPop(frameData))
        {
            // 队列为空，等待一段时间
            emptyQueueCount++;
            if (emptyQueueCount % 100 == 0)
            {
                std::cout << "视频编码线程: 帧队列持续为空 " << emptyQueueCount / 100 << " 秒" << std::endl;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        // 重置空队列计数
        emptyQueueCount = 0;

        // 转换为AVFrame
        AVFrame *frame = static_cast<AVFrame *>(frameData);
        if (!frame)
        {
            std::cerr << "视频编码线程: 从队列获取的帧为空" << std::endl;
            continue;
        }

        // 检查是否为EOF标记帧
        if (frame->format == -1 || frame->width == 0 || frame->height == 0 || frame->data[0] == nullptr)
        {
            std::cout << "视频编码线程: 收到EOF标记帧，执行最终编码刷新" << std::endl;
            receivedEOF = true;

            // 释放EOF标记帧
            av_frame_free(&frame);

            // 刷新编码器
            try
            {
                flush();
            }
            catch (const std::exception &e)
            {
                std::cerr << "视频编码线程: 刷新编码器时发生异常: " << e.what() << std::endl;
            }
            catch (...)
            {
                std::cerr << "视频编码线程: 刷新编码器时发生未知异常" << std::endl;
            }
            continue;
        }

        // 处理帧（应用滤镜）
        AVFrame *frameToEncode = frame;
        bool useOriginalFrame = true;

        if (useFilter && videoFilter)
        {
            // 应用视频滤镜
            std::cout << "视频编码线程: 应用滤镜处理帧 #" << processedFrames << std::endl;

            // 确保filteredFrame是干净的
            av_frame_unref(filteredFrame);

            if (videoFilter->processFrame(frame, filteredFrame))
            {
                // 使用滤镜处理后的帧
                frameToEncode = filteredFrame;
                useOriginalFrame = false;
                std::cout << "视频编码线程: 滤镜处理成功" << std::endl;
                filterFailCount = 0;
            }
            else
            {
                filterFailCount++;
                std::cerr << "视频编码线程: 滤镜处理失败 (" << filterFailCount << " 次)，使用原始帧" << std::endl;

                // 如果连续失败次数过多，可能是滤镜配置有问题，禁用滤镜
                if (filterFailCount > 10)
                {
                    std::cerr << "视频编码线程: 滤镜连续失败次数过多，禁用滤镜" << std::endl;
                    useFilter = false;
                }
            }
        }

        // 编码帧
        std::cout << "视频编码线程: 编码帧 #" << processedFrames << std::endl;

        // 检查帧是否有效
        if (frameToEncode && frameToEncode->data[0])
        {
            if (encodeFrame(frameToEncode))
            {
                encodedPackets++;
            }
            else
            {
                std::cerr << "视频编码线程: 编码帧 #" << processedFrames << " 失败" << std::endl;
            }
        }
        else
        {
            std::cerr << "视频编码线程: 帧 #" << processedFrames << " 无效，跳过编码" << std::endl;
        }

        // 每处理100帧打印一次进度
        processedFrames++;
        if (processedFrames % 100 == 0)
        {
            auto now = std::chrono::high_resolution_clock::now();
            double elapsedSeconds = std::chrono::duration<double>(now - startTime).count();
            double fps = (processedFrames > 0 && elapsedSeconds > 0) ? processedFrames / elapsedSeconds : 0;

            std::cout << "视频编码线程: 已处理 " << processedFrames << " 帧，编码 "
                      << encodedPackets << " 个包，编码速度: " << fps << " fps" << std::endl;
        }

        // 释放原始帧
        av_frame_free(&frame);
    }

    // 释放滤镜输出帧
    if (filteredFrame)
    {
        av_frame_free(&filteredFrame);
        std::cout << "视频编码线程: 已释放滤镜输出帧" << std::endl;
    }

    auto endTime = std::chrono::high_resolution_clock::now();
    double totalSeconds = std::chrono::duration<double>(endTime - startTime).count();

    std::cout << "视频编码线程: 结束，总共处理 " << processedFrames << " 帧，编码 "
              << encodedPackets << " 个包，耗时 " << totalSeconds << " 秒";

    if (totalSeconds > 0)
    {
        std::cout << "，平均编码速度: " << processedFrames / totalSeconds << " fps";
    }

    std::cout << std::endl;
}

// 设置视频滤镜
bool VideoEncoder::setVideoFilter(VideoFilter *filter)
{
    if (!filter)
    {
        std::cerr << "视频编码器: 无效的滤镜指针" << std::endl;
        return false;
    }

    videoFilter = filter;
    useFilter = true;
    std::cout << "视频编码器: 已设置视频滤镜" << std::endl;
    return true;
}

// 设置编码回调
void VideoEncoder::setEncodeCallback(VideoEncodeCallback callback)
{
    encodeCallback = callback;
    std::cout << "视频编码器: 已设置编码回调" << std::endl;
}

// 获取视频宽度
int VideoEncoder::getWidth() const
{
    return width;
}

// 获取视频高度
int VideoEncoder::getHeight() const
{
    return height;
}

// 获取帧率
int VideoEncoder::getFrameRate() const
{
    return frameRate;
}

// 获取比特率
int VideoEncoder::getBitRate() const
{
    return bitRate;
}

// 获取编码器名称
const char *VideoEncoder::getCodecName() const
{
    return codec ? codec->name : "unknown";
}

// 获取编码帧数
int VideoEncoder::getFrameCount() const
{
    return frameCount;
}

// 获取编解码器上下文
AVCodecContext *VideoEncoder::getCodecContext() const
{
    return codecContext;
}