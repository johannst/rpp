
#pragma once

namespace rpp {

template<Ordered T, Allocator A = Mdefault>
struct Heap {

    Heap() = default;

    explicit Heap(u64 capacity) {
        data_ = reinterpret_cast<T*>(A::alloc(capacity * sizeof(T)));
        length_ = 0;
        capacity_ = capacity;
    }

    template<typename... Ss>
        requires All<T, Ss...> && Move_Constructable<T>
    explicit Heap(Ss&&... init) {
        reserve(sizeof...(Ss));
        (push(std::move(init)), ...);
    }

    explicit Heap(const Heap& src) = delete;
    Heap& operator=(const Heap& src) = delete;

    Heap(Heap&& src) {
        data_ = src.data_;
        length_ = src.length_;
        capacity_ = src.capacity_;
        src.data_ = null;
        src.length_ = 0;
        src.capacity_ = 0;
    }
    Heap& operator=(Heap&& src) {
        this->~Heap();
        data_ = src.data_;
        length_ = src.length_;
        capacity_ = src.capacity_;
        src.data_ = null;
        src.length_ = 0;
        src.capacity_ = 0;
        return *this;
    }

    ~Heap() {
        if constexpr(Must_Destruct<T>) {
            for(T& v : *this) {
                v.~T();
            }
        }
        A::free(data_);
        data_ = null;
        length_ = 0;
        capacity_ = 0;
    }

    template<Allocator B = A>
    Heap<T, B> clone() const
        requires Clone<T> || Trivial<T>
    {
        Heap<T, B> ret;
        ret.data_ = reinterpret_cast<T*>(B::alloc(capacity_ * sizeof(T)));
        ret.length_ = length_;
        ret.capacity_ = capacity_;
        if constexpr(Clone<T>) {
            for(u64 i = 0; i < length_; i++) {
                new(&ret.data_[i]) T{data_[i].clone()};
            }
        } else {
            static_assert(Trivial<T>);
            std::memcpy(ret.data_, data_, length_ * sizeof(T));
        }
        return ret;
    }

    void reserve(u64 new_capacity)
        requires Trivially_Movable<T> || Move_Constructable<T>
    {
        if(new_capacity <= capacity_) return;

        T* new_data = reinterpret_cast<T*>(A::alloc(new_capacity * sizeof(T)));
        if constexpr(Trivially_Movable<T>) {
            std::memcpy(new_data, data_, length_ * sizeof(T));
        } else {
            static_assert(Move_Constructable<T>);
            for(u64 i = 0; i < length_; i++) {
                new(&new_data[i]) T{std::move(data_[i])};
            }
        }
        A::free(data_);

        capacity_ = new_capacity;
        data_ = new_data;
    }

    void grow()
        requires Trivially_Movable<T> || Move_Constructable<T>
    {
        u64 new_capacity = capacity_ ? 2 * capacity_ : 8;
        reserve(new_capacity);
    }

    void clear() {
        if constexpr(Must_Destruct<T>) {
            for(T& v : *this) {
                v.~T();
            }
        }
        length_ = 0;
    }

    bool empty() const {
        return length_ == 0;
    }
    bool full() const {
        return length_ == capacity_;
    }
    u64 length() const {
        return length_;
    }

    void push(T&& value)
        requires Move_Constructable<T>
    {
        if(full()) grow();
        new(&data_[length_++]) T{std::move(value)};
        reheap_up(length_ - 1);
    }

    template<typename... Args>
    void emplace(Args&&... args)
        requires Constructable<T, Args...>
    {
        if(full()) grow();
        new(&data_[length_++]) T{std::forward<Args>(args)...};
        reheap_up(length_ - 1);
    }

    void pop()
        requires Trivially_Movable<T> || Move_Constructable<T>
    {
        assert(length_ > 0);

        if constexpr(Must_Destruct<T>) {
            data_[0].~T();
        }
        length_--;

        if(length_ > 0) {
            if constexpr(Trivially_Movable<T>) {
                std::memcpy(data_, data_ + length_, sizeof(T));
            } else {
                static_assert(Move_Constructable<T>);
                new(data_) T{std::move(data_[length_])};
            }
            reheap_down(0);
        }
    }

    T& top() {
        return data_[0];
    }
    const T& top() const {
        return data_[0];
    }

    const T* begin() const {
        return data_;
    }
    const T* end() const {
        return data_ + length_;
    }
    T* begin() {
        return data_;
    }
    T* end() {
        return data_ + length_;
    }

private:
    void swap(u64 a, u64 b)
        requires Move_Constructable<T>
    {
        T temp{std::move(data_[a])};
        new(&data_[a]) T{std::move(data_[b])};
        new(&data_[b]) T{std::move(temp)};
    }

    void reheap_up(u64 idx)
        requires Move_Constructable<T>
    {
        while(idx) {
            u64 parent_idx = (idx - 1) / 2;
            T& elem = data_[idx];
            T& parent = data_[parent_idx];
            if(elem < parent) {
                swap(idx, parent_idx);
                idx = parent_idx;
            } else {
                return;
            }
        }
    }

    void reheap_down(u64 idx)
        requires Move_Constructable<T>
    {
        while(true) {
            T& parent = data_[idx];

            u64 left = idx * 2 + 1;
            u64 right = left + 1;

            if(right < length_) {
                T& lchild = data_[left];
                T& rchild = data_[right];
                if(lchild < parent && !(rchild < lchild)) {
                    swap(idx, left);
                    idx = left;
                } else if(rchild < parent && !(lchild < rchild)) {
                    swap(idx, right);
                    idx = right;
                } else {
                    return;
                }
            } else if(left < length_) {
                T& lchild = data_[left];
                if(lchild < parent) {
                    swap(idx, left);
                }
                return;
            } else {
                return;
            }
        }
    }

    T* data_ = null;
    u64 length_ = 0;
    u64 capacity_ = 0;

    friend struct Reflect<Heap>;
};

template<typename H, Allocator A>
struct Reflect<Heap<H, A>> {
    using T = Heap<H, A>;
    static constexpr Literal name = "Heap";
    static constexpr Kind kind = Kind::record_;
    using members = List<FIELD(data_), FIELD(length_), FIELD(capacity_)>;
    static_assert(Record<T>);
};

} // namespace rpp