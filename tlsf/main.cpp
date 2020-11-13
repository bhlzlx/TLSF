/***************************************************
** Two Level Segregated Fit memory allocator
** Written by bhlzlx@gmail.com ( lixin )
** 
** Copyright (c) 2020, bhlzlx@gmail.com
** All rights reserved.
****************************************************/

#include <cstdio>
#include <cstdint>
#include <map>
#include <string>
#include <random>
#include <chrono>
#include <set>

#include "tlsf.hpp"
#include "../MemoryAllocator.h"

class TLSF_kusugawa {
private:
    ugi::TLSF           _tlsf;
    uint8_t*            _memptr;
    size_t              _miniSize;
    size_t              _capacity;
public:
    TLSF_kusugawa()
        : _tlsf()
        , _memptr( nullptr )
        , _miniSize(16)
    {}

    bool initialize( size_t capacity, size_t minimiumSize ) {
        void* ptr = malloc(capacity);
        ugi::TLSFPool pool(ptr, capacity);
        _tlsf.initialize(pool);
        //
        _miniSize = minimiumSize;
        _capacity = capacity;
        return true;
    }

    void* alloc( size_t size)  {
        return _tlsf.alloc(size);
    }

    void free( void* ptr ) {
        _tlsf.free( ptr );
    }

    bool contains( void* ptr ) {
        return true;
    }

    void dump() {
        _tlsf.dump();
    }
};

int main() {

    constexpr uint32_t capacity = 1024 * 1024;
    constexpr uint32_t minimum = 128;

	ugi::MemoryAllocator<TLSF_kusugawa> allocator;
    allocator.initialize(capacity, minimum);
    std::default_random_engine randEngine;
    std::uniform_int_distribution<uint32_t> randRange(96, 1024);

    uint64_t allocateTime = 0;
    uint64_t freeTime = 0;
    uint32_t allocateCount = 0;
    uint32_t allocatedTotal = 0;

    std::set<void*> pointers;
    uint32_t randSize = randRange(randEngine);
    std::chrono::steady_clock::time_point startTime;
    std::chrono::steady_clock::time_point endTime;
    startTime = std::chrono::high_resolution_clock::now();
    auto ptr = allocator.alloc(randSize);
    endTime = std::chrono::high_resolution_clock::now();
    while(ptr) {
        allocatedTotal += randSize;
        auto duration = endTime - startTime;
        allocateTime += duration.count();
        pointers.insert(ptr);
        randSize = randRange(randEngine);
        randSize = (randSize + 15)&~(15);
        startTime = std::chrono::high_resolution_clock::now();
        ptr = allocator.alloc(randSize);
        endTime = std::chrono::high_resolution_clock::now();
    }
    allocateCount = pointers.size();
    allocator.dump();
    //===================================================================
    while(!pointers.empty()) {
        auto size = pointers.size();
        std::uniform_int_distribution<uint32_t> range(0, size-1);
        auto position = range(randEngine);
        auto iter = pointers.begin();
        for(size_t i = 0; i<position; ++i) {
            ++iter;
        }
        startTime = std::chrono::high_resolution_clock::now();
        allocator.free(*iter);
        endTime = std::chrono::high_resolution_clock::now();
        auto duration = endTime - startTime;
        freeTime += duration.count();
        pointers.erase(iter);
    }

    printf("count : %u\n", allocateCount);
    printf("allocate : %f ms\n", (float)allocateTime/allocateCount/1000000.0f);
    printf("free : %f ms\n", (float)freeTime / allocateCount / 1000000.0f);
    printf("utilization ratio: %f", (float)allocatedTotal/capacity);
    
	return 0;
}

//#include <cstdio>
//#include "tlsf.h"
//
//int main() {
//    ugi::TLSF tlsf;
//    constexpr size_t poolSize = 256;
//    void* ptr = malloc(poolSize);
//    ugi::TLSFPool pool(ptr, poolSize);
//    tlsf.initialize(pool);
//    void* ptr1 = tlsf.alloc(128);
//    void* ptr2 = tlsf.alloc(48);
//    void* ptr3 = tlsf.alloc(56);
//    void* ptr4 = tlsf.alloc(16);
//    void* ptr5 = tlsf.alloc(55);
//    //
//    tlsf.free(ptr2);
//    tlsf.free(ptr4);
//	tlsf.free(ptr1);
//    ptr5 = tlsf.alloc(64);
//    //
//    return 0;
//}