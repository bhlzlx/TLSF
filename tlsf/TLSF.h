#pragma once

/***************************************************
** Two Level Segregated Fit memory allocator
** Written by bhlzlx@gmail.com ( lixin )
** 
** Copyright (c) 2020, bhlzlx@gmail.com
** All rights reserved.
****************************************************/

#include <cstdint>
#include <cassert>
#include <cstdio>
#include <cmath>
#include <utility>

#include "fls.h"
#include "TLSFUtility.h"

#define TLSF_DEBUG_ASSERT 0

namespace ugi {

    class TLSF {
    public:
        constexpr static size_t MinimiumAllocationSize = 16;                                // minimium allocation & alignment size
        constexpr static size_t FLC = sizeof(uint32_t) * 8 - 1;								// first level count max
        constexpr static size_t SLI = 5;                                                    // second level index bit count
        constexpr static size_t SLC = 1 << SLI;                                             // count of the segments per-first level
        constexpr static size_t FLM = MinimiumAllocationSize<<SLI;                          // first level max
        uint32_t BasePowLevel = tlsf_fls_sizet(FLM);

    private:
        struct BitmapLevel{
            union {
                alignas(4) uint32_t val;
                struct {
                    alignas(2) uint16_t firstLevel;
                    alignas(2) uint16_t secondLevel;
                };
            };
            inline bool valid() {
                return val != 0xffffffff;
            }
            BitmapLevel()
                : val(0xffffffff)
            {}
            BitmapLevel( uint16_t f, uint16_t s)
                : firstLevel(f), secondLevel(s)
            {}
        };
    private:
        uint32_t                                            _firstLevelBitmap;      // 4GB * 16 = 64 GB Maximium
        TLSFArray<uint32_t, 31>                             _secondLevelBitmap;     //
        TLSFArray< TLSFArray<AllocHeader*, SLC>, 31>        _allocationLinkTable;   //
        TLSFVector<TLSFPool>                                _memoryPools;
    public:
        TLSF()
            : _firstLevelBitmap(0)
            , _secondLevelBitmap{}
            , _allocationLinkTable{}
            , _memoryPools{4}
        {}

        // 每一级可以分配一定范围的大小，所以里面所有的块
		BitmapLevel queryBitmapLevelForAlloc(size_t size);

		BitmapLevel queryBitmapLevelForInsert(size_t size);

		size_t queryLevelSize(BitmapLevel level);

		size_t queryAlignedLevelSize(size_t size);

        //  看这个级别是不是有空闲块
		inline bool queryFreeStatus(BitmapLevel level);
        // 
		AllocHeader* queryFreeAllocation(size_t size);

		BitmapLevel findLevelForSplit(BitmapLevel baseLevel);

        // 给定一个bitmap level（确信它一定有空闲块），
        // 取出来空闲块分割出指定大小（size)的块，并返回，剩下的块插入到合适的位置
		AllocHeader* splitAllocation(BitmapLevel level, size_t size);

		AllocHeader* queryAllocationWithFreeLevel(BitmapLevel level);

		void removeFreeAllocationAndUpdateBitmap(AllocHeader* allocation);

		void removeFreeAllocationAndUpdateBitmap(AllocHeader* allocation, BitmapLevel level);

        // allocation : 要回收的块 
        // 分割的情况下我们不需要合并自由块
        // mergeCheck 是因为我们会在两种情况下调用这个方法，一个是回收内存，一个是分割大块剩下一个小块
		void insertFreeAllocation(AllocHeader* allocation, bool mergeCheck = false, const TLSFPool* pool = nullptr);

		// locate the pool which contains the allocation
		inline const TLSFPool* locatePool(AllocHeader* allocation) {
			const TLSFPool* allocPool = nullptr;
			for (const auto& pool : _memoryPools) {
				if (pool.contains(allocation)) {
					allocPool = &pool;
				}
			}
			assert(allocPool);
			return allocPool;
		}
    public:
		bool initialize(TLSFPool pool);
        // ===============================================
		void* alloc(size_t size);

		void* realloc(void* ptr, size_t size);

		void free(void* ptr);

		void dump();
    };

}