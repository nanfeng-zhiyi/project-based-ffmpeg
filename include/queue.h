#ifndef QUEUE_H
#define QUEUE_H

#include <mutex>              // 替换pthread_mutex_t
#include <condition_variable> // 替换pthread_cond_t

// 前向声明
extern "C"
{
    struct AVPacket;
    struct AVFrame;

    // FFmpeg函数声明
    void av_packet_free(AVPacket **pkt);
    void av_frame_free(AVFrame **frame);
}

// 队列节点结构体
template <typename T>
struct QueueNode
{
    T data;
    QueueNode<T> *next;

    QueueNode(const T &value) : data(value), next(nullptr) {}
};

// 线程安全的队列类（基于链表实现）
template <typename T>
class ThreadSafeQueue
{
protected: // 改为protected，以便子类可以访问
    QueueNode<T> *head;
    QueueNode<T> *tail;
    int size;

    // 互斥锁和条件变量，用于线程同步
    mutable std::mutex mutex;
    std::condition_variable cond;

private:
    // 禁止拷贝构造和赋值操作
    ThreadSafeQueue(const ThreadSafeQueue &) = delete;
    ThreadSafeQueue &operator=(const ThreadSafeQueue &) = delete;

public:
    // 构造函数
    ThreadSafeQueue() : head(nullptr), tail(nullptr), size(0)
    {
        // 使用C++11的mutex和condition_variable不需要显式初始化
    }

    // 析构函数
    virtual ~ThreadSafeQueue()
    {
        // 清空队列
        clear();

        // 使用C++11的mutex和condition_variable不需要显式销毁
    }

    // 入队操作
    void push(const T &value)
    {
        // 创建新节点
        QueueNode<T> *newNode = new QueueNode<T>(value);

        // 加锁
        std::unique_lock<std::mutex> lock(mutex);

        // 如果队列为空，头尾指针都指向新节点
        if (isEmptyUnsafe())
        {
            head = newNode;
            tail = newNode;
        }
        else
        {
            // 否则，将新节点添加到尾部
            tail->next = newNode;
            tail = newNode;
        }

        // 增加队列大小
        size++;

        // 发送信号，通知可能在等待的线程
        // 注意：在持有锁的情况下通知，确保消费者线程能立即获取数据
        cond.notify_one();

        // 解锁会在unique_lock析构时自动发生
    }

    // 出队操作，如果队列为空则阻塞
    T pop()
    {
        // 加锁
        std::unique_lock<std::mutex> lock(mutex);

        // 如果队列为空，等待条件变量
        cond.wait(lock, [this]
                  { return !isEmptyUnsafe(); });

        // 获取头节点的数据
        QueueNode<T> *temp = head;
        T value = temp->data;

        // 更新头指针
        head = head->next;

        // 如果队列变为空，更新尾指针
        if (head == nullptr)
        {
            tail = nullptr;
        }

        // 减少队列大小
        size--;

        // 删除旧的头节点
        delete temp;

        // 解锁自动发生在unique_lock析构时
        return value;
    }

    // 尝试出队，如果队列为空则返回false
    bool tryPop(T &value)
    {
        // 加锁
        std::unique_lock<std::mutex> lock(mutex);

        // 如果队列为空，返回false
        if (isEmptyUnsafe())
        {
            return false;
        }

        // 获取头节点的数据
        QueueNode<T> *temp = head;
        value = temp->data;

        // 更新头指针
        head = head->next;

        // 如果队列变为空，更新尾指针
        if (head == nullptr)
        {
            tail = nullptr;
        }

        // 减少队列大小
        size--;

        // 删除旧的头节点
        delete temp;

        // 解锁自动发生在unique_lock析构时
        return true;
    }

    // 获取队列大小
    int getSize()
    {
        std::lock_guard<std::mutex> lock(mutex);
        return size;
    }

    // 提供给外部调用
    bool isEmpty()
    {
        std::lock_guard<std::mutex> lock(mutex);
        return head == nullptr;
    }

    // 当前队列对象已经获取锁
    bool isEmptyUnsafe() const
    {
        return head == nullptr;
    }

    // 清空队列
    virtual void clear()
    {
        std::lock_guard<std::mutex> lock(mutex);

        // 逐个删除节点
        QueueNode<T> *current = head;
        while (current != nullptr)
        {
            QueueNode<T> *temp = current;
            current = current->next;
            delete temp;
        }

        // 重置队列状态
        head = nullptr;
        tail = nullptr;
        size = 0;
    }
};

// 视频包队列
class VideoPacketQueue : public ThreadSafeQueue<void *>
{
public:
    VideoPacketQueue() : ThreadSafeQueue<void *>() {}
    ~VideoPacketQueue() {}

    // 重写clear方法，确保正确释放AVPacket资源
    void clear() override
    {
        std::lock_guard<std::mutex> lock(mutex);

        // 逐个删除节点，并释放AVPacket
        QueueNode<void *> *current = head;
        while (current != nullptr)
        {
            QueueNode<void *> *temp = current;
            current = current->next;

            // 释放AVPacket
            if (temp->data != nullptr)
            {
                AVPacket *packet = static_cast<AVPacket *>(temp->data);
                // 使用FFmpeg函数释放AVPacket
                av_packet_free(&packet);
            }

            delete temp;
        }

        // 重置队列状态
        head = nullptr;
        tail = nullptr;
        size = 0;
    }
};

// 音频包队列
class AudioPacketQueue : public ThreadSafeQueue<void *>
{
public:
    AudioPacketQueue() : ThreadSafeQueue<void *>() {}
    ~AudioPacketQueue() {}

    // 重写clear方法，确保正确释放AVPacket资源
    void clear() override
    {
        std::lock_guard<std::mutex> lock(mutex);

        // 逐个删除节点，并释放AVPacket
        QueueNode<void *> *current = head;
        while (current != nullptr)
        {
            QueueNode<void *> *temp = current;
            current = current->next;

            // 释放AVPacket
            if (temp->data != nullptr)
            {
                AVPacket *packet = static_cast<AVPacket *>(temp->data);
                // 使用FFmpeg函数释放AVPacket
                av_packet_free(&packet);
            }

            delete temp;
        }

        // 重置队列状态
        head = nullptr;
        tail = nullptr;
        size = 0;
    }
};

// 视频帧队列 - 用于存储解码后的视频帧
class VideoFrameQueue : public ThreadSafeQueue<void *>
{
public:
    VideoFrameQueue() : ThreadSafeQueue<void *>() {}
    ~VideoFrameQueue() {}

    // 重写clear方法，确保正确释放AVFrame资源
    void clear() override
    {
        std::lock_guard<std::mutex> lock(mutex);

        // 逐个删除节点，并释放AVFrame
        QueueNode<void *> *current = head;
        while (current != nullptr)
        {
            QueueNode<void *> *temp = current;
            current = current->next;

            // 释放AVFrame
            if (temp->data != nullptr)
            {
                AVFrame *frame = static_cast<AVFrame *>(temp->data);
                // 使用FFmpeg函数释放AVFrame
                av_frame_free(&frame);
            }

            delete temp;
        }

        // 重置队列状态
        head = nullptr;
        tail = nullptr;
        size = 0;
    }
};

// 音频帧队列 - 用于存储解码后的音频帧
class AudioFrameQueue : public ThreadSafeQueue<void *>
{
public:
    AudioFrameQueue() : ThreadSafeQueue<void *>() {}
    ~AudioFrameQueue() {}

    // 重写clear方法，确保正确释放AVFrame资源
    void clear() override
    {
        std::lock_guard<std::mutex> lock(mutex);

        // 逐个删除节点，并释放AVFrame
        QueueNode<void *> *current = head;
        while (current != nullptr)
        {
            QueueNode<void *> *temp = current;
            current = current->next;

            // 释放AVFrame
            if (temp->data != nullptr)
            {
                AVFrame *frame = static_cast<AVFrame *>(temp->data);
                // 使用FFmpeg函数释放AVFrame
                av_frame_free(&frame);
            }

            delete temp;
        }

        // 重置队列状态
        head = nullptr;
        tail = nullptr;
        size = 0;
    }
};

#endif // QUEUE_H
