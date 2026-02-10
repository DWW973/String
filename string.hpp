/**
 * @file string.hpp
 * @brief 字符串类的定义和实现
 * @details 实现了一个高效的字符串类，支持SSO（Small String Optimization）优化
 */
#ifndef STRING_HPP
#define STRING_HPP

/**
 * @def NAMESPACE
 * @brief 命名空间宏
 * @details 根据不同要求，调整命名空间名字
 */
#define NAMESPACE cgui

#include <cstring>
#include <algorithm>
#include <initializer_list>
#include <stdexcept>
#include <ostream>
#include <istream>
#include <cstdint>
#include <cstddef>
#include <cctype>
#include <utility>
#include <iterator>
#include <cstdio>

namespace NAMESPACE {

    /**
     * @class String
     * @brief 字符串类
     * @details 实现了一个高效的字符串类，支持SSO（Small String Optimization）优化
     */
    class String {
    public:
        /**
         * @brief 大小类型
         */
        using size_type = size_t;
        /**
         * @brief 无效位置标记
         */
        static constexpr size_type npos = static_cast<size_type>(-1);
        /**
         * @brief SSO容量
         * @details 减少1以适应null结尾符
         */
        static constexpr size_type SSO_CAPACITY = 14;

    private:
        /**
         * @union Storage
         * @brief 存储联合体
         * @details 用于SSO优化，小字符串直接存储在栈上，大字符串存储在堆上
         */
        union Storage {
            /**
             * @struct sso
             * @brief 小字符串存储结构
             */
            struct {
                /**
                 * @brief 数据缓冲区
                 * @details 为null结尾符预留空间
                 */
                char data[SSO_CAPACITY + 1];
                /**
                 * @brief 字符串大小
                 */
                uint8_t size;
            } sso;
            /**
             * @struct large
             * @brief 大字符串存储结构
             */
            struct {
                /**
                 * @brief 数据指针
                 */
                char* ptr;
                /**
                 * @brief 字符串大小
                 */
                size_type size;
                /**
                 * @brief 容量
                 */
                size_type capacity;
            } large;
        } storage;
        /**
         * @brief 是否使用SSO
         */
        bool is_sso;

        /**
         * @brief 获取数据指针
         * @return 数据指针
         */
        char* data_ptr() noexcept {
            return is_sso ? storage.sso.data : storage.large.ptr;
        }

        /**
         * @brief 获取数据指针（常量版本）
         * @return 数据指针
         */
        const char* data_ptr() const noexcept {
            return is_sso ? storage.sso.data : storage.large.ptr;
        }

        /**
         * @struct Allocator
         * @brief 内存分配器
         */
        struct Allocator {
            /**
             * @brief 分配内存
             * @param size 大小
             * @return 分配的内存指针
             */
            static char* allocate(size_type size) {
                return new char[size + 1];
            }
            /**
             * @brief 释放内存
             * @param ptr 内存指针
             */
            static void deallocate(char* ptr) {
                delete[] ptr;
            }
        };

        /**
         * @brief 重新分配内存
         * @param new_cap 新容量
         */
        void reallocate(size_type new_cap) {
            if (new_cap <= capacity()) return;

            // 计算新容量
            new_cap = std::max(new_cap, capacity() + capacity() / 2);
            new_cap = std::max(new_cap, size_type(16));
            new_cap = (new_cap + 7) & ~7; // 对齐到8字节

            // 分配新内存
            char* new_ptr = Allocator::allocate(new_cap);
            size_type curr_size = size();
            
            try {
                // 复制数据
                if (curr_size > 0) {
                    std::memcpy(new_ptr, data_ptr(), curr_size);
                }
                new_ptr[curr_size] = '\0';
            } catch (...) {
                // 异常处理
                Allocator::deallocate(new_ptr);
                throw;
            }

            // 释放旧内存
            if (!is_sso) {
                Allocator::deallocate(storage.large.ptr);
            }

            // 更新存储
            storage.large.ptr = new_ptr;
            storage.large.size = curr_size;
            storage.large.capacity = new_cap;
            is_sso = false;
        }

    public:
        /**
         * @brief 默认构造函数
         */
        String() noexcept : is_sso(true) {
            storage.sso.size = 0;
            storage.sso.data[0] = '\0';
        }

        /**
         * @brief 初始化列表构造函数
         * @param il 初始化列表
         */
        String(std::initializer_list<char> il) {
            size_type len = il.size();
            if (len == 0) {
                is_sso = true;
                storage.sso.size = 0;
                storage.sso.data[0] = '\0';
            } else {
                const char* begin = il.begin();
                initialize(begin, len);
            }
        }

        /**
         * @brief 迭代器构造函数
         * @param first 开始迭代器
         * @param last 结束迭代器
         */
        template<typename InputIt>
        String(InputIt first, InputIt last) {
            size_type len = std::distance(first, last);
            if (len == 0) {
                is_sso = true;
                storage.sso.size = 0;
                storage.sso.data[0] = '\0';
                return;
            }

            reserve(len);
            char* p = data_ptr();
            for (auto it = first; it != last; ++it) {
                *p++ = *it;
            }
            *p = '\0';

            if (is_sso) {
                storage.sso.size = static_cast<uint8_t>(len);
            } else {
                storage.large.size = len;
            }
        }

        /**
         * @brief C字符串构造函数
         * @param str C字符串
         */
        String(const char* str) {
            size_type len = (str) ? std::strlen(str) : 0;
            initialize(str, len);
        }

        /**
         * @brief C字符串构造函数
         * @param str C字符串
         * @param len 长度
         */
        String(const char* str, size_type len) {
            initialize(str, len);
        }

        /**
         * @brief 拷贝构造函数
         * @param other 另一个字符串
         */
        String(const String& other) {
            if (other.is_sso) {
                // 复制SSO数据
                std::memcpy(&storage.sso, &other.storage.sso, sizeof(storage.sso));
                is_sso = true;
            } else {
                // 复制堆数据
                storage.large.size = other.storage.large.size;
                storage.large.capacity = other.storage.large.size;
                storage.large.ptr = Allocator::allocate(storage.large.capacity);
                std::memcpy(storage.large.ptr, other.storage.large.ptr, storage.large.size + 1);
                is_sso = false;
            }
        }

        /**
         * @brief 移动构造函数
         * @param other 另一个字符串
         */
        String(String&& other) noexcept {
            if (other.is_sso) {
                // 复制SSO数据
                std::memcpy(&storage.sso, &other.storage.sso, sizeof(storage.sso));
                is_sso = true;
            } else {
                // 移动堆数据
                storage.large = other.storage.large;
                is_sso = false;
            }
            // 重置原字符串
            other.is_sso = true;
            other.storage.sso.size = 0;
            other.storage.sso.data[0] = '\0';
        }

        /**
         * @brief 析构函数
         */
        ~String() {
            if (!is_sso) {
                Allocator::deallocate(storage.large.ptr);
            }
        }

        /**
         * @brief 拷贝赋值运算符
         * @param other 另一个字符串
         * @return 自身引用
         */
        String& operator=(const String& other) {
            if (this != &other) {
                String temp(other);
                swap(temp);
            }
            return *this;
        }

        /**
         * @brief 移动赋值运算符
         * @param other 另一个字符串
         * @return 自身引用
         */
        String& operator=(String&& other) noexcept {
            if (this != &other) {
                // 释放自身内存
                if (!is_sso) {
                    Allocator::deallocate(storage.large.ptr);
                }
                
                // 复制或移动数据
                if (other.is_sso) {
                    std::memcpy(&storage.sso, &other.storage.sso, sizeof(storage.sso));
                } else {
                    storage.large = other.storage.large;
                }
                
                is_sso = other.is_sso;
                
                // 重置原字符串
                other.is_sso = true;
                other.storage.sso.size = 0;
                other.storage.sso.data[0] = '\0';
            }
            return *this;
        }

        /**
         * @brief C字符串赋值运算符
         * @param str C字符串
         * @return 自身引用
         */
        String& operator=(const char* str) {
            *this = String(str);
            return *this;
        }

        /**
         * @brief 字符赋值运算符
         * @param c 字符
         * @return 自身引用
         */
        String& operator=(char c) {
            clear();
            push_back(c);
            return *this;
        }

        /**
         * @brief 初始化列表赋值运算符
         * @param il 初始化列表
         * @return 自身引用
         */
        String& operator=(std::initializer_list<char> il) {
            String temp(il);
            swap(temp);
            return *this;
        }

        /**
         * @brief 获取大小
         * @return 大小
         */
        size_type size() const noexcept {
            return is_sso ? static_cast<size_type>(storage.sso.size) : storage.large.size;
        }

        /**
         * @brief 获取容量
         * @return 容量
         */
        size_type capacity() const noexcept {
            return is_sso ? SSO_CAPACITY : storage.large.capacity;
        }

        /**
         * @brief 检查是否为空
         * @return 是否为空
         */
        bool empty() const noexcept {
            return size() == 0;
        }

        /**
         * @brief 预留空间
         * @param new_cap 新容量
         */
        void reserve(size_type new_cap) {
            if (new_cap > capacity()) {
                if (is_sso) {
                    // 从SSO转换为堆存储
                    char* new_ptr = Allocator::allocate(new_cap);
                    size_type curr_size = size();
                    
                    try {
                        if (curr_size > 0) {
                            std::memcpy(new_ptr, storage.sso.data, curr_size);
                        }
                        new_ptr[curr_size] = '\0';
                    } catch (...) {
                        Allocator::deallocate(new_ptr);
                        throw;
                    }
                    
                    storage.large.ptr = new_ptr;
                    storage.large.size = curr_size;
                    storage.large.capacity = new_cap;
                    is_sso = false;
                } else {
                    // 重新分配堆内存
                    reallocate(new_cap);
                }
            }
        }

        /**
         * @brief 收缩容量
         */
        void shrink_to_fit() {
            if (!is_sso && size() < capacity()) {
                reallocate(size());
            }
        }

        /**
         * @brief 下标运算符
         * @param pos 位置
         * @return 字符
         */
        char operator[](size_type pos) const noexcept {
            return data_ptr()[pos];
        }

        /**
         * @brief 下标运算符
         * @param pos 位置
         * @return 字符引用
         */
        char& operator[](size_type pos) noexcept {
            return data_ptr()[pos];
        }

        /**
         * @brief 安全下标访问
         * @param pos 位置
         * @return 字符
         * @throw std::out_of_range 位置越界
         */
        char at(size_type pos) const {
            if (pos >= size()) {
                throw std::out_of_range("FastString::at() - index out of range");
            }
            return data_ptr()[pos];
        }

        /**
         * @brief 安全下标访问
         * @param pos 位置
         * @return 字符引用
         * @throw std::out_of_range 位置越界
         */
        char& at(size_type pos) {
            if (pos >= size()) {
                throw std::out_of_range("FastString::at() - index out of range");
            }
            return data_ptr()[pos];
        }

        /**
         * @brief 获取首字符
         * @return 首字符
         * @throw std::out_of_range 字符串为空
         */
        char front() const {
            if (empty()) {
                throw std::out_of_range("FastString::front() - string is empty");
            }
            return data_ptr()[0];
        }

        /**
         * @brief 获取首字符引用
         * @return 首字符引用
         * @throw std::out_of_range 字符串为空
         */
        char& front() {
            if (empty()) {
                throw std::out_of_range("FastString::front() - string is empty");
            }
            return data_ptr()[0];
        }

        /**
         * @brief 获取尾字符
         * @return 尾字符
         * @throw std::out_of_range 字符串为空
         */
        char back() const {
            if (empty()) {
                throw std::out_of_range("FastString::back() - string is empty");
            }
            return data_ptr()[size() - 1];
        }

        /**
         * @brief 获取尾字符引用
         * @return 尾字符引用
         * @throw std::out_of_range 字符串为空
         */
        char& back() {
            if (empty()) {
                throw std::out_of_range("FastString::back() - string is empty");
            }
            return data_ptr()[size() - 1];
        }

        /**
         * @brief 获取C字符串
         * @return C字符串
         */
        const char* c_str() const noexcept {
            return data_ptr();
        }

        /**
         * @brief 获取数据指针
         * @return 数据指针
         */
        const char* data() const noexcept {
            return data_ptr();
        }

        /**
         * @brief 获取数据指针
         * @return 数据指针
         */
        char* data() noexcept {
            return data_ptr();
        }

        /**
         * @brief 清空字符串
         */
        void clear() noexcept {
            if (!is_sso) {
                storage.large.size = 0;
                storage.large.ptr[0] = '\0';
            } else {
                storage.sso.size = 0;
                storage.sso.data[0] = '\0';
            }
        }

        /**
         * @brief 交换字符串
         * @param other 另一个字符串
         */
        void swap(String& other) noexcept {
            using std::swap;
            swap(storage, other.storage);
            swap(is_sso, other.is_sso);
        }

        /**
         * @brief 追加字符串
         * @param str 字符串
         * @param len 长度
         * @return 自身引用
         */
        String& append(const char* str, size_type len) {
            if (len == 0) return *this;
            
            size_type curr_size = size();
            size_type new_size = curr_size + len;
            
            if (new_size > capacity()) {
                reallocate(new_size);
            }
            
            char* dst = data_ptr();
            std::memcpy(dst + curr_size, str, len);
            dst[new_size] = '\0'; // 正确设置字符串结束符的位置
            
            if (!is_sso) {
                storage.large.size = new_size;
            } else {
                storage.sso.size = static_cast<uint8_t>(new_size);
            }
            
            return *this;
        }

        /**
         * @brief 追加C字符串
         * @param str C字符串
         * @return 自身引用
         */
        String& operator+=(const char* str) {   
            return append(str, std::strlen(str));
        }

        /**
         * @brief 追加字符
         * @param c 字符
         * @return 自身引用
         */
        String& operator+=(char c) {
            size_type curr_size = size();
            
            if (curr_size < capacity()) {
                // 直接在现有空间追加
                data_ptr()[curr_size] = c;
                data_ptr()[curr_size + 1] = '\0';
                
                if (is_sso) {
                    storage.sso.size++;
                } else {
                    storage.large.size++;
                }
            } else {
                // 需要重新分配空间
                append(&c, 1);
            }
            
            return *this;
        }

        /**
         * @brief 追加字符串
         * @param other 字符串
         * @return 自身引用
         */
        String& operator+=(const String& other) {
            return append(other.data_ptr(), other.size());
        }

        /**
         * @brief 追加数字
         * @param value 数字
         * @return 自身引用
         */
        template<typename T>
        typename std::enable_if<std::is_arithmetic<T>::value, String&>::type
        operator+=(T value) {
            char buffer[32]; // 足够容纳64位整数或双精度浮点数
            int len = 0;
            
            if (std::is_integral<T>::value) {
                // 处理整数类型
                len = std::snprintf(buffer, sizeof(buffer), "%lld", static_cast<long long>(value));
            } else if (std::is_floating_point<T>::value) {
                // 处理浮点类型
                len = std::snprintf(buffer, sizeof(buffer), "%g", static_cast<double>(value));
            }
            
            if (len > 0) {
                append(buffer, static_cast<size_type>(len));
            }
            
            return *this;
        }

        /**
         * @brief 添加字符到末尾
         * @param c 字符
         */
        void push_back(char c) {
            *this += c;
        }

        /**
         * @brief 在指定位置插入字符
         * @param pos 位置
         * @param c 字符
         * @return 自身引用
         */
        String& insert(size_type pos, char c) {
            if (pos > size()) pos = size();
            
            size_type curr_size = size();
            size_type new_size = curr_size + 1;
            
            if (new_size > capacity()) {
                reserve(new_size);
            }
            
            char* data = data_ptr();
            std::memmove(data + pos + 1, data + pos, curr_size - pos);
            data[pos] = c;
            data[new_size] = '\0';
            
            if (!is_sso) {
                storage.large.size = new_size;
            } else {
                storage.sso.size = static_cast<uint8_t>(new_size);
            }
            
            return *this;
        }
        
        /**
         * @brief 在指定位置插入字符串
         * @param pos 位置
         * @param str 字符串
         * @param len 长度
         * @return 自身引用
         */
        String& insert(size_type pos, const char* str, size_type len) {
            if (pos > size()) pos = size();
            if (len == 0) return *this;
            
            size_type curr_size = size();
            size_type new_size = curr_size + len;
            
            if (new_size > capacity()) {
                reserve(new_size);
            }
            
            char* data = data_ptr();
            std::memmove(data + pos + len, data + pos, curr_size - pos);
            std::memcpy(data + pos, str, len);
            data[new_size] = '\0';
            
            if (!is_sso) {
                storage.large.size = new_size;
            } else {
                storage.sso.size = static_cast<uint8_t>(new_size);
            }
            
            return *this;
        }

        /**
         * @brief 在指定位置插入C字符串
         * @param pos 位置
         * @param str C字符串
         * @return 自身引用
         */
        String& insert(size_type pos, const char* str) {
            return insert(pos, str, std::strlen(str));
        }

        /**
         * @brief 在指定位置插入字符串
         * @param pos 位置
         * @param other 字符串
         * @return 自身引用
         */
        String& insert(size_type pos, const String& other) {
            return insert(pos, other.data_ptr(), other.size());
        }

        /**
         * @brief 删除子字符串
         * @param pos 位置
         * @param len 长度
         * @return 自身引用
         */
        String& erase(size_type pos = 0, size_type len = npos) {
            if (pos >= size()) return *this;
            
            size_type curr_size = size();
            size_type erase_len = std::min(len, curr_size - pos);
            if (erase_len == 0) return *this;
            
            size_type new_size = curr_size - erase_len;
            char* data = data_ptr();
            
            if (pos + erase_len < curr_size) {
                std::memmove(data + pos, data + pos + erase_len, new_size - pos);
            }
            data[new_size] = '\0';
            
            if (!is_sso) {
                storage.large.size = new_size;
            } else {
                storage.sso.size = static_cast<uint8_t>(new_size);
            }
            
            return *this;
        }

        /**
         * @brief 删除末尾字符
         */
        void pop_back() {
            if (empty()) return;
            
            size_type new_size = size() - 1;
            data_ptr()[new_size] = '\0';
            
            if (!is_sso) {
                storage.large.size = new_size;
            } else {
                storage.sso.size = static_cast<uint8_t>(new_size);
            }
        }

        /**
         * @brief 获取子字符串
         * @param pos 位置
         * @param len 长度
         * @return 子字符串
         */
        String substr(size_type pos, size_type len = npos) const {
            size_type curr_size = size();
            if (pos > curr_size) pos = curr_size;
            
            size_type remain = curr_size - pos;
            size_type substr_len = (len == npos) ? remain : std::min(len, remain);
            
            return String(data_ptr() + pos, substr_len);
        }

        /**
         * @brief 查找字符
         * @param c 字符
         * @param pos 起始位置
         * @return 位置
         */
        size_type find(char c, size_type pos = 0) const noexcept {
            if (pos >= size()) return npos;
            
            const char* data = data_ptr();
            const char* result = std::strchr(data + pos, c);
            return (result != nullptr) ? (result - data) : npos;
        }

        /**
         * @brief 查找字符串
         * @param str 字符串
         * @param pos 起始位置
         * @return 位置
         */
        size_type find(const char* str, size_type pos = 0) const noexcept {
            if (pos >= size() || str[0] == '\0') return npos;
            
            const char* data = data_ptr();
            size_type data_len = size();
            size_type str_len = std::strlen(str);
            
            if (pos + str_len > data_len) return npos;
            
            for (size_type i = pos; i <= data_len - str_len; ++i) {
                if (std::memcmp(data + i, str, str_len) == 0) {
                    return i;
                }
            }
            
            return npos;
        }
        
        /**
         * @brief 查找字符串
         * @param str 字符串
         * @param pos 起始位置
         * @return 位置
         */
        size_type find(const String& str, size_type pos = 0) const noexcept {
            return find(str.data_ptr(), pos);
        }

        /**
         * @brief 比较字符串
         * @param other 字符串
         * @return 比较结果
         */
        int compare(const String& other) const noexcept {
            return std::strcmp(data_ptr(), other.data_ptr());
        }

        /**
         * @brief 比较字符串
         * @param str C字符串
         * @return 比较结果
         */
        int compare(const char* str) const noexcept {
            return std::strcmp(data_ptr(), str);
        }

        // 友元声明
        friend bool operator==(const String& lhs, const String& rhs) noexcept;
        friend bool operator!=(const String& lhs, const String& rhs) noexcept;
        friend bool operator<(const String& lhs, const String& rhs) noexcept;
        friend bool operator<=(const String& lhs, const String& rhs) noexcept;
        friend bool operator>(const String& lhs, const String& rhs) noexcept;
        friend bool operator>=(const String& lhs, const String& rhs) noexcept;
        friend bool operator==(const String& lhs, const char* rhs) noexcept;
        friend bool operator!=(const String& lhs, const char* rhs) noexcept;
        friend bool operator==(const char* lhs, const String& rhs) noexcept;
        friend bool operator!=(const char* lhs, const String& rhs) noexcept;
        friend std::ostream& operator<<(std::ostream& os, const String& str);
        friend std::istream& operator>>(std::istream& is, String& str);
        friend std::istream& getline(std::istream& is, String& str, char delim);

        /**
         * @brief 替换子字符串
         * @param pos 位置
         * @param len 长度
         * @param str 字符串
         * @param str_len 字符串长度
         * @return 自身引用
         */
        String& replace(size_type pos, size_type len, const char* str, size_type str_len) {
            if (pos > size()) {
                throw std::out_of_range("String::replace() - pos out of range");
            }
            
            size_type curr_size = size();
            size_type erase_len = std::min(len, curr_size - pos);
            size_type new_size = curr_size - erase_len + str_len;
            
            if (new_size <= capacity() * 2) {
                // 在现有空间内替换
                if (pos + erase_len < curr_size) {
                    std::memmove(data_ptr() + pos + str_len, data_ptr() + pos + erase_len, curr_size - (pos + erase_len));
                }
                std::memcpy(data_ptr() + pos, str, str_len);
                data_ptr()[new_size] = '\0';
                
                if (!is_sso) {
                    storage.large.size = new_size;
                } else {
                    storage.sso.size = static_cast<uint8_t>(new_size);
                }
            } else {
                // 需要重新分配空间
                String temp;
                temp.reserve(new_size);
                
                if (pos > 0) {
                    temp.append(data_ptr(), pos);
                }
                
                if (str_len > 0) {
                    temp.append(str, str_len);
                }
                
                if (pos + erase_len < curr_size) {
                    temp.append(data_ptr() + pos + erase_len, curr_size - (pos + erase_len));
                }
                
                swap(temp);
            }
            
            return *this;
        }

        /**
         * @brief 替换子字符串
         * @param pos 位置
         * @param len 长度
         * @param str C字符串
         * @return 自身引用
         */
        String& replace(size_type pos, size_type len, const char* str) {
            return replace(pos, len, str, std::strlen(str));
        }

        /**
         * @brief 替换子字符串
         * @param pos 位置
         * @param len 长度
         * @param other 字符串
         * @return 自身引用
         */
        String& replace(size_type pos, size_type len, const String& other) {
            return replace(pos, len, other.data_ptr(), other.size());
        }

        /**
         * @brief 转换为小写
         * @return 小写字符串
         */
        String to_lower() const {
            String result(*this);
            char* p = result.data_ptr();
            for (size_type i = 0; i < result.size(); ++i) {
                p[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(p[i])));
            }
            return result;
        }

        /**
         * @brief 转换为大写
         * @return 大写字符串
         */
        String to_upper() const {
            String result(*this);
            char* p = result.data_ptr();
            for (size_type i = 0; i < result.size(); ++i) {
                p[i] = static_cast<char>(std::toupper(static_cast<unsigned char>(p[i])));
            }
            return result;
        }

        /**
         * @brief 反向查找字符
         * @param c 字符
         * @param pos 起始位置
         * @return 位置
         */
        size_type rfind(char c, size_type pos = npos) const noexcept {
            size_type curr_size = size();
            if (curr_size == 0) return npos;
            
            size_type end = (pos == npos || pos >= curr_size) ? curr_size - 1 : pos;
            const char* data = data_ptr();
            
            for (size_type i = end; ; --i) {
                if (data[i] == c) {
                    return i;
                }
                if (i == 0) break;
            }
            
            return npos;
        }

        /**
         * @brief 反向查找字符串
         * @param str 字符串
         * @param pos 起始位置
         * @return 位置
         */
        size_type rfind(const char* str, size_type pos = npos) const noexcept {
            if (str == nullptr || str[0] == '\0') return npos;
            
            size_type curr_size = size();
            size_type str_len = std::strlen(str);
            if (str_len > curr_size) return npos;
            
            size_type end = (pos == npos || pos >= curr_size) ? curr_size - str_len : pos;
            const char* data = data_ptr();
            
            for (size_type i = end; i != npos; --i) {
                if (i < str_len) break;
                if (std::memcmp(data + i, str, str_len) == 0) {
                    return i;
                }
                if (i == 0) break;
            }
            
            return npos;
        }
        
        /**
         * @brief 反向查找字符串
         * @param str 字符串
         * @param pos 起始位置
         * @return 位置
         */
        size_type rfind(const String& str, size_type pos = npos) const noexcept {
            return rfind(str.data_ptr(), pos);
        }

    private:
        /**
         * @brief 初始化字符串
         * @param str 字符串
         * @param len 长度
         */
        void initialize(const char* str, size_type len) {
            if (len == 0) {
                is_sso = true;
                storage.sso.size = 0;
                storage.sso.data[0] = '\0';
                return;
            }
            if (str == nullptr) {
                is_sso = true;
                storage.sso.size = 0;
                storage.sso.data[0] = '\0';
                return;
            }

            if (len <= SSO_CAPACITY) {
                // 使用SSO存储
                is_sso = true;
                storage.sso.size = static_cast<uint8_t>(len);
                std::memcpy(storage.sso.data, str, len);
                storage.sso.data[len] = '\0';
            } else {
                // 使用堆存储
                is_sso = false;
                storage.large.size = len;
                storage.large.capacity = len;
                storage.large.ptr = Allocator::allocate(len);
                std::memcpy(storage.large.ptr, str, len);
                storage.large.ptr[len] = '\0'; // 这里是安全的，因为Allocator::allocate分配了len+1字节
            }
        }
    };
    
    /**
     * @brief 交换两个字符串
     * @param lhs 左字符串
     * @param rhs 右字符串
     */
    inline void swap(String& lhs, String& rhs) noexcept {
        lhs.swap(rhs);
    }

    /**
     * @brief 等于运算符
     * @param lhs 左字符串
     * @param rhs 右字符串
     * @return 是否相等
     */
    inline bool operator==(const String& lhs, const String& rhs) noexcept {
        return lhs.size() == rhs.size() && std::strcmp(lhs.data_ptr(), rhs.data_ptr()) == 0;
    }

    /**
     * @brief 不等于运算符
     * @param lhs 左字符串
     * @param rhs 右字符串
     * @return 是否不相等
     */
    inline bool operator!=(const String& lhs, const String& rhs) noexcept {
        return !(lhs == rhs);
    }

    /**
     * @brief 小于运算符
     * @param lhs 左字符串
     * @param rhs 右字符串
     * @return 是否小于
     */
    inline bool operator<(const String& lhs, const String& rhs) noexcept {
        return std::strcmp(lhs.data_ptr(), rhs.data_ptr()) < 0;
    }

    /**
     * @brief 小于等于运算符
     * @param lhs 左字符串
     * @param rhs 右字符串
     * @return 是否小于等于
     */
    inline bool operator<=(const String& lhs, const String& rhs) noexcept {
        return std::strcmp(lhs.data_ptr(), rhs.data_ptr()) <= 0;
    }

    /**
     * @brief 大于运算符
     * @param lhs 左字符串
     * @param rhs 右字符串
     * @return 是否大于
     */
    inline bool operator>(const String& lhs, const String& rhs) noexcept {
        return std::strcmp(lhs.data_ptr(), rhs.data_ptr()) > 0;
    }

    /**
     * @brief 大于等于运算符
     * @param lhs 左字符串
     * @param rhs 右字符串
     * @return 是否大于等于
     */
    inline bool operator>=(const String& lhs, const String& rhs) noexcept {
        return std::strcmp(lhs.data_ptr(), rhs.data_ptr()) >= 0;
    }

    /**
     * @brief 等于运算符
     * @param lhs 左字符串
     * @param rhs 右C字符串
     * @return 是否相等
     */
    inline bool operator==(const String& lhs, const char* rhs) noexcept {
        if (rhs == nullptr) return lhs.empty();
        return std::strcmp(lhs.data_ptr(), rhs) == 0;
    }

    /**
     * @brief 等于运算符
     * @param lhs 左C字符串
     * @param rhs 右字符串
     * @return 是否相等
     */
    inline bool operator==(const char* lhs, const String& rhs) noexcept {
        return rhs == lhs;
    }

    /**
     * @brief 不等于运算符
     * @param lhs 左字符串
     * @param rhs 右C字符串
     * @return 是否不相等
     */
    inline bool operator!=(const String& lhs, const char* rhs) noexcept {
        return !(lhs == rhs);
    }

    /**
     * @brief 不等于运算符
     * @param lhs 左C字符串
     * @param rhs 右字符串
     * @return 是否不相等
     */
    inline bool operator!=(const char* lhs, const String& rhs) noexcept {
        return !(rhs == lhs);
    }

    // 二元加法运算符
    /**
     * @brief 字符串加法
     * @param lhs 左字符串
     * @param rhs 右字符串
     * @return 新字符串
     */
    inline String operator+(const String& lhs, const String& rhs) {
        String result(lhs);
        result += rhs;
        return result;
    }

    /**
     * @brief 字符串加法
     * @param lhs 左字符串
     * @param rhs 右C字符串
     * @return 新字符串
     */
    inline String operator+(const String& lhs, const char* rhs) {
        String result(lhs);
        result += rhs;
        return result;
    }

    /**
     * @brief 字符串加法
     * @param lhs 左C字符串
     * @param rhs 右字符串
     * @return 新字符串
     */
    inline String operator+(const char* lhs, const String& rhs) {
        String result(lhs);
        result += rhs;
        return result;
    }

    /**
     * @brief 字符串加法
     * @param lhs 左字符串
     * @param rhs 右字符
     * @return 新字符串
     */
    inline String operator+(const String& lhs, char rhs) {
        String result(lhs);
        result += rhs;
        return result;
    }

    /**
     * @brief 字符串加法
     * @param lhs 左字符
     * @param rhs 右字符串
     * @return 新字符串
     */
    inline String operator+(char lhs, const String& rhs) {
        String result;
        result += lhs;
        result += rhs;
        return result;
    }

    /**
     * @brief 字符串加法
     * @param lhs 左字符串
     * @param rhs 右数字
     * @return 新字符串
     */
    template<typename T>
    inline typename std::enable_if<std::is_arithmetic<T>::value, String>::type
    operator+(const String& lhs, T rhs) {
        String result(lhs);
        result += rhs;
        return result;
    }

    /**
     * @brief 字符串加法
     * @param lhs 左数字
     * @param rhs 右字符串
     * @return 新字符串
     */
    template<typename T>
    inline typename std::enable_if<std::is_arithmetic<T>::value, String>::type
    operator+(T lhs, const String& rhs) {
        String result;
        result += lhs;
        result += rhs;
        return result;
    }

    /**
     * @brief 输出流运算符
     * @param os 输出流
     * @param str 字符串
     * @return 输出流
     */
    inline std::ostream& operator<<(std::ostream& os, const String& str) {  
        return os << str.c_str();
    }

    /**
     * @brief 输入流运算符
     * @param is 输入流
     * @param str 字符串
     * @return 输入流
     */
    inline std::istream& operator>>(std::istream& is, String& str) {
        str.clear();
        char buffer[1024];
        
        if (is >> buffer) {
            str = buffer;
            
            // 处理大型输入
            while (is.rdbuf()->in_avail() > 0) {
                is.read(buffer, sizeof(buffer));
                str.append(buffer, static_cast<size_t>(is.gcount()));
            }
        }
        
        return is;
    }
    
    /**
     * @brief 读取一行
     * @param is 输入流
     * @param str 字符串
     * @param delim 分隔符
     * @return 输入流
     */
    inline std::istream& getline(std::istream& is, String& str, char delim = '\n') {
        str.clear();
        char buffer[1024];
        
        while (is.good()) {
            is.getline(buffer, sizeof(buffer), delim);
            size_t count = static_cast<size_t>(is.gcount());
            
            if (count > 0) {
                if (count == sizeof(buffer)-1 && !is.eof() && !is.fail()) {
                    // 缓冲区满，且不是EOF或错误
                    str.append(buffer, count);
                } else {
                    // 添加读取到的内容，包括空字符串
                    str.append(buffer, count);
                    break;
                }
            } else if (is.eof()) {
                // EOF且没有读取到任何内容，结束
                break;
            }
        }
        
        return is;
    }
}
#endif // CPL_STRING_HPP