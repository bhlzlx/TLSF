#pragma once 

/***************************************************
** Two Level Segregated Fit memory allocator
** Written by bhlzlx@gmail.com ( lixin )
** 
** Copyright (c) 2020, bhlzlx@gmail.com
** All rights reserved.
****************************************************/

#include <new>

namespace ugi {

    class TLSFPool {
    private:
        void*       _memptr;
        size_t      _capacity;
    public:
        TLSFPool() 
            : _memptr(nullptr)
            , _capacity(0)
        {
        }
        TLSFPool( void* ptr, size_t capacity )
            : _memptr(ptr)
            , _capacity(capacity)
        {
        }
        TLSFPool( TLSFPool& pool ) {
            _memptr = pool._memptr;
            _capacity = pool._capacity;
        }
        TLSFPool( TLSFPool&& pool) noexcept {
            _memptr = pool._memptr;
            _capacity = pool._capacity;
            pool._capacity = 0;
            pool._memptr = nullptr;
        }
        TLSFPool& operator =(TLSFPool&& pool) noexcept {
            _memptr = pool._memptr;
            _capacity = pool._capacity;
            return *this;
        }
        inline bool check_next_contains(void* ptr) const {
            return ptr < (uint8_t*)_memptr + _capacity;
        }
        inline bool contains( void* ptr ) const {
            return ptr>=_memptr && ptr< ((uint8_t*)_memptr + _capacity) ;
        }
        inline size_t capacity() const {
            return _capacity;
        }
        inline void* ptr() const {
            return _memptr;
        }
        inline void* endPtr() const {
            return (uint8_t*)_memptr + _capacity;
        }
        static TLSFPool createPool( size_t capacity ) {
            struct alignas(16) AlignType {
                alignas(16) uint32_t data[4];
            };
            capacity = (capacity + 15ULL) & ~(15ULL);
            void* ptr = new AlignType[capacity>>4];
            if(!ptr) {
                return TLSFPool(nullptr, 0);
            }
            return TLSFPool(ptr, capacity);
        }
    };

    /* simple vector/array implementation for TLSF*/

    template< class T >
    class TLSFVector {
    private:
        T*          _data;
        size_t      _size;
        size_t      _capacity;
    public:
        TLSFVector( size_t size ) {
            _capacity = (size_t)ceil(log2(size));
            _data = new T[_capacity];
            _size = 0;
        }
        template< class ...ARGS >
        void emplace_back( ARGS&& ...args ) {
            if (_size == _capacity) {
                auto data = new T[_capacity*2];
                _capacity <<= 1;
                for (size_t i = 0; i < _size; ++i) {
                    data[i] = std::move(_data[i]);
                }
                free(_data);
                _data = (T*)data;
            }
            new(&_data[_size])T(std::forward<ARGS>(args)...);
            ++_size;
        }
        size_t size() const {
            return _size;
        }
        const T* begin() const {
            return _data;
        }
        const T* end() const {
            return  _data + _size;            
        }
        ~TLSFVector() {
            if (_data) {
                delete[]_data;
            }
        }
    };

    template< class T, size_t SIZE>
    class TLSFArray {
    private:
        T            _data[SIZE] = {};
    public:
        TLSFArray() {}
        T& operator[](size_t index) {
            return _data[index];
        }
        const T& operator[](size_t index) const {
            return _data[index];
        }
    };

    struct AllocHeader {

        static constexpr size_t TrueSize = 16;
        static constexpr size_t FullSize = 32;

        alignas(sizeof(size_t))     AllocHeader*            prevPhyAlloc;
        struct alignas(sizeof(size_t)) {
            size_t                                          size:31;    // 这里是为了省内存
            size_t                                          free:1;
            //size_t                                          flags:1;    // 本来可能会觉得除了free还有其它属性目前发现不需要其它属性了，只需要Free就够了
        };
        // == 下边这两个属性在被分配之后就是无效状态了，即存用户数据
        // 而在可分配状态下，这两个值是占有空间的，假如我们要将某块内存前后空闲且连续的内存合并，就需要下边这两个指针
        alignas(sizeof(size_t))     AllocHeader*            prevFreeAlloc;
        alignas(sizeof(size_t))     AllocHeader*            nextFreeAlloc;
        //
        void initForSplit( size_t newSize, AllocHeader* prevPhysic ) {
            size = newSize;
            free = 1;
            prevPhyAlloc = prevPhysic;
        }
        inline void* ptr() {
            return ((uint8_t*)this) + TrueSize;
        }
        inline AllocHeader* nextPhyAllocation() {
            return (AllocHeader*)(((uint8_t*)this) + TrueSize + size);
        }
        static AllocHeader* fromPtr( void* ptr ) {
            return (AllocHeader*)(((uint8_t*)ptr) - TrueSize);
        }
    };
    static_assert( AllocHeader::TrueSize == sizeof(AllocHeader) - sizeof(void*)*2, "must be true" );
    static_assert( AllocHeader::FullSize == sizeof(AllocHeader), "must be true" );
}