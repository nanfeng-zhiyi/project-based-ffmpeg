#ifndef RING_BUFFER_H
#define RING_BUFFER_H

#include <cstddef> // 仅用于 size_t
#include <cstring> // 用于 memcpy

/**
 * @brief 通用环形缓冲区实现，不依赖STL
 *
 * 这是一个线程不安全的简单环形缓冲区实现，适用于单生产者单消费者场景。
 * 如果需要在多线程环境中使用，需要添加适当的同步机制。
 */
template <typename T>
class RingBuffer
{
private:
    T *buffer;       // 缓冲区数据
    size_t capacity; // 缓冲区容量
    size_t size;     // 当前存储的元素数量
    size_t head;     // 读取位置
    size_t tail;     // 写入位置
    bool overwrite;  // 是否允许覆盖旧数据

public:
    /**
     * @brief 构造函数
     *
     * @param capacity 缓冲区容量
     * @param allowOverwrite 是否允许在缓冲区满时覆盖旧数据
     */
    RingBuffer(size_t capacity, bool allowOverwrite = false)
        : capacity(capacity), size(0), head(0), tail(0), overwrite(allowOverwrite)
    {
        buffer = new T[capacity];
    }

    /**
     * @brief 析构函数
     */
    ~RingBuffer()
    {
        delete[] buffer;
    }

    /**
     * @brief 向缓冲区写入单个元素
     *
     * @param item 要写入的元素
     * @return 是否写入成功
     */
    bool write(const T &item)
    {
        if (isFull())
        {
            if (!overwrite)
            {
                return false; // 缓冲区已满且不允许覆盖
            }
            // 允许覆盖，移动头指针
            head = (head + 1) % capacity;
            size--;
        }

        buffer[tail] = item;
        tail = (tail + 1) % capacity;
        size++;
        return true;
    }

    /**
     * @brief 向缓冲区批量写入多个元素
     *
     * @param items 要写入的元素数组
     * @param count 元素数量
     * @return 实际写入的元素数量
     */
    size_t writeMultiple(const T *items, size_t count)
    {
        if (count == 0)
            return 0;

        size_t written = 0;
        for (size_t i = 0; i < count; i++)
        {
            if (write(items[i]))
            {
                written++;
            }
            else
            {
                break;
            }
        }
        return written;
    }

    /**
     * @brief 从缓冲区读取单个元素
     *
     * @param item 读取的元素将存储在此变量中
     * @return 是否读取成功
     */
    bool read(T &item)
    {
        if (isEmpty())
        {
            return false;
        }

        item = buffer[head];
        head = (head + 1) % capacity;
        size--;
        return true;
    }

    /**
     * @brief 从缓冲区批量读取多个元素
     *
     * @param items 存储读取元素的数组
     * @param count 要读取的元素数量
     * @return 实际读取的元素数量
     */
    size_t readMultiple(T *items, size_t count)
    {
        if (count == 0)
            return 0;

        size_t read_count = 0;
        for (size_t i = 0; i < count; i++)
        {
            if (read(items[i]))
            {
                read_count++;
            }
            else
            {
                break;
            }
        }
        return read_count;
    }

    /**
     * @brief 查看缓冲区中的下一个元素，但不移除它
     *
     * @param item 存储查看的元素
     * @return 是否成功查看
     */
    bool peek(T &item) const
    {
        if (isEmpty())
        {
            return false;
        }

        item = buffer[head];
        return true;
    }

    /**
     * @brief 清空缓冲区
     */
    void clear()
    {
        head = 0;
        tail = 0;
        size = 0;
    }

    /**
     * @brief 检查缓冲区是否为空
     *
     * @return 缓冲区是否为空
     */
    bool isEmpty() const
    {
        return size == 0;
    }

    /**
     * @brief 检查缓冲区是否已满
     *
     * @return 缓冲区是否已满
     */
    bool isFull() const
    {
        return size == capacity;
    }

    /**
     * @brief 获取缓冲区当前元素数量
     *
     * @return 当前元素数量
     */
    size_t getSize() const
    {
        return size;
    }

    /**
     * @brief 获取缓冲区容量
     *
     * @return 缓冲区容量
     */
    size_t getCapacity() const
    {
        return capacity;
    }

    /**
     * @brief 获取缓冲区剩余空间
     *
     * @return 剩余空间大小
     */
    size_t getAvailableSpace() const
    {
        return capacity - size;
    }
};

// 针对字节数据的特化版本，提供更高效的批量操作
template <>
class RingBuffer<unsigned char>
{
private:
    unsigned char *buffer;
    size_t capacity;
    size_t size;
    size_t head;
    size_t tail;
    bool overwrite;

public:
    RingBuffer(size_t capacity, bool allowOverwrite = false)
        : capacity(capacity), size(0), head(0), tail(0), overwrite(allowOverwrite)
    {
        buffer = new unsigned char[capacity];
    }

    ~RingBuffer()
    {
        delete[] buffer;
    }

    bool write(const unsigned char &item)
    {
        if (isFull())
        {
            if (!overwrite)
            {
                return false;
            }
            head = (head + 1) % capacity;
            size--;
        }

        buffer[tail] = item;
        tail = (tail + 1) % capacity;
        size++;
        return true;
    }

    // 针对字节数据的优化版本，使用memcpy提高效率
    size_t writeMultiple(const unsigned char *items, size_t count)
    {
        if (count == 0)
            return 0;
        if (isFull() && !overwrite)
            return 0;

        // 计算可以写入的数量
        size_t available = capacity - size;
        if (overwrite && count > capacity)
        {
            // 如果允许覆盖且数据量大于容量，只保留最后capacity个字节
            items += (count - capacity);
            count = capacity;
            clear(); // 清空缓冲区，准备写入新数据
        }
        else if (count > available && overwrite)
        {
            // 需要覆盖一些旧数据
            size_t overflow = count - available;
            head = (head + overflow) % capacity;
            size -= overflow;
        }
        else if (count > available)
        {
            // 不允许覆盖，只写入能容纳的部分
            count = available;
        }

        // 执行写入操作
        if (tail + count <= capacity)
        {
            // 连续空间足够
            memcpy(buffer + tail, items, count);
        }
        else
        {
            // 需要分两段写入
            size_t firstPart = capacity - tail;
            memcpy(buffer + tail, items, firstPart);
            memcpy(buffer, items + firstPart, count - firstPart);
        }

        tail = (tail + count) % capacity;
        size += count;
        return count;
    }

    bool read(unsigned char &item)
    {
        if (isEmpty())
        {
            return false;
        }

        item = buffer[head];
        head = (head + 1) % capacity;
        size--;
        return true;
    }

    // 针对字节数据的优化版本，使用memcpy提高效率
    size_t readMultiple(unsigned char *items, size_t count)
    {
        if (isEmpty() || count == 0)
            return 0;

        // 计算实际可读取的数量
        count = (count > size) ? size : count;

        if (head + count <= capacity)
        {
            // 连续空间足够
            memcpy(items, buffer + head, count);
        }
        else
        {
            // 需要分两段读取
            size_t firstPart = capacity - head;
            memcpy(items, buffer + head, firstPart);
            memcpy(items + firstPart, buffer, count - firstPart);
        }

        head = (head + count) % capacity;
        size -= count;
        return count;
    }

    bool peek(unsigned char &item) const
    {
        if (isEmpty())
        {
            return false;
        }

        item = buffer[head];
        return true;
    }

    void clear()
    {
        head = 0;
        tail = 0;
        size = 0;
    }

    bool isEmpty() const
    {
        return size == 0;
    }

    bool isFull() const
    {
        return size == capacity;
    }

    size_t getSize() const
    {
        return size;
    }

    size_t getCapacity() const
    {
        return capacity;
    }

    size_t getAvailableSpace() const
    {
        return capacity - size;
    }
};

#endif // RING_BUFFER_H