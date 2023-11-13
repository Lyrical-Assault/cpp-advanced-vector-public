#pragma once

#include <cassert>
#include <cstdlib>
#include <new>
#include <utility>
#include <memory>


template <typename T>
class RawMemory {
public:
    RawMemory() = default;

    explicit RawMemory(size_t capacity)
            : buffer_(Allocate(capacity))
            , capacity_(capacity) {
    }

    ~RawMemory() {
        Deallocate(buffer_);
    }

    RawMemory(const RawMemory&) = delete;

    RawMemory& operator=(const RawMemory& rhs) = delete;

    RawMemory(RawMemory&& other) noexcept : buffer_(std::exchange(other.buffer_, nullptr)),
                                            capacity_(std::exchange(other.capacity_, 0)){}

    RawMemory& operator=(RawMemory&& rhs) noexcept {
        if(this != &rhs) {
            RawMemory temp(std::move(rhs));
            Swap(temp);
        }
        return *this;
    }

    T* operator+(size_t offset) noexcept {
        // Разрешается получать адрес ячейки памяти, следующей за последним элементом массива
        assert(offset <= capacity_);
        return buffer_ + offset;
    }

    const T* operator+(size_t offset) const noexcept {
        return const_cast<RawMemory&>(*this) + offset;
    }

    const T& operator[](size_t index) const noexcept {
        return const_cast<RawMemory&>(*this)[index];
    }

    T& operator[](size_t index) noexcept {
        assert(index < capacity_);
        return buffer_[index];
    }

    void Swap(RawMemory& other) noexcept {
        std::swap(buffer_, other.buffer_);
        std::swap(capacity_, other.capacity_);
    }

    const T* GetAddress() const noexcept {
        return buffer_;
    }

    T* GetAddress() noexcept {
        return buffer_;
    }

    size_t Capacity() const {
        return capacity_;
    }

private:
    // Выделяет сырую память под n элементов и возвращает указатель на неё
    static T* Allocate(size_t n) {
        return n != 0 ? static_cast<T*>(operator new(n * sizeof(T))) : nullptr;
    }

    // Освобождает сырую память, выделенную ранее по адресу buf при помощи Allocate
    static void Deallocate(T* buf) noexcept {
        operator delete(buf);
    }

    T* buffer_ = nullptr;
    size_t capacity_ = 0;
};

template <typename T>
class Vector {
public:

    Vector() = default;

    explicit Vector(size_t size)
            : data_(size)
            , size_(size)
    {
        std::uninitialized_value_construct_n(data_.GetAddress(), size);
    }

    ~Vector() {
        std::destroy_n(data_.GetAddress(), size_);
    }

    Vector(const Vector& other)
            : data_(other.size_)
            , size_(other.size_)
    {
        std::uninitialized_copy_n(other.data_.GetAddress(), other.size_, data_.GetAddress());
    }

    Vector(Vector&& other) noexcept
                        : data_(std::move(other.data_))
                        , size_(std::exchange(other.size_, 0))
    {
        std::uninitialized_move_n(other.data_.GetAddress(), other.size_, data_.GetAddress());
    }

    Vector& operator=(const Vector& rhs) {
        if (this != &rhs) {
            if (rhs.size_ > data_.Capacity()) {
                Vector temp(rhs);
                Swap(temp);
            } else {
                size_t i = 0;
                if(rhs.size_ < size_){
                    for(; i < rhs.size_; ++i) {
                        data_[i] = rhs.data_[i];
                    }
                    std::destroy_n(data_.GetAddress() + rhs.size_, size_ - rhs.size_);
                } else {
                    for(; i < size_; ++i) {
                        data_[i] = rhs.data_[i];
                    }
                    std::uninitialized_copy_n(rhs.data_.GetAddress(), rhs.size_ - size_, data_.GetAddress() + i);
                }
                size_ = rhs.size_;
            }
        }
        return *this;
    }

    Vector& operator=(Vector&& rhs) noexcept {
        if (this != &rhs) {
            data_.Swap(rhs.data_);
            size_ = std::exchange(rhs.size_, 0);
        }
        return *this;
    }

    void Swap(Vector& other) noexcept {
        data_.Swap(other.data_);
        std::swap(size_, other.size_);
    }

    void Reserve(size_t new_capacity) {
        if (new_capacity <= data_.Capacity()) {
            return;
        }
        RawMemory<T> new_data(new_capacity);
        if constexpr (std::is_nothrow_move_constructible_v<T> || !std::is_copy_constructible_v<T>) {
            std::uninitialized_move_n(data_.GetAddress(), size_, new_data.GetAddress());
        } else {
            std::uninitialized_copy_n(data_.GetAddress(), size_, new_data.GetAddress());
        }
        std::destroy_n(data_.GetAddress(), size_);
        data_.Swap(new_data);
    }

    void Resize(size_t new_size) {
        if (new_size < size_) {
            std::destroy_n(data_.GetAddress() + new_size, size_ - new_size);
        } else if (new_size > size_) {
            Reserve(new_size);
            std::uninitialized_value_construct_n(data_.GetAddress() + size_, new_size - size_);
        }
        size_ = new_size;
    }

    template<typename S>
    void PushBack(S&& value) {
        EmplaceBack(std::forward<S>(value));
    }
    template <typename... Args>
    T& EmplaceBack(Args&&... args) {
        if (size_ == data_.Capacity()) {
            RawMemory<T> new_data(size_ == 0 ? 1 : size_ * 2);
            T* new_element = new (new_data + size_) T(std::forward<Args>(args)...);
            try {
                if constexpr (std::is_nothrow_move_constructible_v<T> || !std::is_copy_constructible_v<T>) {
                    std::uninitialized_move_n(data_.GetAddress(), size_, new_data.GetAddress());
                } else {
                    std::uninitialized_copy_n(data_.GetAddress(), size_, new_data.GetAddress());
                }
            } catch (...) {
                new_element->~T();
                throw;
            }
            std::destroy_n(data_.GetAddress(), size_);
            data_.Swap(new_data);
        } else {
            new (data_ + size_) T(std::forward<Args>(args)...);
        }
        return *(data_.GetAddress() + size_++);
    }

    void PopBack() noexcept {
        assert(size_ != 0);
        std::destroy_n(data_.GetAddress() + (size_ - 1), 1);
        --size_;
    }

    size_t Size() const noexcept {
        return size_;
    }

    size_t Capacity() const noexcept {
        return data_.Capacity();
    }

    const T& operator[](size_t index) const noexcept {
        return const_cast<Vector&>(*this)[index];
    }

    T& operator[](size_t index) noexcept {
        assert(index < size_);
        return data_[index];
    }

    using iterator = T*;

    using const_iterator = const T*;

    iterator begin() noexcept {
        return iterator{data_.GetAddress()};
    }
    iterator end() noexcept {
        return iterator{data_.GetAddress() + size_};
    }
    const_iterator begin() const noexcept {
        return const_iterator{data_.GetAddress()};
    }
    const_iterator end() const noexcept {
        return const_iterator{data_.GetAddress() + size_};
    }
    const_iterator cbegin() const noexcept {
        return const_iterator{data_.GetAddress()};
    }
    const_iterator cend() const noexcept {
        return const_iterator{data_.GetAddress() + size_};
    }

    template <typename... Args>
    iterator Emplace(const_iterator pos, Args&&... args) {
        assert(pos >= cbegin() && pos <= cend());
        size_t index = std::distance(cbegin(), pos);
        if (size_ == data_.Capacity()) {
            RawMemory<T> new_data(size_ == 0 ? 1 : size_ * 2);
            T* new_element = new (new_data + index) T(std::forward<Args>(args)...);
            try {
                if constexpr (std::is_nothrow_move_constructible_v<T> || !std::is_copy_constructible_v<T>) {
                    std::uninitialized_move_n(data_.GetAddress(), index, new_data.GetAddress());
                    std::uninitialized_move_n(data_.GetAddress() + index, size_ - index, new_data.GetAddress() + (index + 1));
                } else {
                    std::uninitialized_copy_n(data_.GetAddress(), index, new_data.GetAddress());
                    std::uninitialized_copy_n(data_.GetAddress() + index, size_ - index, new_data.GetAddress() + (index + 1));
                }
            } catch (...) {
                std::destroy_n(new_data.GetAddress(), index);
                new_element->~T();
                throw;
            }
            std::destroy_n(data_.GetAddress(), size_);
            data_.Swap(new_data);
            ++size_;
        } else {
            if (index < size_) {
                T* last_elem = data_.GetAddress() + size_++;
                new (last_elem) T(std::move(*(last_elem - 1)));
                std::move_backward(data_.GetAddress() + index, last_elem - 1, last_elem);
                data_[index] = T(std::forward<Args>(args)...);
            } else {
                new (data_ + size_) T(std::forward<Args>(args)...);
                ++size_;
            }
        }
        return data_.GetAddress() + index;
    }

    iterator Erase(const_iterator pos) noexcept {
        assert(pos >= cbegin() && pos <= cend());
        size_t index = std::distance(cbegin(), pos);
        std::move(data_.GetAddress() + (index + 1), end(), data_.GetAddress() + index);
        std::destroy_n(end() - 1, 1);
        --size_;
        return data_.GetAddress() + index;
    }

    template<typename S>
    iterator Insert(const_iterator pos, S&& value) {
        return Emplace(pos, std::forward<S>(value));
    }

private:
    RawMemory<T> data_;
    size_t size_ = 0;
};
