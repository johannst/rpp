
#pragma once

namespace rpp {

template<typename T>
concept Key = Equality<T> && Hashable<T> && Movable<T>;

template<Key K, Movable V, Allocator A = Mdefault>
struct Map {
private:
    struct Slot;

public:
    Map() = default;

    explicit Map(u64 capacity) {
        capacity_ = Math::next_pow2(capacity);
        shift_ = Math::ctlz(capacity_) + 1;
        usable_ = (capacity_ / 4) * 3;
        length_ = 0;
        data_ = reinterpret_cast<Slot*>(A::alloc(capacity_ * sizeof(Slot)));
    }

    template<typename... Ss>
        requires All<Pair<K, V>, Ss...> && Move_Constructable<Pair<K, V>>
    explicit Map(Ss&&... init) {
        (insert(std::move(init.first), std::move(init.second)), ...);
    }

    Map(const Map& src) = delete;
    Map& operator=(const Map& src) = delete;

    Map(Map&& src) {
        data_ = src.data_;
        capacity_ = src.capacity_;
        length_ = src.length_;
        usable_ = src.usable_;
        shift_ = src.shift_;
        src.data_ = null;
        src.capacity_ = 0;
        src.length_ = 0;
        src.usable_ = 0;
        src.shift_ = 0;
    }
    Map& operator=(Map&& src) {
        this->~Map();
        data_ = src.data_;
        capacity_ = src.capacity_;
        length_ = src.length_;
        usable_ = src.usable_;
        shift_ = src.shift_;
        src.data_ = null;
        src.capacity_ = 0;
        src.length_ = 0;
        src.usable_ = 0;
        src.shift_ = 0;
        return *this;
    }

    ~Map() {
        if constexpr(Must_Destruct<Pair<K, V>>) {
            for(u64 i = 0; i < capacity_; i++) {
                data_[i].~Slot();
            }
        }
        A::free(data_);
        data_ = null;
        capacity_ = 0;
        length_ = 0;
        usable_ = 0;
        shift_ = 0;
    }

    template<Allocator B = A>
    Map<K, V, B> clone() const
        requires(Clone<K> || Trivial<K>) && (Clone<V> || Trivial<V>)
    {
        Map<K, V, B> ret(capacity_);
        ret.length_ = length_;
        if constexpr(Trivial<K> && Trivial<V>) {
            std::memcpy(ret.data_, data_, capacity_ * sizeof(Slot));
        } else {
            for(u64 i = 0; i < length_; i++) {
                new(&ret.data_[i]) Slot{data_[i].clone()};
            }
        }
        return ret;
    }

    void reserve(u64 new_capacity) {
        if(new_capacity <= capacity_) return;

        Slot* old_data = data_;
        u64 old_capacity = capacity_;

        capacity_ = new_capacity;
        data_ = reinterpret_cast<Slot*>(A::alloc(capacity_ * sizeof(Slot)));
        usable_ = (capacity_ / 4) * 3;
        shift_ = Math::ctlz(capacity_) + 1;

        for(u64 i = 0; i < old_capacity; i++) {
            if(old_data[i].hash != EMPTY_SLOT) insert_slot(std::move(old_data[i]));
        }
        A::free(old_data);
    }

    void grow() {
        u64 new_capacity = capacity_ ? 2 * capacity_ : 32;
        reserve(new_capacity);
    }

    void clear() {
        if constexpr(Must_Destruct<Pair<K, V>>) {
            for(u64 i = 0; i < capacity_; i++) {
                data_[i].~Slot();
            }
        } else {
            std::memset(data_, EMPTY_SLOT, capacity_ * sizeof(Slot));
        }
        length_ = 0;
    }

    bool empty() const {
        return length_ == 0;
    }
    bool full() const {
        return length_ == usable_;
    }
    u64 length() const {
        return length_;
    }

    V& insert(const K& key, V&& value)
        requires Trivial<K>
    {
        if(full()) grow();
        Slot slot{K{key}, std::move(value)};
        Slot& placed = insert_slot(std::move(slot));
        length_ += 1;
        return placed.data->second;
    }

    V& insert(K&& key, V&& value) {
        if(full()) grow();
        Slot slot{std::move(key), std::move(value)};
        Slot& placed = insert_slot(std::move(slot));
        length_ += 1;
        return placed.data->second;
    }

    template<typename... Args>
        requires Constructable<V, Args...>
    V& emplace(K&& key, Args&&... args) {
        if(full()) grow();
        Slot slot{std::move(key), V{std::forward<Args>(args)...}};
        Slot& placed = insert_slot(std::move(slot));
        length_ += 1;
        return placed.data->second;
    }

    Opt<Ref<V>> try_get(const K& key) {
        if(empty()) return {};
        u64 hash = hash_(key);
        u64 idx = hash >> shift_;
        u64 dist = 0;
        for(;;) {
            u64 k = data_[idx].hash;
            if(k == EMPTY_SLOT) return {};
            if(k == hash && data_[idx].data->first == key) {
                return Opt{Ref{data_[idx].data->second}};
            }
            u64 kidx = k >> shift_;
            u64 kdist = kidx <= idx ? idx - kidx : capacity_ + idx - kidx;
            if(kdist < dist) return {};
            dist++;
            if(++idx == capacity_) idx = 0;
        }
    }

    Opt<Ref<const V>> try_get(const K& key) const {
        if(empty()) return {};
        u64 hash = hash_(key);
        u64 idx = hash >> shift_;
        u64 dist = 0;
        for(;;) {
            u64 k = data_[idx].hash;
            if(k == EMPTY_SLOT) return {};
            if(k == hash && data_[idx].data->first == key) {
                return Opt{Ref{data_[idx].data->second}};
            }
            u64 kidx = k >> shift_;
            u64 kdist = kidx <= idx ? idx - kidx : capacity_ + idx - kidx;
            if(kdist < dist) return {};
            dist++;
            if(++idx == capacity_) idx = 0;
        }
    }

    bool try_erase(const K& key) {
        if(empty()) return false;
        u64 hash = hash_(key);
        u64 idx = hash >> shift_;
        u64 dist = 0;
        for(;;) {
            u64 k = data_[idx].hash;
            if(k == hash && data_[idx].data->first == key) {
                data_[idx].~Slot();
                fix_up(idx);
                length_ -= 1;
                return true;
            }
            u64 kidx = k >> shift_;
            u64 kdist = kidx <= idx ? idx - kidx : capacity_ + idx - kidx;
            if(kdist < dist) return false;
            dist++;
            if(++idx == capacity_) idx = 0;
        }
    }

    bool contains(const K& key) const {
        return try_get(key);
    }

    V& get(const K& key) {
        Opt<Ref<V>> value = try_get(key);
        if(!value) die("Failed to find key %!", key);
        return **value;
    }

    const V& get(const K& key) const {
        Opt<Ref<const V>> value = try_get(key);
        if(!value) die("Failed to find key %!", key);
        return **value;
    }

    void erase(const K& key) {
        if(!try_erase(key)) die("Failed to erase key %!", key);
    }

    template<bool const_>
    struct Iterator {
        using M = typename If<const_, const Map, Map>::type;

        Iterator operator++(int) {
            Iterator i = *this;
            count_++;
            skip();
            return i;
        }
        Iterator operator++() {
            count_++;
            skip();
            return *this;
        }

        Pair<const K, V>& operator*() const
            requires(!const_)
        {
            return reinterpret_cast<Pair<const K, V>&>(*map_.data_[count_].data);
        }
        const Pair<K, V>& operator*() const {
            return *map_.data_[count_].data;
        }

        Pair<const K, V>* operator->() const
            requires(!const_)
        {
            return reinterpret_cast<Pair<const K, V>*>(&*map_.data_[count_].data);
        }
        const Pair<K, V>* operator->() const {
            return &*map_.data_[count_].data;
        }

        bool operator==(const Iterator& rhs) const {
            return &map_ == &rhs.map_ && count_ == rhs.count_;
        }

    private:
        void skip() {
            while(count_ < map_.capacity_ && map_.data_[count_].hash == EMPTY_SLOT) count_++;
        }
        Iterator(M& map, u64 count) : map_(map), count_(count) {
            skip();
        }
        M& map_;
        u64 count_ = 0;

        friend struct Map;
    };

    using iterator = Iterator<false>;
    using const_iterator = Iterator<true>;

    const_iterator begin() const {
        return const_iterator(*this, 0);
    }
    const_iterator end() const {
        return const_iterator(*this, capacity_);
    }
    iterator begin() {
        return iterator(*this, 0);
    }
    iterator end() {
        return iterator(*this, capacity_);
    }

private:
    static constexpr u64 EMPTY_SLOT = 0;

    static u64 hash_(const K& k) {
        return hash(k) | 1;
    }

    Slot& insert_slot(Slot&& slot) {
        u64 idx = slot.hash >> shift_;
        Slot* placement = null;
        u64 dist = 0;
        for(;;) {
            u64 hash = data_[idx].hash;
            if(hash == EMPTY_SLOT) {
                data_[idx] = std::move(slot);
                return placement ? *placement : data_[idx];
            }
            if(hash == slot.hash && data_[idx].data->first == slot.data->first) {
                data_[idx] = std::move(slot);
                return data_[idx];
            }
            u64 hashidx = hash >> shift_;
            u64 hashdist = hashidx <= idx ? idx - hashidx : capacity_ + idx - hashidx;
            if(hashdist < dist) {
                std::swap(data_[idx], slot);
                placement = placement ? placement : &data_[idx];
                dist = hashdist;
            }
            dist++;
            if(++idx == capacity_) idx = 0;
        }
    }

    void fix_up(u64 idx) {
        for(;;) {
            u64 next = idx == capacity_ - 1 ? 0 : idx + 1;
            u64 nexthash_ = data_[next].hash;
            if(nexthash_ == EMPTY_SLOT) return;
            u64 next_ideal = nexthash_ >> shift_;
            if(next == next_ideal) return;
            data_[idx] = std::move(data_[next]);
            idx = next;
        }
    }

    struct Slot {
        Slot() = default;

        explicit Slot(K&& key, V&& value) : hash(hash_(key)) {
            data.construct(std::move(key), std::move(value));
        }
        ~Slot() {
            if constexpr(Must_Destruct<Pair<K, V>>) {
                if(hash != EMPTY_SLOT) data.destruct();
            }
            hash = EMPTY_SLOT;
        }

        Slot(const Slot&) = delete;
        Slot& operator=(const Slot&) = delete;

        Slot(Slot&& slot) {
            hash = slot.hash;
            slot.hash = EMPTY_SLOT;
            if(hash != EMPTY_SLOT) data.construct(std::move(*slot.data));
        }
        Slot& operator=(Slot&& slot) {
            this->~Slot();
            hash = slot.hash;
            slot.hash = EMPTY_SLOT;
            if(hash != EMPTY_SLOT) data.construct(std::move(*slot.data));
            return *this;
        }

        Slot clone() const
            requires(Clone<K> || Trivial<K>) && (Clone<V> || Trivial<K>)
        {
            if(hash == EMPTY_SLOT) return Slot{};
            if constexpr(Clone<K> && Clone<V>) {
                return Slot{data->first.clone(), data->second.clone()};
            } else if constexpr(Clone<K> && Trivial<V>) {
                return Slot{data->first.clone(), data->second};
            } else if constexpr(Trivial<K> && Clone<V>) {
                return Slot{data->first, data->second.clone()};
            } else {
                static_assert(Trivial<K> && Trivial<V>);
                return Slot{data->first, data->second};
            }
        }

        u64 hash = EMPTY_SLOT;
        Storage<Pair<K, V>> data;
    };

    Slot* data_ = null;
    u64 capacity_ = 0;
    u64 length_ = 0;
    u64 usable_ = 0;
    u64 shift_ = 0;

    friend struct Reflect<Map>;
    template<bool>
    friend struct Iterator;
};

template<Key K, typename V, Allocator A>
struct Reflect<Map<K, V, A>> {
    using T = Map<K, V, A>;
    static constexpr Literal name = "Map";
    static constexpr Kind kind = Kind::record_;
    using members =
        List<FIELD(data_), FIELD(capacity_), FIELD(length_), FIELD(usable_), FIELD(shift_)>;
    static_assert(Record<T>);
};

} // namespace rpp
