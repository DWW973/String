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
#include <vector>
#include <string>
#include <limits>

namespace NAMESPACE {

    /**
     * @class BasicString
     * @brief 字符串类模板
     * @details 实现了一个高效的字符串类，支持SSO（Small String Optimization）优化和自定义分配器
     */
    template<typename Allocator = std::allocator<char>>
    class BasicString {
    public:
        /**
         * @brief 大小类型
         */
        using size_type = size_t;
        /**
         * @brief 分配器类型
         */
        using allocator_type = Allocator;
        /**
         * @brief 迭代器类型
         */
        using iterator = char*;
        /**
         * @brief 常量迭代器类型
         */
        using const_iterator = const char*;
        /**
         * @brief 反向迭代器类型
         */
        using reverse_iterator = std::reverse_iterator<iterator>;
        /**
         * @brief 常量反向迭代器类型
         */
        using const_reverse_iterator = std::reverse_iterator<const_iterator>;
        /**
         * @brief 无效位置标记
         */
        static constexpr size_type npos = std::numeric_limits<size_type>::max();
        /**
         * @brief SSO容量
         * @details 减少1以适应null结尾符
         */
        #ifdef __x86_64__
        static constexpr size_type SSO_CAPACITY = 23;  // 64位系统
        #else
        static constexpr size_type SSO_CAPACITY = 15;  // 32位系统
        #endif

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
                 * @details 最低位用于标记是否为SSO模式
                 */
                uint8_t size_and_flag;
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
         * @brief 检查是否使用SSO
         * @return 是否使用SSO
         */
        bool is_sso() const noexcept {
            return !(storage.sso.size_and_flag & 0x80);
        }
        
        /**
         * @brief 设置SSO标志
         * @param flag 是否为SSO
         */
        void set_sso_flag(bool flag) noexcept {
            if (flag) {
                storage.sso.size_and_flag &= ~0x80;
            } else {
                storage.sso.size_and_flag |= 0x80;
            }
        }
        
        /**
         * @brief 获取SSO大小
         * @return SSO大小
         */
        uint8_t get_sso_size() const noexcept {
            return storage.sso.size_and_flag & 0x7F;
        }
        
        /**
         * @brief 设置SSO大小
         * @param size 大小
         */
        void set_sso_size(uint8_t size) noexcept {
            storage.sso.size_and_flag = (size & 0x7F) | (storage.sso.size_and_flag & 0x80);
        }

        /**
         * @brief 获取数据指针
         * @return 数据指针
         */
        char* data_ptr() noexcept {
            return is_sso() ? storage.sso.data : storage.large.ptr;
        }

        /**
         * @brief 获取数据指针（常量版本）
         * @return 数据指针
         */
        const char* data_ptr() const noexcept {
            return is_sso() ? storage.sso.data : storage.large.ptr;
        }

        /**
         * @brief 获取分配器
         * @return 分配器引用
         */
        allocator_type get_allocator() const noexcept {
            return allocator_;
        }
        
    private:
        /**
         * @brief 分配器
         */
        allocator_type allocator_;

        /**
         * @brief 重新分配内存
         * @param new_cap 新容量
         * @details 提供强异常安全性：如果发生异常，对象状态保持不变
         */
        void reallocate(size_type new_cap) {
            if (new_cap <= capacity()) return;

            // 使用预测容量
            new_cap = next_capacity(new_cap);

            // 分配新内存
            char* new_ptr = nullptr;
            try {
                new_ptr = allocator_.allocate(new_cap + 1);
            } catch (...) {
                // 内存分配失败，直接抛出异常，对象状态不变
                throw;
            }
            
            size_type curr_size = size();
            
            try {
                // 检查new_ptr是否有效
                if (new_ptr == nullptr) {
                    throw std::bad_alloc();
                }
                
                // 复制数据
                if (curr_size > 0) {
                    // 使用std::copy_n代替std::memcpy，提供更好的异常安全性
                    std::copy_n(data_ptr(), curr_size, new_ptr);
                }
                new_ptr[curr_size] = '\0';
            } catch (...) {
                // 复制数据失败，释放新分配的内存，然后重新抛出异常
                allocator_.deallocate(new_ptr, new_cap + 1);
                throw;
            }

            // 保存旧状态，以便在释放内存失败时恢复
            bool was_sso = is_sso();
            char* old_ptr = nullptr;
            size_type old_capacity = 0;
            
            if (!was_sso) {
                old_ptr = storage.large.ptr;
                old_capacity = storage.large.capacity;
            }

            // 更新存储
            storage.large.ptr = new_ptr;
            storage.large.size = curr_size;
            storage.large.capacity = new_cap;
            set_sso_flag(false);

            // 释放旧内存
            if (!was_sso) {
                try {
                    allocator_.deallocate(old_ptr, old_capacity + 1);
                } catch (...) {
                    // 释放旧内存失败，对象已经更新为新状态，无法恢复
                    // 但至少我们已经成功分配了新内存并复制了数据
                    // 这里可以考虑记录错误，但不应该抛出异常
                }
            }
        }

    public:
        /**
         * @brief 默认构造函数
         */
        BasicString() noexcept
            : allocator_() {
            set_sso_flag(true);
            set_sso_size(0);
            storage.sso.data[0] = '\0';
        }
        
        /**
         * @brief 带分配器的构造函数
         */
        explicit BasicString(const allocator_type& alloc) noexcept
            : allocator_(alloc) {
            set_sso_flag(true);
            set_sso_size(0);
            storage.sso.data[0] = '\0';
        }

        /**
         * @brief 初始化列表构造函数
         * @param il 初始化列表
         */
        BasicString(std::initializer_list<char> il)
            : allocator_() {
            size_type len = il.size();
            if (len == 0) {
                set_sso_flag(true);
                set_sso_size(0);
                storage.sso.data[0] = '\0';
            } else {
                const char* begin = il.begin();
                initialize(begin, len);
            }
        }
        
        /**
         * @brief 带分配器的初始化列表构造函数
         * @param il 初始化列表
         * @param alloc 分配器
         */
        BasicString(std::initializer_list<char> il, const allocator_type& alloc)
            : allocator_(alloc) {
            size_type len = il.size();
            if (len == 0) {
                set_sso_flag(true);
                set_sso_size(0);
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
        BasicString(InputIt first, InputIt last)
            : allocator_() {
            size_type len = std::distance(first, last);
            if (len == 0) {
                set_sso_flag(true);
                set_sso_size(0);
                storage.sso.data[0] = '\0';
                return;
            }

            reserve(len);
            char* p = data_ptr();
            for (auto it = first; it != last; ++it) {
                *p++ = *it;
            }
            *p = '\0';

            if (is_sso()) {
                set_sso_size(static_cast<uint8_t>(len));
            } else {
                storage.large.size = len;
            }
        }
        
        /**
         * @brief 带分配器的迭代器构造函数
         * @param first 开始迭代器
         * @param last 结束迭代器
         * @param alloc 分配器
         */
        template<typename InputIt>
        BasicString(InputIt first, InputIt last, const allocator_type& alloc)
            : allocator_(alloc) {
            size_type len = std::distance(first, last);
            if (len == 0) {
                set_sso_flag(true);
                set_sso_size(0);
                storage.sso.data[0] = '\0';
                return;
            }

            reserve(len);
            char* p = data_ptr();
            for (auto it = first; it != last; ++it) {
                *p++ = *it;
            }
            *p = '\0';

            if (is_sso()) {
                set_sso_size(static_cast<uint8_t>(len));
            } else {
                storage.large.size = len;
            }
        }

        /**
         * @brief C字符串构造函数
         * @param str C字符串
         */
        BasicString(const char* str)
            : allocator_() {
            size_type len = (str) ? std::strlen(str) : 0;
            initialize(str, len);
        }
        
        /**
         * @brief 带分配器的C字符串构造函数
         * @param str C字符串
         * @param alloc 分配器
         */
        BasicString(const char* str, const allocator_type& alloc)
            : allocator_(alloc) {
            size_type len = (str) ? std::strlen(str) : 0;
            initialize(str, len);
        }

        /**
         * @brief C字符串构造函数
         * @param str C字符串
         * @param len 长度
         */
        BasicString(const char* str, size_type len)
            : allocator_() {
            initialize(str, len);
        }
        
        /**
         * @brief 带分配器的C字符串构造函数
         * @param str C字符串
         * @param len 长度
         * @param alloc 分配器
         */
        BasicString(const char* str, size_type len, const allocator_type& alloc)
            : allocator_(alloc) {
            initialize(str, len);
        }

        /**
         * @brief 重复字符构造函数
         * @param count 字符个数
         * @param ch 字符
         */
        BasicString(size_type count, char ch)
            : allocator_() {
            if (count == 0) {
                set_sso_flag(true);
                set_sso_size(0);
                storage.sso.data[0] = '\0';
            } else {
                reserve(count);
                std::memset(data_ptr(), ch, count);
                data_ptr()[count] = '\0';
                
                if (is_sso()) {
                    set_sso_size(static_cast<uint8_t>(count));
                } else {
                    storage.large.size = count;
                }
            }
        }
        
        /**
         * @brief 带分配器的重复字符构造函数
         * @param count 字符个数
         * @param ch 字符
         * @param alloc 分配器
         */
        BasicString(size_type count, char ch, const allocator_type& alloc)
            : allocator_(alloc) {
            if (count == 0) {
                set_sso_flag(true);
                set_sso_size(0);
                storage.sso.data[0] = '\0';
            } else {
                reserve(count);
                std::memset(data_ptr(), ch, count);
                data_ptr()[count] = '\0';
                
                if (is_sso()) {
                    set_sso_size(static_cast<uint8_t>(count));
                } else {
                    storage.large.size = count;
                }
            }
        }

        /**
         * @brief 拷贝构造函数
         * @param other 另一个字符串
         */
        BasicString(const BasicString& other)
            : allocator_(other.allocator_) {
            if (other.is_sso()) {
                // 复制SSO数据
                std::memcpy(&storage.sso, &other.storage.sso, sizeof(storage.sso));
            } else {
                // 复制堆数据
                storage.large.size = other.storage.large.size;
                storage.large.capacity = other.storage.large.capacity;
                storage.large.ptr = allocator_.allocate(storage.large.capacity + 1);
                std::memcpy(storage.large.ptr, other.storage.large.ptr, storage.large.size + 1);
                set_sso_flag(false);
            }
        }
        
        /**
         * @brief 带分配器的拷贝构造函数
         * @param other 另一个字符串
         * @param alloc 分配器
         */
        BasicString(const BasicString& other, const allocator_type& alloc)
            : allocator_(alloc) {
            if (other.is_sso()) {
                // 复制SSO数据
                std::memcpy(&storage.sso, &other.storage.sso, sizeof(storage.sso));
            } else {
                // 复制堆数据
                storage.large.size = other.storage.large.size;
                storage.large.capacity = other.storage.large.capacity;
                storage.large.ptr = allocator_.allocate(storage.large.capacity + 1);
                std::memcpy(storage.large.ptr, other.storage.large.ptr, storage.large.size + 1);
                set_sso_flag(false);
            }
        }

        /**
         * @brief 移动构造函数
         * @param other 另一个字符串
         */
        BasicString(BasicString&& other) noexcept
            : allocator_(std::move(other.allocator_)) {
            if (other.is_sso()) {
                // 复制SSO数据
                std::memcpy(&storage.sso, &other.storage.sso, sizeof(storage.sso));
            } else {
                // 移动堆数据
                storage.large = other.storage.large;
                set_sso_flag(false);
            }
            // 重置原字符串
            other.set_sso_flag(true);
            other.set_sso_size(0);
            other.storage.sso.data[0] = '\0';
        }
        
        /**
         * @brief 带分配器的移动构造函数
         * @param other 另一个字符串
         * @param alloc 分配器
         */
        BasicString(BasicString&& other, const allocator_type& alloc) noexcept
            : allocator_(alloc) {
            if (other.is_sso()) {
                // 复制SSO数据
                std::memcpy(&storage.sso, &other.storage.sso, sizeof(storage.sso));
            } else {
                // 复制堆数据（因为分配器不同，不能直接移动）
                storage.large.size = other.storage.large.size;
                storage.large.capacity = other.storage.large.capacity;
                storage.large.ptr = allocator_.allocate(storage.large.capacity + 1);
                std::memcpy(storage.large.ptr, other.storage.large.ptr, storage.large.size + 1);
                set_sso_flag(false);
                // 释放原字符串的内存
                other.allocator_.deallocate(other.storage.large.ptr, other.storage.large.capacity + 1);
            }
            // 重置原字符串
            other.set_sso_flag(true);
            other.set_sso_size(0);
            other.storage.sso.data[0] = '\0';
        }

        /**
         * @brief 析构函数
         */
        ~BasicString() {
            if (!is_sso()) {
                allocator_.deallocate(storage.large.ptr, storage.large.capacity + 1);
            }
        }

        /**
         * @brief 拷贝赋值运算符
         * @param other 另一个字符串
         * @return 自身引用
         */
        BasicString& operator=(const BasicString& other) {
            if (this != &other) {
                BasicString temp(other);
                swap(temp);
            }
            return *this;
        }

        /**
         * @brief 移动赋值运算符
         * @param other 另一个字符串
         * @return 自身引用
         */
        BasicString& operator=(BasicString&& other) noexcept {
            // 使用copy-and-swap idiom确保异常安全和分配器一致性
            BasicString temp(std::move(other));
            swap(temp);
            return *this;
        }

        /**
         * @brief C字符串赋值运算符
         * @param str C字符串
         * @return 自身引用
         */
        BasicString& operator=(const char* str) {
            *this = BasicString(str, get_allocator());
            return *this;
        }

        /**
         * @brief 字符赋值运算符
         * @param c 字符
         * @return 自身引用
         */
        BasicString& operator=(char c) {
            clear();
            push_back(c);
            return *this;
        }

        /**
         * @brief 初始化列表赋值运算符
         * @param il 初始化列表
         * @return 自身引用
         */
        BasicString& operator=(std::initializer_list<char> il) {
            BasicString temp(il, get_allocator());
            swap(temp);
            return *this;
        }

        /**
         * @brief 获取大小
         * @return 大小
         */
        size_type size() const noexcept {
            return is_sso() ? static_cast<size_type>(get_sso_size()) : storage.large.size;
        }

        /**
         * @brief 获取容量
         * @return 容量
         */
        size_type capacity() const noexcept {
            return is_sso() ? SSO_CAPACITY : storage.large.capacity;
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
         * @param exact 是否使用精确容量
         */
        void reserve(size_type new_cap, bool exact = false) {
            if (new_cap > capacity()) {
                if (is_sso()) {
                    // 从SSO转换为堆存储
                    char* new_ptr = allocator_.allocate(new_cap + 1);
                    size_type curr_size = size();
                    
                    try {
                        if (curr_size > 0) {
                            std::memcpy(new_ptr, storage.sso.data, curr_size);
                        }
                        new_ptr[curr_size] = '\0';
                    } catch (...) {
                        allocator_.deallocate(new_ptr, new_cap + 1);
                        throw;
                    }
                    
                    storage.large.ptr = new_ptr;
                    storage.large.size = curr_size;
                    storage.large.capacity = new_cap;
                    set_sso_flag(false);
                } else {
                    // 重新分配堆内存
                    if (exact) {
                        // 使用精确容量
                        reallocate(new_cap);
                    } else {
                        // 使用预测容量
                        reallocate(next_capacity(new_cap));
                    }
                }
            }
        }

        /**
         * @brief 收缩容量
         */
        void shrink_to_fit() {
            if (!is_sso() && size() < capacity()) {
                if (size() <= SSO_CAPACITY) {
                    // 转换回SSO存储
                    size_type curr_size = size();
                    
                    // 先复制到临时缓冲区
                    char temp_data[SSO_CAPACITY + 1];
                    std::memcpy(temp_data, data_ptr(), curr_size);
                    temp_data[curr_size] = '\0';
                    
                    // 释放堆内存
                    allocator_.deallocate(storage.large.ptr, storage.large.capacity + 1);
                    
                    // 更新为SSO存储
                    set_sso_flag(true);
                    set_sso_size(static_cast<uint8_t>(curr_size));
                    std::memcpy(storage.sso.data, temp_data, curr_size + 1);
                } else {
                    // 仍然使用堆存储，收缩容量
                    reallocate(size());
                }
            }
        }

        /**
         * @brief 容量预测优化
         * @param new_size 新大小
         * @return 预测的容量
         */
        size_type next_capacity(size_type new_size) const noexcept {
            size_type cap = capacity();
            while (cap < new_size) {
                cap = cap + cap / 2;  // 1.5倍增长
                cap = (cap + 7) & ~7; // 对齐到8字节
            }
            return cap;
        }

        /**
         * @brief 调整字符串大小
         * @param count 新大小
         * @param ch 填充字符
         */
        void resize(size_type count, char ch = '\0') {
            size_type curr_size = size();
            if (count > curr_size) {
                reserve(count);  // 一次性分配足够空间
                char* data = data_ptr();
                std::memset(data + curr_size, ch, count - curr_size);
                data[count] = '\0';
                if (!is_sso()) {
                    storage.large.size = count;
                } else {
                    set_sso_size(static_cast<uint8_t>(count));
                }
            } else if (count < curr_size) {
                erase(count);
            }
        }

        /**
         * @brief 下标运算符
         * @param pos 位置
         * @return 字符
         * @details 添加边界检查，超出范围返回空字符
         */
        char operator[](size_type pos) const noexcept {
            if (pos >= size()) {
                return '\0';
            }
            return data_ptr()[pos];
        }

        /**
         * @brief 下标运算符
         * @param pos 位置
         * @return 字符引用
         * @details 添加边界检查，超出范围抛出异常
         * @throw std::out_of_range 位置越界
         */
        char& operator[](size_type pos) {
            if (pos >= size()) {
                throw std::out_of_range("BasicString::operator[] - index out of range");
            }
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
         * @brief 获取开始迭代器
         * @return 开始迭代器
         */
        iterator begin() noexcept {
            return data_ptr();
        }

        /**
         * @brief 获取结束迭代器
         * @return 结束迭代器
         */
        iterator end() noexcept {
            return data_ptr() + size();
        }

        /**
         * @brief 获取开始迭代器（常量版本）
         * @return 开始迭代器
         */
        const_iterator begin() const noexcept {
            return data_ptr();
        }

        /**
         * @brief 获取结束迭代器（常量版本）
         * @return 结束迭代器
         */
        const_iterator end() const noexcept {
            return data_ptr() + size();
        }

        /**
         * @brief 获取常量开始迭代器
         * @return 常量开始迭代器
         */
        const_iterator cbegin() const noexcept {
            return data_ptr();
        }

        /**
         * @brief 获取常量结束迭代器
         * @return 常量结束迭代器
         */
        const_iterator cend() const noexcept {
            return data_ptr() + size();
        }

        /**
         * @brief 获取反向开始迭代器
         * @return 反向开始迭代器
         */
        reverse_iterator rbegin() noexcept {
            return reverse_iterator(end());
        }

        /**
         * @brief 获取反向结束迭代器
         * @return 反向结束迭代器
         */
        reverse_iterator rend() noexcept {
            return reverse_iterator(begin());
        }

        /**
         * @brief 获取反向开始迭代器（常量版本）
         * @return 反向开始迭代器
         */
        const_reverse_iterator rbegin() const noexcept {
            return const_reverse_iterator(end());
        }

        /**
         * @brief 获取反向结束迭代器（常量版本）
         * @return 反向结束迭代器
         */
        const_reverse_iterator rend() const noexcept {
            return const_reverse_iterator(begin());
        }

        /**
         * @brief 获取常量反向开始迭代器
         * @return 常量反向开始迭代器
         */
        const_reverse_iterator crbegin() const noexcept {
            return const_reverse_iterator(cend());
        }

        /**
         * @brief 获取常量反向结束迭代器
         * @return 常量反向结束迭代器
         */
        const_reverse_iterator crend() const noexcept {
            return const_reverse_iterator(cbegin());
        }

        /**
         * @brief 清空字符串
         */
        void clear() noexcept {
            if (!is_sso()) {
                storage.large.size = 0;
                storage.large.ptr[0] = '\0';
            } else {
                set_sso_size(0);
                storage.sso.data[0] = '\0';
            }
        }

        /**
         * @brief 交换字符串
         * @param other 另一个字符串
         */
        void swap(BasicString& other) noexcept {
            // 直接交换存储结构和分配器
            Storage temp_storage = storage;
            storage = other.storage;
            other.storage = temp_storage;
            
            // 交换分配器
            std::swap(allocator_, other.allocator_);
        }

        /**
         * @brief 追加字符串
         * @param str 字符串
         * @param len 长度
         * @return 自身引用
         */
        BasicString& append(const char* str, size_type len) {
            if (len == 0) return *this;
            
            size_type curr_size = size();
            size_type new_size = curr_size + len;
            
            // 小字符串优化：对于小追加，避免函数调用开销
            if (len <= 4 && new_size <= capacity() && curr_size <= capacity() - len) {
                // 检查自引用情况
                char* dst = data_ptr();
                if (str >= dst && str < dst + curr_size) {
                    // 源字符串指向自身数据，先复制到临时缓冲区
                    char temp[5]; // 足够容纳4个字符加终止符
                    std::memcpy(temp, str, len);
                    temp[len] = '\0';
                    
                    // 从临时缓冲区复制
                    for (size_type i = 0; i < len; ++i) {
                        dst[curr_size + i] = temp[i];
                    }
                } else {
                    // 正常情况
                    for (size_type i = 0; i < len; ++i) {
                        dst[curr_size + i] = str[i];
                    }
                }
                dst[new_size] = '\0';
                
                if (is_sso()) {
                    set_sso_size(static_cast<uint8_t>(new_size));
                } else {
                    storage.large.size = new_size;
                }
                
                return *this;
            }
            
            // 处理自引用情况
            char* dst = data_ptr();
            if (str >= dst && str < dst + curr_size) {
                // 源字符串指向自身数据，先复制到临时缓冲区
                // 使用RAII管理临时内存
                struct TempBuffer {
                    char* ptr;
                    size_type size;
                    allocator_type& allocator;
                    
                    TempBuffer(size_type sz, allocator_type& alloc) : size(sz), allocator(alloc) {
                        ptr = allocator.allocate(size);
                    }
                    
                    ~TempBuffer() {
                        allocator.deallocate(ptr, size);
                    }
                } temp(len + 1, allocator_);
                
                std::memcpy(temp.ptr, str, len);
                temp.ptr[len] = '\0';
                
                if (new_size > capacity()) {
                    reallocate(new_size);
                }
                
                // 重新获取dst指针，因为reallocate可能改变了它
                dst = data_ptr();
                std::memcpy(dst + curr_size, temp.ptr, len);
                dst[new_size] = '\0';
            } else {
                // 正常情况
                if (new_size > capacity()) {
                    reallocate(new_size);
                }
                
                // 重新获取dst指针，因为reallocate可能改变了它
                dst = data_ptr();
                std::memcpy(dst + curr_size, str, len);
                dst[new_size] = '\0';
            }
            
            if (!is_sso()) {
                storage.large.size = new_size;
            } else {
                set_sso_size(static_cast<uint8_t>(new_size));
            }
            
            return *this;
        }

        /**
         * @brief 追加C字符串
         * @param str C字符串
         * @return 自身引用
         */
        BasicString& operator+=(const char* str) {   
            return append(str, std::strlen(str));
        }

        /**
         * @brief 追加字符
         * @param c 字符
         * @return 自身引用
         */
        BasicString& operator+=(char c) {
            size_type curr_size = size();
            
            if (curr_size < capacity()) {
                // 直接在现有空间追加
                data_ptr()[curr_size] = c;
                data_ptr()[curr_size + 1] = '\0';
                
                if (is_sso()) {
                    set_sso_size(static_cast<uint8_t>(curr_size + 1));
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
        BasicString& operator+=(const BasicString& other) {
            return append(other.data_ptr(), other.size());
        }

        /**
         * @brief 追加数字
         * @param value 数字
         * @return 自身引用
         */
        template<typename T>
        typename std::enable_if<std::is_arithmetic<T>::value, BasicString&>::type
        operator+=(T value) {
            char buffer[64]; // 足够容纳所有数值类型
            int len;
            
            if constexpr (std::is_integral<T>::value) {
                // 整数类型使用更精确的格式
                if constexpr (std::is_signed<T>::value) {
                    if constexpr (sizeof(T) <= sizeof(int)) {
                        len = std::snprintf(buffer, sizeof(buffer), "%d", value);
                    } else if constexpr (sizeof(T) <= sizeof(long)) {
                        len = std::snprintf(buffer, sizeof(buffer), "%ld", value);
                    } else {
                        len = std::snprintf(buffer, sizeof(buffer), "%lld", static_cast<long long>(value));
                    }
                } else {
                    if constexpr (sizeof(T) <= sizeof(unsigned int)) {
                        len = std::snprintf(buffer, sizeof(buffer), "%u", value);
                    } else if constexpr (sizeof(T) <= sizeof(unsigned long)) {
                        len = std::snprintf(buffer, sizeof(buffer), "%lu", value);
                    } else {
                        len = std::snprintf(buffer, sizeof(buffer), "%llu", static_cast<unsigned long long>(value));
                    }
                }
            } else {
                // 浮点数类型，使用%g避免过长小数
                len = std::snprintf(buffer, sizeof(buffer), "%.15g", value);
            }
            
            if (len > 0 && len < static_cast<int>(sizeof(buffer))) {
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
        BasicString& insert(size_type pos, char c) {
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
            
            if (!is_sso()) {
                storage.large.size = new_size;
            } else {
                set_sso_size(static_cast<uint8_t>(new_size));
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
        BasicString& insert(size_type pos, const char* str, size_type len) {
            if (pos > size()) pos = size();
            if (len == 0) return *this;
            
            size_type curr_size = size();
            size_type new_size = curr_size + len;
            
            // 处理自引用情况
            char* data = data_ptr();
            if (str >= data && str < data + curr_size) {
                // 源字符串指向自身数据，先复制到临时缓冲区
                // 使用RAII管理临时内存
                struct TempBuffer {
                    char* ptr;
                    size_type size;
                    allocator_type& allocator;
                    
                    TempBuffer(size_type sz, allocator_type& alloc) : size(sz), allocator(alloc) {
                        ptr = allocator.allocate(size);
                    }
                    
                    ~TempBuffer() {
                        allocator.deallocate(ptr, size);
                    }
                } temp(len + 1, allocator_);
                
                std::memcpy(temp.ptr, str, len);
                temp.ptr[len] = '\0';
                
                if (new_size > capacity()) {
                    reserve(new_size);
                }
                
                // 重新获取data指针，因为reserve可能改变了它
                data = data_ptr();
                std::memmove(data + pos + len, data + pos, curr_size - pos);
                std::memcpy(data + pos, temp.ptr, len);
                data[new_size] = '\0';
            } else {
                // 正常情况
                if (new_size > capacity()) {
                    reserve(new_size);
                }
                
                // 重新获取data指针，因为reserve可能改变了它
                data = data_ptr();
                std::memmove(data + pos + len, data + pos, curr_size - pos);
                std::memcpy(data + pos, str, len);
                data[new_size] = '\0';
            }
            
            if (!is_sso()) {
                storage.large.size = new_size;
            } else {
                set_sso_size(static_cast<uint8_t>(new_size));
            }
            
            return *this;
        }

        /**
         * @brief 在指定位置插入C字符串
         * @param pos 位置
         * @param str C字符串
         * @return 自身引用
         */
        BasicString& insert(size_type pos, const char* str) {
            return insert(pos, str, std::strlen(str));
        }

        /**
         * @brief 在指定位置插入字符串
         * @param pos 位置
         * @param other 字符串
         * @return 自身引用
         */
        BasicString& insert(size_type pos, const BasicString& other) {
            return insert(pos, other.data_ptr(), other.size());
        }

        /**
         * @brief 删除子字符串
         * @param pos 位置
         * @param len 长度
         * @return 自身引用
         */
        BasicString& erase(size_type pos = 0, size_type len = npos) {
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
            
            if (!is_sso()) {
                storage.large.size = new_size;
            } else {
                set_sso_size(static_cast<uint8_t>(new_size));
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
            
            if (!is_sso()) {
                storage.large.size = new_size;
            } else {
                set_sso_size(static_cast<uint8_t>(new_size));
            }
        }

        /**
         * @brief 获取子字符串
         * @param pos 位置
         * @param len 长度
         * @return 子字符串
         */
        BasicString substr(size_type pos, size_type len = npos) const {
            size_type curr_size = size();
            if (pos > curr_size) pos = curr_size;
            
            size_type remain = curr_size - pos;
            size_type substr_len = (len == npos) ? remain : std::min(len, remain);
            
            return BasicString(data_ptr() + pos, substr_len, get_allocator());
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
            if (pos >= size() || str == nullptr || str[0] == '\0') return npos;
            
            const char* data = data_ptr();
            size_type data_len = size();
            size_type str_len = std::strlen(str);
            
            if (pos + str_len > data_len) return npos;
            
            // 使用std::search进行查找，更安全，不会读取到缓冲区之外
            const char* start = data + pos;
            const char* end = data + data_len;
            const char* str_end = str + str_len;
            
            const char* result = std::search(start, end, str, str_end);
            if (result == end) return npos;
            return static_cast<size_type>(result - data);
        }
        
        /**
         * @brief 查找字符串
         * @param str 字符串
         * @param pos 起始位置
         * @return 位置
         */
        size_type find(const BasicString& str, size_type pos = 0) const noexcept {
            return find(str.data_ptr(), pos);
        }

        /**
         * @brief 比较字符串
         * @param other 字符串
         * @return 比较结果
         */
        int compare(const BasicString& other) const noexcept {
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
        template<typename Alloc1, typename Alloc2>
        friend bool operator==(const BasicString<Alloc1>& lhs, const BasicString<Alloc2>& rhs) noexcept;
        template<typename Alloc1, typename Alloc2>
        friend bool operator!=(const BasicString<Alloc1>& lhs, const BasicString<Alloc2>& rhs) noexcept;
        template<typename Alloc1, typename Alloc2>
        friend bool operator<(const BasicString<Alloc1>& lhs, const BasicString<Alloc2>& rhs) noexcept;
        template<typename Alloc1, typename Alloc2>
        friend bool operator<=(const BasicString<Alloc1>& lhs, const BasicString<Alloc2>& rhs) noexcept;
        template<typename Alloc1, typename Alloc2>
        friend bool operator>(const BasicString<Alloc1>& lhs, const BasicString<Alloc2>& rhs) noexcept;
        template<typename Alloc1, typename Alloc2>
        friend bool operator>=(const BasicString<Alloc1>& lhs, const BasicString<Alloc2>& rhs) noexcept;
        template<typename Alloc>
        friend bool operator==(const BasicString<Alloc>& lhs, const char* rhs) noexcept;
        template<typename Alloc>
        friend bool operator!=(const BasicString<Alloc>& lhs, const char* rhs) noexcept;
        template<typename Alloc>
        friend bool operator<(const BasicString<Alloc>& lhs, const char* rhs) noexcept;
        template<typename Alloc>
        friend bool operator<=(const BasicString<Alloc>& lhs, const char* rhs) noexcept;
        template<typename Alloc>
        friend bool operator>(const BasicString<Alloc>& lhs, const char* rhs) noexcept;
        template<typename Alloc>
        friend bool operator>=(const BasicString<Alloc>& lhs, const char* rhs) noexcept;
        template<typename Alloc>
        friend bool operator==(const char* lhs, const BasicString<Alloc>& rhs) noexcept;
        template<typename Alloc>
        friend bool operator!=(const char* lhs, const BasicString<Alloc>& rhs) noexcept;
        template<typename Alloc>
        friend bool operator<(const char* lhs, const BasicString<Alloc>& rhs) noexcept;
        template<typename Alloc>
        friend bool operator<=(const char* lhs, const BasicString<Alloc>& rhs) noexcept;
        template<typename Alloc>
        friend bool operator>(const char* lhs, const BasicString<Alloc>& rhs) noexcept;
        template<typename Alloc>
        friend bool operator>=(const char* lhs, const BasicString<Alloc>& rhs) noexcept;
        template<typename Alloc>
        friend std::ostream& operator<<(std::ostream& os, const BasicString<Alloc>& str);
        template<typename Alloc>
        friend std::istream& operator>>(std::istream& is, BasicString<Alloc>& str);
        template<typename Alloc>
        friend std::istream& getline(std::istream& is, BasicString<Alloc>& str, char delim);

        /**
         * @brief 替换子字符串
         * @param pos 位置
         * @param len 长度
         * @param str 字符串
         * @param str_len 字符串长度
         * @return 自身引用
         */
        BasicString& replace(size_type pos, size_type len, const char* str, size_type str_len) {
            if (pos > size()) {
                throw std::out_of_range("BasicString::replace() - pos out of range");
            }
            
            size_type curr_size = size();
            size_type erase_len = std::min(len, curr_size - pos);
            size_type new_size = curr_size - erase_len + str_len;
            
            // 处理自引用情况
            char* data = data_ptr();
            if (str >= data && str < data + curr_size) {
                // 源字符串指向自身数据，先复制到临时缓冲区
                // 使用RAII管理临时内存
                struct TempBuffer {
                    char* ptr;
                    size_type size;
                    allocator_type& allocator;
                    
                    TempBuffer(size_type sz, allocator_type& alloc) : size(sz), allocator(alloc) {
                        ptr = allocator.allocate(size);
                    }
                    
                    ~TempBuffer() {
                        allocator.deallocate(ptr, size);
                    }
                } temp(str_len + 1, allocator_);
                
                std::memcpy(temp.ptr, str, str_len);
                temp.ptr[str_len] = '\0';
                
                // 使用copy-and-swap idiom确保强异常安全
                BasicString temp_str(get_allocator());
                temp_str.reserve(new_size);
                
                if (pos > 0) {
                    temp_str.append(data, pos);
                }
                
                if (str_len > 0) {
                    temp_str.append(temp.ptr, str_len);
                }
                
                if (pos + erase_len < curr_size) {
                    temp_str.append(data + pos + erase_len, curr_size - (pos + erase_len));
                }
                
                swap(temp_str);
            } else {
                // 正常情况，使用copy-and-swap idiom确保强异常安全
                BasicString temp_str(get_allocator());
                temp_str.reserve(new_size);
                
                if (pos > 0) {
                    temp_str.append(data, pos);
                }
                
                if (str_len > 0) {
                    temp_str.append(str, str_len);
                }
                
                if (pos + erase_len < curr_size) {
                    temp_str.append(data + pos + erase_len, curr_size - (pos + erase_len));
                }
                
                swap(temp_str);
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
        BasicString& replace(size_type pos, size_type len, const char* str) {
            return replace(pos, len, str, std::strlen(str));
        }

        /**
         * @brief 替换子字符串
         * @param pos 位置
         * @param len 长度
         * @param other 字符串
         * @return 自身引用
         */
        BasicString& replace(size_type pos, size_type len, const BasicString& other) {
            return replace(pos, len, other.data_ptr(), other.size());
        }

        /**
         * @brief 转换为小写
         * @return 小写字符串
         */
        BasicString to_lower() const {
            BasicString result(*this);
            char* p = result.data_ptr();
            for (size_type i = 0; i < result.size(); ++i) {
                p[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(p[i])));
            }
            return result;
        }

        /**
         * @brief 从string_view构造
         * @param sv string_view
         */
        #if __cplusplus >= 201703L && __has_include(<string_view>)
        #include <string_view>

        BasicString(std::string_view sv) {
            initialize(sv.data(), sv.size());
        }
        
        /**
         * @brief 带分配器的从string_view构造
         * @param sv string_view
         * @param alloc 分配器
         */
        BasicString(std::string_view sv, const allocator_type& alloc)
            : allocator_(alloc) {
            initialize(sv.data(), sv.size());
        }

        /**
         * @brief 转换为string_view
         * @return string_view
         */
        operator std::string_view() const noexcept {
            return std::string_view(data(), size());
        }
        #endif

        /**
         * @brief 转换为大写
         * @return 大写字符串
         */
        BasicString to_upper() const {
            BasicString result(*this);
            char* p = result.data_ptr();
            for (size_type i = 0; i < result.size(); ++i) {
                p[i] = static_cast<char>(std::toupper(static_cast<unsigned char>(p[i])));
            }
            return result;
        }

        /**
         * @brief 去除字符串开头的空白字符
         * @return 自身引用
         */
        BasicString& trim_left() {
            const char* data = data_ptr();
            size_type pos = 0;
            while (pos < size() && std::isspace(static_cast<unsigned char>(data[pos]))) {
                ++pos;
            }
            if (pos > 0) {
                erase(0, pos);
            }
            return *this;
        }

        /**
         * @brief 去除字符串结尾的空白字符
         * @return 自身引用
         */
        BasicString& trim_right() {
            const char* data = data_ptr();
            size_type pos = size();
            while (pos > 0 && std::isspace(static_cast<unsigned char>(data[pos - 1]))) {
                --pos;
            }
            if (pos < size()) {
                erase(pos);
            }
            return *this;
        }

        /**
         * @brief 去除字符串开头和结尾的空白字符
         * @return 自身引用
         */
        BasicString& trim() {
            return trim_left().trim_right();
        }

        /**
         * @brief 获取最大大小
         * @return 最大大小
         */
        size_type max_size() const noexcept {
            // 使用std::allocator_traits获取分配器的最大大小
            return std::allocator_traits<allocator_type>::max_size(allocator_);
        }

        /**
         * @brief 检查是否以指定字符串开头
         * @param str 指定字符串
         * @return 是否以指定字符串开头
         */
        bool starts_with(const BasicString& str) const noexcept {
            return size() >= str.size() &&
                   std::memcmp(data(), str.data(), str.size()) == 0;
        }

        /**
         * @brief 检查是否以指定C字符串开头
         * @param str 指定C字符串
         * @return 是否以指定C字符串开头
         */
        bool starts_with(const char* str) const noexcept {
            if (str == nullptr) return empty();
            size_type str_len = std::strlen(str);
            return size() >= str_len &&
                   std::memcmp(data(), str, str_len) == 0;
        }

        /**
         * @brief 检查是否以指定字符开头
         * @param c 指定字符
         * @return 是否以指定字符开头
         */
        bool starts_with(char c) const noexcept {
            return !empty() && data()[0] == c;
        }

        /**
         * @brief 检查是否以指定字符串结尾
         * @param str 指定字符串
         * @return 是否以指定字符串结尾
         */
        bool ends_with(const BasicString& str) const noexcept {
            return size() >= str.size() &&
                   std::memcmp(data() + size() - str.size(), str.data(), str.size()) == 0;
        }

        /**
         * @brief 检查是否以指定C字符串结尾
         * @param str 指定C字符串
         * @return 是否以指定C字符串结尾
         */
        bool ends_with(const char* str) const noexcept {
            if (str == nullptr) return empty();
            size_type str_len = std::strlen(str);
            return size() >= str_len &&
                   std::memcmp(data() + size() - str_len, str, str_len) == 0;
        }

        /**
         * @brief 检查是否以指定字符结尾
         * @param c 指定字符
         * @return 是否以指定字符结尾
         */
        bool ends_with(char c) const noexcept {
            return !empty() && data()[size() - 1] == c;
        }

        /**
         * @brief 检查是否包含指定字符串
         * @param str 指定字符串
         * @return 是否包含指定字符串
         */
        bool contains(const BasicString& str) const noexcept {
            return find(str) != npos;
        }

        /**
         * @brief 检查是否包含指定C字符串
         * @param str 指定C字符串
         * @return 是否包含指定C字符串
         */
        bool contains(const char* str) const noexcept {
            return find(str) != npos;
        }

        /**
         * @brief 检查是否包含指定字符
         * @param c 指定字符
         * @return 是否包含指定字符
         */
        bool contains(char c) const noexcept {
            return find(c) != npos;
        }
        
        /**
         * @brief 验证字符串完整性
         * @details 在DEBUG模式下检查字符串的完整性
         */
        #ifdef DEBUG
        void validate() const {
            // 检查字符串是否正确以null结尾
            assert(data_ptr()[size()] == '\0');
            
            // 检查SSO标志一致性
            if (is_sso()) {
                assert(size() <= SSO_CAPACITY);
                assert(get_sso_size() == static_cast<uint8_t>(size()));
            } else {
                assert(storage.large.ptr != nullptr);
                assert(storage.large.size <= storage.large.capacity);
            }
            
            // 增加更多调试检查
            assert(size() <= max_size());  // 大小合法性检查
            assert(c_str()[size()] == '\0');  // 空终止符检查
        }
        #else
        void validate() const noexcept {
            // 非DEBUG模式下为空实现
        }
        #endif

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
            
            size_type start = (pos == npos || pos >= curr_size) ? curr_size - str_len : std::min(pos, curr_size - str_len);
            const char* data = data_ptr();
            
            for (size_type i = start;; --i) {
                if (std::memcmp(data + i, str, str_len) == 0) {
                    return i;
                }
                if (i == 0) break;
            }
            
            return npos;
        }

        /**
         * @brief 查找第一个匹配指定字符串中任意字符的位置
         * @param str 指定字符串
         * @param pos 起始位置
         * @return 位置
         */
        size_type find_first_of(const BasicString& str, size_type pos = 0) const noexcept {
            return find_first_of(str.data(), pos, str.size());
        }

        /**
         * @brief 查找第一个匹配指定C字符串中任意字符的位置
         * @param str 指定C字符串
         * @param pos 起始位置
         * @return 位置
         */
        size_type find_first_of(const char* str, size_type pos = 0) const noexcept {
            return find_first_of(str, pos, std::strlen(str));
        }

        /**
         * @brief 查找第一个匹配指定C字符串中任意字符的位置
         * @param str 指定C字符串
         * @param pos 起始位置
         * @param n 字符串长度
         * @return 位置
         */
        size_type find_first_of(const char* str, size_type pos, size_type n) const noexcept {
            if (pos >= size() || str == nullptr || n == 0) return npos;
            
            const char* data = data_ptr();
            size_type data_len = size();
            
            for (size_type i = pos; i < data_len; ++i) {
                if (std::memchr(str, data[i], n) != nullptr) {
                    return i;
                }
            }
            
            return npos;
        }

        /**
         * @brief 查找最后一个匹配指定字符串中任意字符的位置
         * @param str 指定字符串
         * @param pos 起始位置
         * @return 位置
         */
        size_type find_last_of(const BasicString& str, size_type pos = npos) const noexcept {
            return find_last_of(str.data(), pos, str.size());
        }

        /**
         * @brief 查找最后一个匹配指定C字符串中任意字符的位置
         * @param str 指定C字符串
         * @param pos 起始位置
         * @return 位置
         */
        size_type find_last_of(const char* str, size_type pos = npos) const noexcept {
            return find_last_of(str, pos, std::strlen(str));
        }

        /**
         * @brief 查找最后一个匹配指定C字符串中任意字符的位置
         * @param str 指定C字符串
         * @param pos 起始位置
         * @param n 字符串长度
         * @return 位置
         */
        size_type find_last_of(const char* str, size_type pos, size_type n) const noexcept {
            if (pos >= size() || str == nullptr || n == 0) return npos;
            
            const char* data = data_ptr();
            size_type data_len = size();
            size_type end = (pos == npos || pos >= data_len) ? data_len - 1 : pos;
            
            for (size_type i = end;; --i) {
                if (std::memchr(str, data[i], n) != nullptr) {
                    return i;
                }
                if (i == 0) break;
            }
            
            return npos;
        }

        /**
         * @brief 查找第一个不匹配指定字符串中任意字符的位置
         * @param str 指定字符串
         * @param pos 起始位置
         * @return 位置
         */
        size_type find_first_not_of(const BasicString& str, size_type pos = 0) const noexcept {
            return find_first_not_of(str.data(), pos, str.size());
        }

        /**
         * @brief 查找第一个不匹配指定C字符串中任意字符的位置
         * @param str 指定C字符串
         * @param pos 起始位置
         * @return 位置
         */
        size_type find_first_not_of(const char* str, size_type pos = 0) const noexcept {
            if (str == nullptr) return npos;
            return find_first_not_of(str, pos, std::strlen(str));
        }

        /**
         * @brief 查找第一个不匹配指定C字符串中任意字符的位置
         * @param str 指定C字符串
         * @param pos 起始位置
         * @param n 字符串长度
         * @return 位置
         */
        size_type find_first_not_of(const char* str, size_type pos, size_type n) const noexcept {
            if (pos >= size() || str == nullptr || n == 0) return npos;
            
            const char* data = data_ptr();
            size_type data_len = size();
            
            for (size_type i = pos; i < data_len; ++i) {
                if (std::memchr(str, data[i], n) == nullptr) {
                    return i;
                }
            }
            
            return npos;
        }

        /**
         * @brief 查找最后一个不匹配指定字符串中任意字符的位置
         * @param str 指定字符串
         * @param pos 起始位置
         * @return 位置
         */
        size_type find_last_not_of(const BasicString& str, size_type pos = npos) const noexcept {
            return find_last_not_of(str.data(), pos, str.size());
        }

        /**
         * @brief 查找最后一个不匹配指定C字符串中任意字符的位置
         * @param str 指定C字符串
         * @param pos 起始位置
         * @return 位置
         */
        size_type find_last_not_of(const char* str, size_type pos = npos) const noexcept {
            if (str == nullptr) return npos;
            return find_last_not_of(str, pos, std::strlen(str));
        }

        /**
         * @brief 查找最后一个不匹配指定C字符串中任意字符的位置
         * @param str 指定C字符串
         * @param pos 起始位置
         * @param n 字符串长度
         * @return 位置
         */
        size_type find_last_not_of(const char* str, size_type pos, size_type n) const noexcept {
            if (pos >= size() || str == nullptr || n == 0) return npos;
            
            const char* data = data_ptr();
            size_type data_len = size();
            size_type end = (pos == npos || pos >= data_len) ? data_len - 1 : pos;
            
            for (size_type i = end;; --i) {
                if (std::memchr(str, data[i], n) == nullptr) {
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
        size_type rfind(const BasicString& str, size_type pos = npos) const noexcept {
            return rfind(str.data_ptr(), pos);
        }

        /**
         * @brief 转换为整数
         * @return 整数值
         * @throw std::invalid_argument 如果字符串不是有效的整数
         */
        int to_int() const {
            char* end;  // 用于检测转换是否成功
            long result = std::strtol(data_ptr(), &end, 10);
            if (*end != '\0') {
                throw std::invalid_argument("String is not a valid integer");
            }
            if (result < std::numeric_limits<int>::min() || result > std::numeric_limits<int>::max()) {
                throw std::out_of_range("Integer value out of range");
            }
            return static_cast<int>(result);
        }

        /**
         * @brief 转换为长整数
         * @return 长整数值
         * @throw std::invalid_argument 如果字符串不是有效的长整数
         */
        long to_long() const {
            char* end;  // 用于检测转换是否成功
            long result = std::strtol(data_ptr(), &end, 10);
            if (*end != '\0') {
                throw std::invalid_argument("String is not a valid long integer");
            }
            return result;
        }

        /**
         * @brief 转换为无符号整数
         * @return 无符号整数值
         * @throw std::invalid_argument 如果字符串不是有效的无符号整数
         */
        unsigned int to_uint() const {
            char* end;  // 用于检测转换是否成功
            unsigned long result = std::strtoul(data_ptr(), &end, 10);
            if (*end != '\0') {
                throw std::invalid_argument("String is not a valid unsigned integer");
            }
            if (result > std::numeric_limits<unsigned int>::max()) {
                throw std::out_of_range("Unsigned integer value out of range");
            }
            return static_cast<unsigned int>(result);
        }

        /**
         * @brief 转换为无符号长整数
         * @return 无符号长整数值
         * @throw std::invalid_argument 如果字符串不是有效的无符号长整数
         */
        unsigned long to_ulong() const {
            char* end;  // 用于检测转换是否成功
            unsigned long result = std::strtoul(data_ptr(), &end, 10);
            if (*end != '\0') {
                throw std::invalid_argument("String is not a valid unsigned long integer");
            }
            return result;
        }

        /**
         * @brief 转换为双精度浮点数
         * @return 双精度浮点数值
         * @throw std::invalid_argument 如果字符串不是有效的浮点数
         */
        double to_double() const {
            char* end;  // 用于检测转换是否成功
            double result = std::strtod(data_ptr(), &end);
            if (*end != '\0') {
                throw std::invalid_argument("String is not a valid double");
            }
            return result;
        }

        /**
         * @brief 转换为单精度浮点数
         * @return 单精度浮点数值
         * @throw std::invalid_argument 如果字符串不是有效的浮点数
         */
        float to_float() const {
            char* end;  // 用于检测转换是否成功
            float result = std::strtof(data_ptr(), &end);
            if (*end != '\0') {
                throw std::invalid_argument("String is not a valid float");
            }
            return result;
        }

        /**
         * @brief 检查字符串是否为有效的数字
         * @return 是否为有效的数字
         */
        bool is_number() const noexcept {
            char* end;
            std::strtod(data_ptr(), &end);
            return *end == '\0' && !empty();
        }

        /**
         * @brief 检查字符串是否为有效的整数
         * @return 是否为有效的整数
         */
        bool is_integer() const noexcept {
            char* end;
            std::strtol(data_ptr(), &end, 10);
            return *end == '\0' && !empty();
        }

        /**
         * @brief 检查字符串是否为有效的浮点数
         * @return 是否为有效的浮点数
         */
        bool is_float() const noexcept {
            char* end;
            std::strtod(data_ptr(), &end);
            return *end == '\0' && !empty();
        }

        /**
         * @brief 格式化数字
         * @param value 数值
         * @param precision 精度（小数位数）
         * @return 格式化后的字符串
         */
        template<typename T>
        static BasicString format_number(T value, int precision = 2) {
            BasicString result;
            char buffer[64];
            int len;
            
            if constexpr (std::is_integral<T>::value) {
                len = std::snprintf(buffer, sizeof(buffer), "%lld", static_cast<long long>(value));
            } else {
                char format[16];
                std::snprintf(format, sizeof(format), "%%.%df", precision);
                len = std::snprintf(buffer, sizeof(buffer), format, value);
            }
            
            if (len > 0 && len < static_cast<int>(sizeof(buffer))) {
                result.append(buffer, static_cast<size_type>(len));
            }
            
            return result;
        }

    private:
        /**
         * @brief 初始化字符串
         * @param str 字符串
         * @param len 长度
         */
        void initialize(const char* str, size_type len) {
            if (len == 0) {
                set_sso_flag(true);
                set_sso_size(0);
                storage.sso.data[0] = '\0';
                return;
            }
            if (str == nullptr) {
                set_sso_flag(true);
                set_sso_size(0);
                storage.sso.data[0] = '\0';
                return;
            }

            if (len <= SSO_CAPACITY) {
                // 使用SSO存储
                set_sso_flag(true);
                set_sso_size(static_cast<uint8_t>(len));
                std::memcpy(storage.sso.data, str, len);
                storage.sso.data[len] = '\0';
            } else {
                // 使用堆存储
                set_sso_flag(false);
                storage.large.size = len;
                storage.large.capacity = len;
                storage.large.ptr = allocator_.allocate(len + 1);
                std::memcpy(storage.large.ptr, str, len);
                storage.large.ptr[len] = '\0'; // 这里是安全的，因为allocator_.allocate分配了len+1字节
            }
        }
    };
    
    /**
     * @brief 默认字符串类型
     * @details 使用std::allocator<char>作为默认分配器
     */
    using String = BasicString<>;
    
    /**
     * @brief 交换两个字符串
     * @param lhs 左字符串
     * @param rhs 右字符串
     */
    template<typename Alloc>
    inline void swap(BasicString<Alloc>& lhs, BasicString<Alloc>& rhs) noexcept {
        lhs.swap(rhs);
    }
    
    /**
     * @brief 交换两个字符串（String特化版本）
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
    template<typename Alloc1, typename Alloc2>
    inline bool operator==(const BasicString<Alloc1>& lhs, const BasicString<Alloc2>& rhs) noexcept {
        if (lhs.size() != rhs.size()) return false;
        return std::memcmp(lhs.data(), rhs.data(), lhs.size()) == 0;
    }

    /**
     * @brief 不等于运算符
     * @param lhs 左字符串
     * @param rhs 右字符串
     * @return 是否不相等
     */
    template<typename Alloc1, typename Alloc2>
    inline bool operator!=(const BasicString<Alloc1>& lhs, const BasicString<Alloc2>& rhs) noexcept {
        return !(lhs == rhs);
    }

    /**
     * @brief 小于运算符
     * @param lhs 左字符串
     * @param rhs 右字符串
     * @return 是否小于
     */
    template<typename Alloc1, typename Alloc2>
    inline bool operator<(const BasicString<Alloc1>& lhs, const BasicString<Alloc2>& rhs) noexcept {
        size_t min_size = std::min(lhs.size(), rhs.size());
        int cmp = std::memcmp(lhs.data(), rhs.data(), min_size);
        if (cmp != 0) return cmp < 0;
        return lhs.size() < rhs.size();
    }

    /**
     * @brief 小于等于运算符
     * @param lhs 左字符串
     * @param rhs 右字符串
     * @return 是否小于等于
     */
    template<typename Alloc1, typename Alloc2>
    inline bool operator<=(const BasicString<Alloc1>& lhs, const BasicString<Alloc2>& rhs) noexcept {
        return !(rhs < lhs);
    }

    /**
     * @brief 大于运算符
     * @param lhs 左字符串
     * @param rhs 右字符串
     * @return 是否大于
     */
    template<typename Alloc1, typename Alloc2>
    inline bool operator>(const BasicString<Alloc1>& lhs, const BasicString<Alloc2>& rhs) noexcept {
        return rhs < lhs;
    }

    /**
     * @brief 大于等于运算符
     * @param lhs 左字符串
     * @param rhs 右字符串
     * @return 是否大于等于
     */
    template<typename Alloc1, typename Alloc2>
    inline bool operator>=(const BasicString<Alloc1>& lhs, const BasicString<Alloc2>& rhs) noexcept {
        return !(lhs < rhs);
    }

    /**
     * @brief 等于运算符
     * @param lhs 左字符串
     * @param rhs 右C字符串
     * @return 是否相等
     */
    template<typename Alloc>
    inline bool operator==(const BasicString<Alloc>& lhs, const char* rhs) noexcept {
        if (rhs == nullptr) return lhs.empty();
        size_t rhs_len = std::strlen(rhs);
        if (lhs.size() != rhs_len) return false;
        return std::memcmp(lhs.data(), rhs, lhs.size()) == 0;
    }

    /**
     * @brief 不等于运算符
     * @param lhs 左字符串
     * @param rhs 右C字符串
     * @return 是否不相等
     */
    template<typename Alloc>
    inline bool operator!=(const BasicString<Alloc>& lhs, const char* rhs) noexcept {
        return !(lhs == rhs);
    }

    /**
     * @brief 小于运算符
     * @param lhs 左字符串
     * @param rhs 右C字符串
     * @return 是否小于
     */
    template<typename Alloc>
    inline bool operator<(const BasicString<Alloc>& lhs, const char* rhs) noexcept {
        if (rhs == nullptr) return false;
        size_t rhs_len = std::strlen(rhs);
        size_t min_size = std::min(lhs.size(), rhs_len);
        int cmp = std::memcmp(lhs.data(), rhs, min_size);
        if (cmp != 0) return cmp < 0;
        return lhs.size() < rhs_len;
    }

    /**
     * @brief 小于等于运算符
     * @param lhs 左字符串
     * @param rhs 右C字符串
     * @return 是否小于等于
     */
    template<typename Alloc>
    inline bool operator<=(const BasicString<Alloc>& lhs, const char* rhs) noexcept {
        return !(rhs < lhs);
    }

    /**
     * @brief 大于运算符
     * @param lhs 左字符串
     * @param rhs 右C字符串
     * @return 是否大于
     */
    template<typename Alloc>
    inline bool operator>(const BasicString<Alloc>& lhs, const char* rhs) noexcept {
        return rhs < lhs;
    }

    /**
     * @brief 大于等于运算符
     * @param lhs 左字符串
     * @param rhs 右C字符串
     * @return 是否大于等于
     */
    template<typename Alloc>
    inline bool operator>=(const BasicString<Alloc>& lhs, const char* rhs) noexcept {
        return !(lhs < rhs);
    }

    /**
     * @brief 等于运算符
     * @param lhs 左C字符串
     * @param rhs 右字符串
     * @return 是否相等
     */
    template<typename Alloc>
    inline bool operator==(const char* lhs, const BasicString<Alloc>& rhs) noexcept {
        return rhs == lhs;
    }

    /**
     * @brief 不等于运算符
     * @param lhs 左C字符串
     * @param rhs 右字符串
     * @return 是否不相等
     */
    template<typename Alloc>
    inline bool operator!=(const char* lhs, const BasicString<Alloc>& rhs) noexcept {
        return !(rhs == lhs);
    }

    /**
     * @brief 小于运算符
     * @param lhs 左C字符串
     * @param rhs 右字符串
     * @return 是否小于
     */
    template<typename Alloc>
    inline bool operator<(const char* lhs, const BasicString<Alloc>& rhs) noexcept {
        if (lhs == nullptr) return !rhs.empty();
        size_t lhs_len = std::strlen(lhs);
        size_t min_size = std::min(lhs_len, rhs.size());
        int cmp = std::memcmp(lhs, rhs.data(), min_size);
        if (cmp != 0) return cmp < 0;
        return lhs_len < rhs.size();
    }

    /**
     * @brief 小于等于运算符
     * @param lhs 左C字符串
     * @param rhs 右字符串
     * @return 是否小于等于
     */
    template<typename Alloc>
    inline bool operator<=(const char* lhs, const BasicString<Alloc>& rhs) noexcept {
        return !(rhs < lhs);
    }

    /**
     * @brief 大于运算符
     * @param lhs 左C字符串
     * @param rhs 右字符串
     * @return 是否大于
     */
    template<typename Alloc>
    inline bool operator>(const char* lhs, const BasicString<Alloc>& rhs) noexcept {
        return rhs < lhs;
    }

    /**
     * @brief 大于等于运算符
     * @param lhs 左C字符串
     * @param rhs 右字符串
     * @return 是否大于等于
     */
    template<typename Alloc>
    inline bool operator>=(const char* lhs, const BasicString<Alloc>& rhs) noexcept {
        return !(lhs < rhs);
    }

    /**
     * @brief 等于运算符（String特化版本）
     * @param lhs 左字符串
     * @param rhs 右字符串
     * @return 是否相等
     */
    inline bool operator==(const String& lhs, const String& rhs) noexcept {
        return operator==(static_cast<const BasicString<>&>(lhs), static_cast<const BasicString<>&>(rhs));
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
        size_t min_size = std::min(lhs.size(), rhs.size());
        int cmp = std::memcmp(lhs.data(), rhs.data(), min_size);
        if (cmp != 0) return cmp < 0;
        return lhs.size() < rhs.size();
    }

    /**
     * @brief 小于等于运算符
     * @param lhs 左字符串
     * @param rhs 右字符串
     * @return 是否小于等于
     */
    inline bool operator<=(const String& lhs, const String& rhs) noexcept {
        size_t min_size = std::min(lhs.size(), rhs.size());
        int cmp = std::memcmp(lhs.data(), rhs.data(), min_size);
        if (cmp != 0) return cmp <= 0;
        return lhs.size() <= rhs.size();
    }

    /**
     * @brief 大于运算符
     * @param lhs 左字符串
     * @param rhs 右字符串
     * @return 是否大于
     */
    inline bool operator>(const String& lhs, const String& rhs) noexcept {
        size_t min_size = std::min(lhs.size(), rhs.size());
        int cmp = std::memcmp(lhs.data(), rhs.data(), min_size);
        if (cmp != 0) return cmp > 0;
        return lhs.size() > rhs.size();
    }

    /**
     * @brief 大于等于运算符
     * @param lhs 左字符串
     * @param rhs 右字符串
     * @return 是否大于等于
     */
    inline bool operator>=(const String& lhs, const String& rhs) noexcept {
        size_t min_size = std::min(lhs.size(), rhs.size());
        int cmp = std::memcmp(lhs.data(), rhs.data(), min_size);
        if (cmp != 0) return cmp >= 0;
        return lhs.size() >= rhs.size();
    }

    /**
     * @brief 三向比较运算符 (C++20)
     * @param lhs 左字符串
     * @param rhs 右字符串
     * @return 比较结果
     */
    #if __cplusplus >= 202002L
    inline std::strong_ordering operator<=>(const String& lhs, const String& rhs) noexcept {
        size_type min_size = std::min(lhs.size(), rhs.size());
        int cmp = std::memcmp(lhs.data(), rhs.data(), min_size);
        if (cmp != 0) return cmp <=> 0;
        return lhs.size() <=> rhs.size();
    }
    #endif

    /**
     * @brief 等于运算符
     * @param lhs 左字符串
     * @param rhs 右C字符串
     * @return 是否相等
     */
    inline bool operator==(const String& lhs, const char* rhs) noexcept {
        if (rhs == nullptr) return lhs.empty();
        size_t rhs_len = std::strlen(rhs);
        if (lhs.size() != rhs_len) return false;
        return std::memcmp(lhs.data(), rhs, lhs.size()) == 0;
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
     * @brief 移动版本的字符串连接运算符
     * @param lhs 左字符串
     * @param rhs 右字符串
     * @return 连接后的字符串
     */
    inline String operator+(String&& lhs, String&& rhs) {
        if (lhs.capacity() - lhs.size() >= rhs.size()) {
            // 如果lhs有足够空间，直接追加
            lhs += rhs;
            return std::move(lhs);
        } else {
            // 否则创建新字符串
            String result(std::move(lhs));
            result += rhs;
            return result;
        }
    }

    /**
     * @brief 移动版本的字符串连接运算符（模板版本）
     * @param lhs 左字符串
     * @param rhs 右字符串
     * @return 连接后的字符串
     */
    template<typename Allocator>
    inline BasicString<Allocator> operator+(BasicString<Allocator>&& lhs, BasicString<Allocator>&& rhs) {
        if (lhs.capacity() - lhs.size() >= rhs.size()) {
            // 如果lhs有足够空间，直接追加
            lhs += rhs;
            return std::move(lhs);
        } else {
            // 否则创建新字符串
            BasicString<Allocator> result(std::move(lhs));
            result += rhs;
            return result;
        }
    }

    /**
     * @brief 混合版本的字符串连接运算符
     * @param lhs 左字符串
     * @param rhs 右字符串
     * @return 连接后的字符串
     */
    template<typename Allocator>
    inline BasicString<Allocator> operator+(BasicString<Allocator>&& lhs, const BasicString<Allocator>& rhs) {
        lhs += rhs;
        return std::move(lhs);
    }

    /**
     * @brief 混合版本的字符串连接运算符
     * @param lhs 左字符串
     * @param rhs 右字符串
     * @return 连接后的字符串
     */
    template<typename Allocator>
    inline BasicString<Allocator> operator+(const BasicString<Allocator>& lhs, BasicString<Allocator>&& rhs) {
        if (rhs.capacity() - rhs.size() >= lhs.size()) {
            // 如果rhs有足够空间，直接在rhs前插入
            rhs = lhs + std::move(rhs);
            return std::move(rhs);
        } else {
            // 否则创建新字符串
            BasicString<Allocator> result(lhs);
            result += std::move(rhs);
            return result;
        }
    }

    /**
     * @brief 混合版本的字符串连接运算符
     * @param lhs 左字符串（右值）
     * @param rhs 右C字符串
     * @return 连接后的字符串
     */
    template<typename Allocator>
    inline BasicString<Allocator> operator+(BasicString<Allocator>&& lhs, const char* rhs) {
        lhs += rhs;
        return std::move(lhs);
    }

    /**
     * @brief 混合版本的字符串连接运算符
     * @param lhs 左C字符串
     * @param rhs 右字符串（右值）
     * @return 连接后的字符串
     */
    template<typename Allocator>
    inline BasicString<Allocator> operator+(const char* lhs, BasicString<Allocator>&& rhs) {
        BasicString<Allocator> result(lhs);
        result += std::move(rhs);
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
    template<typename Allocator>
    inline std::istream& getline(std::istream& is, BasicString<Allocator>& str, char delim) {
        str.clear();
        const size_t initial_buffer_size = 256;
        
        // 使用vector管理缓冲区，自动处理内存分配和释放
        std::vector<char, Allocator> buffer(str.get_allocator());
        buffer.reserve(initial_buffer_size);
        size_t pos = 0;
        
        while (true) {
            // 读取字符直到遇到分隔符或文件结束
            char c;
            if (!is.get(c)) {
                break;
            }
            
            if (c == delim) {
                break;
            }
            
            // 检查缓冲区是否已满
            if (pos >= buffer.size() - 1) {
                // 动态增长缓冲区
                size_t new_size = buffer.empty() ? initial_buffer_size : buffer.size() * 2;
                buffer.resize(new_size);
            }
            
            buffer[pos++] = c;
        }
        
        // 将读取的内容追加到字符串
        if (pos > 0) {
            str.append(buffer.data(), pos);
        }
        
        return is;
    }
    
    /**
     * @brief 读取一行（String特化版本）
     * @param is 输入流
     * @param str 字符串
     * @param delim 分隔符
     * @return 输入流
     */
    inline std::istream& getline(std::istream& is, String& str, char delim = '\n') {
        return getline(is, static_cast<BasicString<>&>(str), delim);
    }
}
#endif // STRING_HPP
