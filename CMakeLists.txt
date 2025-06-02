cmake_minimum_required(VERSION 3.10)
project(VideoTranscode VERSION 1.0)

# 设置C++标准
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 设置输出目录
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# 添加包含目录
include_directories(${CMAKE_SOURCE_DIR})

# 设置FFmpeg路径
set(FFMPEG_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/ffmpeg/include_ffmpeg")
set(FFMPEG_LIB_DIR "${CMAKE_SOURCE_DIR}/ffmpeg/lib")

# 添加FFmpeg头文件目录
include_directories(${FFMPEG_INCLUDE_DIR})

# 添加FFmpeg库目录
link_directories(${FFMPEG_LIB_DIR})

# 设置额外的链接库
set(EXTRA_LIBS dl m z pthread lzma va va-drm va-x11 vdpau X11)

# 检查是否存在合并的FFmpeg库
set(FFMPEG_MERGED_LIB "${FFMPEG_LIB_DIR}/libffmpegjxz.so")
if(EXISTS ${FFMPEG_MERGED_LIB})
    message(STATUS "找到合并的FFmpeg库: ${FFMPEG_MERGED_LIB}")
else()
    message(WARNING "未找到合并的FFmpeg库: ${FFMPEG_MERGED_LIB}")
    set(FFMPEG_MERGED_LIB "")
endif()

# 添加系统库作为备用
set(SYS_FFMPEG_LIBS avfilter avformat avcodec avutil swresample swscale)

# 添加队列库
add_library(queue STATIC src/queue.cpp)
target_link_libraries(queue pthread)

# 添加解复用器库
add_library(demux STATIC src/Demux.cpp)
target_include_directories(demux PRIVATE ${FFMPEG_INCLUDE_DIR})
target_link_libraries(demux queue)

# 添加视频解码器库
add_library(video_decoder STATIC src/VideoDecoder.cpp)
target_include_directories(video_decoder PRIVATE ${FFMPEG_INCLUDE_DIR})
target_link_libraries(video_decoder queue)

# 添加音频解码器库
add_library(audio_decoder STATIC src/AudioDecoder.cpp)
target_include_directories(audio_decoder PRIVATE ${FFMPEG_INCLUDE_DIR})
target_link_libraries(audio_decoder queue)

# 添加视频滤镜库
add_library(video_filter STATIC src/VideoFilter.cpp)
target_include_directories(video_filter PRIVATE ${FFMPEG_INCLUDE_DIR})
target_link_libraries(video_filter queue)

# 添加音频滤镜库
add_library(audio_filter STATIC src/AudioFilter.cpp)
target_include_directories(audio_filter PRIVATE ${FFMPEG_INCLUDE_DIR})
target_link_libraries(audio_filter queue)

# 添加视频编码器库
add_library(video_encoder STATIC src/VideoEncoder.cpp)
target_include_directories(video_encoder PRIVATE ${FFMPEG_INCLUDE_DIR})
target_link_libraries(video_encoder queue video_filter)

# 添加音频编码器库
add_library(audio_encoder STATIC src/AudioEncoder.cpp)
target_include_directories(audio_encoder PRIVATE ${FFMPEG_INCLUDE_DIR})
target_link_libraries(audio_encoder queue audio_filter)

# 添加复用器库
add_library(muxer STATIC src/Muxer.cpp)
target_include_directories(muxer PRIVATE ${FFMPEG_INCLUDE_DIR})
target_link_libraries(muxer queue)

# 创建可执行文件目标
add_executable(transcode Transcode.cpp)
target_include_directories(transcode PRIVATE ${FFMPEG_INCLUDE_DIR})

# 链接所有库
if(FFMPEG_MERGED_LIB)
    # 如果存在合并库，优先使用合并库
    target_link_libraries(transcode 
        queue 
        demux 
        video_decoder 
        audio_decoder 
        video_filter
        audio_filter
        video_encoder
        audio_encoder
        muxer
        ${FFMPEG_MERGED_LIB}
        ${SYS_FFMPEG_LIBS}  # 系统库作为备用
        ${EXTRA_LIBS}
    )
else()
    # 如果不存在合并库，只使用系统库
    target_link_libraries(transcode 
        queue 
        demux 
        video_decoder 
        audio_decoder 
        video_filter
        audio_filter
        video_encoder
        audio_encoder
        muxer
        ${SYS_FFMPEG_LIBS}
        ${EXTRA_LIBS}
    )
endif()

# 如果是Linux系统，添加-Wl,--no-as-needed标志和--allow-multiple-definition标志
if(UNIX AND NOT APPLE)
    set_target_properties(transcode PROPERTIES LINK_FLAGS "-Wl,--no-as-needed -Wl,--allow-multiple-definition")
endif()

# 安装目标
install(TARGETS transcode
    RUNTIME DESTINATION bin
)

# 打印配置信息
message(STATUS "项目配置信息:")
message(STATUS "  FFmpeg头文件目录: ${FFMPEG_INCLUDE_DIR}")
message(STATUS "  FFmpeg库目录: ${FFMPEG_LIB_DIR}")
message(STATUS "  合并的FFmpeg库: ${FFMPEG_MERGED_LIB}")
message(STATUS "  系统FFmpeg库: ${SYS_FFMPEG_LIBS}")
message(STATUS "  额外依赖库: ${EXTRA_LIBS}")
message(STATUS "  C++标准: ${CMAKE_CXX_STANDARD}")
message(STATUS "  构建类型: ${CMAKE_BUILD_TYPE}")
message(STATUS "  输出目录: ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")