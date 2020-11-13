#pragma once

namespace ugi {

    template <class AllocatorType>
    class MemoryAllocator {
    private:
        AllocatorType       _allocator;
    public:
        template< class ...ARGS >
        bool initialize( ARGS&& ...args ) {
            return _allocator.initialize( std::forward<ARGS>(args)... );
        }
        void* alloc( size_t size ) {
            return _allocator.alloc(size);
        }
        void free( void* ptr ) {
            _allocator.free(ptr);
        }
        bool contains( void* ptr ) {
            return _allocator.contains(ptr);
        }
        void dump() {
            _allocator.dump();
        }
    };

}