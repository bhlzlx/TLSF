#include "TLSF.h"

namespace ugi {
	// 每一级可以分配一定范围的大小，所以里面所有的块

	TLSF::BitmapLevel TLSF::queryBitmapLevelForAlloc(size_t size) {
		TLSF::BitmapLevel level;
		if (size <= FLM) {
			level.firstLevel = 0;
			level.secondLevel = (uint16_t)(size / MinimiumAllocationSize);
			if (!(size & (MinimiumAllocationSize - 1))) {
				--level.secondLevel;
			}
			return level;
		}
		else {
			level.firstLevel = tlsf_fls_sizet(size);
			size_t levelMin = 1ULL << level.firstLevel;
			size_t segmentSize = levelMin >> SLI;
			size += (segmentSize - 1);
			size_t alignSize = (1ULL << (level.firstLevel - SLI));
			level.secondLevel = (uint16_t)((size - levelMin) / alignSize);
			if (level.secondLevel) {
				--level.secondLevel;
			}
			else {
				--level.firstLevel;
				level.secondLevel = SLC - 1;
			}
			level.firstLevel -= (BasePowLevel - 1);
			return level;
		}
	}
	TLSF::BitmapLevel TLSF::queryBitmapLevelForInsert(size_t size) {
		TLSF::BitmapLevel level;
		if (size <= FLM) {
			level.firstLevel = 0;
			level.secondLevel = (uint16_t)(size / MinimiumAllocationSize) - 1;
		}
		else {
			level.firstLevel = tlsf_fls_sizet(size);
			size_t levelMin = 1ULL << level.firstLevel;
			level.secondLevel = (uint16_t)((size - levelMin) / (levelMin >> SLI));
			if (level.secondLevel == 0) {
				--level.firstLevel;
				level.secondLevel = SLC - 1;
			}
			else {
				--level.secondLevel;
			}
			level.firstLevel -= level.secondLevel == 0 ? 1 : 0;
			--level.secondLevel;
			level.secondLevel &= (SLC - 1);
			level.firstLevel -= (BasePowLevel - 1);
		}
		return level;
	}
	size_t TLSF::queryLevelSize(TLSF::BitmapLevel level) {
		if (level.firstLevel) {
			size_t firstLevelSize = 1ULL << (level.firstLevel + BasePowLevel - 1);
			size_t rst = firstLevelSize + (firstLevelSize >> SLI)*(level.secondLevel + 1);
			return rst;
		}
		else {
			return ((size_t)level.secondLevel + 1) * MinimiumAllocationSize;
		}
	}
	size_t TLSF::queryAlignedLevelSize(size_t size) {
		auto level = queryBitmapLevelForAlloc(size);
		auto levelSize = queryLevelSize(level);
		return levelSize;
	}

	//  看这个级别是不是有空闲块
	bool TLSF::queryFreeStatus(TLSF::BitmapLevel level) {
		if (!_firstLevelBitmap & (1 << level.firstLevel)) {
			return false;
		}
		uint32_t rst = _secondLevelBitmap[level.firstLevel] & (1 << level.secondLevel);
		if (rst != 0) {
			return true;
		}
		return false;
	}
	// 
	AllocHeader * TLSF::queryFreeAllocation(size_t size) {
		TLSF::BitmapLevel level = queryBitmapLevelForAlloc(size);
		if (queryFreeStatus(level)) { // 恰好有空间块可以分配
			auto allocation = queryAllocationWithFreeLevel(level);
			return allocation;
		}
		else { // 没有合适的内存块分配，找一个可分割的大些的内存块
			size = queryLevelSize(level);
			if (++level.secondLevel >= SLC) {
				++level.firstLevel;
				level.secondLevel = 0;
			}
			// TLSF::BitmapLevel baseLevelForSplit = queryBitmapLevel(size + AllocHeader::TrueSize + MinAllocSize );
			TLSF::BitmapLevel splitLevel = findLevelForSplit(level);
			if (!splitLevel.valid()) {
				return nullptr; // 找不着合适的块了，不能再分配了
			}
			// 拿着合适的块分割，再分配
			AllocHeader* allocation = splitAllocation(splitLevel, size);
			return allocation;
		}
	}
	
	TLSF::BitmapLevel TLSF::findLevelForSplit(TLSF::BitmapLevel baseLevel) {

		for (uint16_t firstLevel = baseLevel.firstLevel; firstLevel < FLC; ++firstLevel) {
			if (!(_firstLevelBitmap&(1 << firstLevel))) {
				baseLevel.secondLevel = 0;
				continue;
			}
			for (uint16_t secondLevel = baseLevel.secondLevel; secondLevel<SLC; ++secondLevel) {
				TLSF::BitmapLevel acquiredLevel = { firstLevel, secondLevel };
				if (queryFreeStatus(acquiredLevel)) {
					return acquiredLevel;
				}
			}
		}
		return TLSF::BitmapLevel();
	}

	// 给定一个bitmap level（确信它一定有空闲块），
	// 取出来空闲块分割出指定大小（size)的块，并返回，剩下的块插入到合适的位置

	AllocHeader * TLSF::splitAllocation(TLSF::BitmapLevel level, size_t size) {
		AllocHeader* targetAlloc = queryAllocationWithFreeLevel(level);
		assert(targetAlloc && "it must not be nullptr!");
		assert(targetAlloc->size >= size);
		removeFreeAllocationAndUpdateBitmap(targetAlloc, level);
		if (targetAlloc->size - size < AllocHeader::TrueSize + MinimiumAllocationSize) {
			return targetAlloc; // 剩余的太小了，就不分割了
		}
		// splited free allocation
		auto nextNextPhyAlloc = targetAlloc->nextPhyAllocation();
		const TLSFPool* pool = locatePool(targetAlloc);
		size_t splitedSize = targetAlloc->size - size - AllocHeader::TrueSize;
		targetAlloc->size = size;
		AllocHeader* nextAlloc = targetAlloc->nextPhyAllocation();
		//nextAlloc->initForSplit(splitedSize, targetAlloc);
		nextAlloc->size = splitedSize;
		nextAlloc->free = 1;
		nextAlloc->prevPhyAlloc = targetAlloc;
		// insert the free allocation to list
		if (pool->check_next_contains(nextNextPhyAlloc)) {
			nextNextPhyAlloc->prevPhyAlloc = nextAlloc;
		}
		insertFreeAllocation(nextAlloc);
#if TLSF_DEBUG_ASSERT
		if (pool->check_next_contains(nextNextPhyAlloc)) {
			assert(nextNextPhyAlloc->prevPhyAlloc == nextAlloc);
			assert(nextAlloc->nextPhyAllocation() == nextNextPhyAlloc);
		}
#endif
		return targetAlloc;
	}
	AllocHeader * TLSF::queryAllocationWithFreeLevel(TLSF::BitmapLevel level) {
		AllocHeader** levelHeaderPtr = &_allocationLinkTable[level.firstLevel][level.secondLevel];
		AllocHeader* originHeader = *levelHeaderPtr;
		assert(originHeader && "it must not be nullptr!");
		AllocHeader* nextFreeAlloc = originHeader->nextFreeAlloc;
		*levelHeaderPtr = nextFreeAlloc;
		if (nextFreeAlloc) {
			nextFreeAlloc->prevFreeAlloc = nullptr;
		}
		else {
			// 如果这一级分配了之后就没有空间的内存块了，那么更新 bitmap
			_secondLevelBitmap[level.firstLevel] &= ~(1 << level.secondLevel);
			if (0 == _secondLevelBitmap[level.firstLevel]) {
				_firstLevelBitmap &= ~(1 << level.firstLevel);
			}
		}
#if TLSF_DEBUG_ASSERT
		if (originHeader->prevPhyAlloc) {
			assert(!originHeader->prevPhyAlloc->free);
		}
#endif
		return originHeader;
	}
	void TLSF::removeFreeAllocationAndUpdateBitmap(AllocHeader * allocation) {
		TLSF::BitmapLevel level = queryBitmapLevelForInsert(allocation->size);
		AllocHeader** levelHeaderPtr = &_allocationLinkTable[level.firstLevel][level.secondLevel];
		AllocHeader* nextFreeAlloc = allocation->nextFreeAlloc; // could be nullptr
		AllocHeader* prevFreeAlloc = allocation->prevFreeAlloc;

		if (prevFreeAlloc) {
			prevFreeAlloc->nextFreeAlloc = allocation->nextFreeAlloc;
		}
		else {
			*levelHeaderPtr = nextFreeAlloc;
		}
		if (nextFreeAlloc) {
			nextFreeAlloc->prevFreeAlloc = prevFreeAlloc;
		}
		if (nullptr == *levelHeaderPtr) { // 需要更新bitmap
			_secondLevelBitmap[level.firstLevel] &= ~(1 << level.secondLevel);
			if (0 == _secondLevelBitmap[level.firstLevel]) {
				_firstLevelBitmap &= ~(1 << level.firstLevel);
			}
		}
	}
	void TLSF::removeFreeAllocationAndUpdateBitmap(AllocHeader * allocation, TLSF::BitmapLevel level) /*pass*/ {
		AllocHeader** levelHeaderPtr = &_allocationLinkTable[level.firstLevel][level.secondLevel];
		AllocHeader* nextFreeAlloc = allocation->nextFreeAlloc; // could be nullptr
		AllocHeader* prevFreeAlloc = allocation->prevFreeAlloc;

		if (prevFreeAlloc) {
			prevFreeAlloc->nextFreeAlloc = allocation->nextFreeAlloc;
		}
		else {
			*levelHeaderPtr = nextFreeAlloc;
		}
		if (nextFreeAlloc) {
			nextFreeAlloc->prevFreeAlloc = prevFreeAlloc;
		}
		if (nullptr == *levelHeaderPtr) { // 需要更新bitmap
			_secondLevelBitmap[level.firstLevel] &= ~(1 << level.secondLevel);
			if (0 == _secondLevelBitmap[level.firstLevel]) {
				_firstLevelBitmap &= ~(1 << level.firstLevel);
			}
		}
	}

	// allocation : 要回收的块 
	// 分割的情况下我们不需要合并自由块
	// mergeCheck 是因为我们会在两种情况下调用这个方法，一个是回收内存，一个是分割大块剩下一个小块

	void TLSF::insertFreeAllocation(AllocHeader * allocation, bool mergeCheck, const TLSFPool * pool) /*pass*/ {
		TLSF::BitmapLevel level;
		if (mergeCheck) {
			assert(pool);
			AllocHeader* prevPhyAlloc = allocation->prevPhyAlloc;
			AllocHeader* nextPhyAlloc = allocation->nextPhyAllocation();
			///////////////// AllocHeader* mergedAlloc = allocation;
			if (prevPhyAlloc && prevPhyAlloc->free) {
				removeFreeAllocationAndUpdateBitmap(prevPhyAlloc);
				prevPhyAlloc->size += (allocation->size + AllocHeader::TrueSize);
				allocation = prevPhyAlloc;
			}
			if (pool->check_next_contains(nextPhyAlloc)) {
				if (nextPhyAlloc->free) {
					removeFreeAllocationAndUpdateBitmap(nextPhyAlloc);
					allocation->size += (nextPhyAlloc->size + AllocHeader::TrueSize);
					auto nextNextAlloc = nextPhyAlloc->nextPhyAllocation();
					if (pool->check_next_contains(nextNextAlloc)) {
						nextNextAlloc->prevPhyAlloc = allocation;
						assert(!nextNextAlloc->free);
						assert(allocation->nextPhyAllocation() == nextNextAlloc);
					}
				}
				else {
					nextPhyAlloc->prevPhyAlloc = allocation;
				}
			}
			// 为合并的 allocation 找个位置
			// allocation = mergedAlloc;
#if TLSF_DEBUG_ASSERT
			auto next = allocation->nextPhyAllocation();
			if (pool->check_next_contains(next)) {
				assert(allocation == next->prevPhyAlloc);
			}
			auto prev = allocation->prevPhyAlloc;
			if (prev) {
				assert(prev->nextPhyAllocation() == allocation);
			}
#endif
		}
		level = queryBitmapLevelForInsert(allocation->size);
		AllocHeader** levelHeaderPtr = &_allocationLinkTable[level.firstLevel][level.secondLevel];
		AllocHeader* originHeader = *levelHeaderPtr;
		*levelHeaderPtr = allocation;
		allocation->nextFreeAlloc = originHeader;
		allocation->prevFreeAlloc = nullptr;
		if (!originHeader) { // update bitmap if need
			_secondLevelBitmap[level.firstLevel] |= 1 << level.secondLevel;
			_firstLevelBitmap |= 1 << (level.firstLevel);
		}
		else {
			originHeader->prevFreeAlloc = allocation;
		}
	}
	bool TLSF::initialize(TLSFPool pool) {
		size_t capacity = pool.capacity();
		AllocHeader* allocation = (AllocHeader*)pool.ptr();
		//allocation->prevPhyAlloc = nullptr;
		//allocation->prevFreeAlloc = nullptr;
		// allocation->initForSplit(capacity - AllocHeader::TrueSize, nullptr);
		allocation->size = capacity - AllocHeader::TrueSize;
		allocation->free = 1;
		allocation->prevPhyAlloc = nullptr;
		insertFreeAllocation(allocation);
		_memoryPools.emplace_back(pool.ptr(), pool.capacity());
		return true;
	}

	// ===============================================
	void * TLSF::alloc(size_t size) {
		auto allocation = queryFreeAllocation(size);
		if (!allocation) {
			return nullptr;
		}
		else {
			// allocation->setFree(false);
			allocation->free = 0;
#if TLSF_DEBUG_ASSERT
			auto pool = locatePool(allocation);
			auto next = allocation->nextPhyAllocation();
			if (pool->check_next_contains(next)) {
				assert(allocation == next->prevPhyAlloc);
			}
			auto prev = allocation->prevPhyAlloc;
			if (prev) {
				assert(prev->nextPhyAllocation() == allocation);
			}
#endif
			return allocation->ptr();
		}
	}
	void * TLSF::realloc(void * ptr, size_t size) {
		AllocHeader* allocation = (AllocHeader*)((uint8_t*)ptr - AllocHeader::TrueSize);
		const TLSFPool* allocPool = nullptr;
		for (const auto& pool : _memoryPools) {
			if (pool.contains(allocation)) {
				allocPool = &pool;
			}
		}
		assert(allocPool);
		AllocHeader* nextPhyAlloc = allocation->nextPhyAllocation();
		if ((void*)nextPhyAlloc >= allocPool->endPtr()) {
			nextPhyAlloc = nullptr;
		}
		if (nextPhyAlloc && nextPhyAlloc->free) {
			size_t alignedLevelSize = queryAlignedLevelSize(size);
			size_t mergedSize = nextPhyAlloc->size + AllocHeader::TrueSize + allocation->size;
			if ((mergedSize >= size) && (mergedSize < alignedLevelSize)) {
				removeFreeAllocationAndUpdateBitmap(nextPhyAlloc);
				allocation->size = mergedSize;
				return ptr;
			}
		}
		insertFreeAllocation(allocation, true, allocPool); // 回收旧的，分配新的
		return alloc(size);
	}
	void TLSF::free(void * ptr) {
		AllocHeader* allocation = (AllocHeader*)((uint8_t*)ptr - AllocHeader::TrueSize);
		// allocation->setFree(true);
		allocation->free = 1;
		const TLSFPool* allocPool = nullptr;
		for (const auto& pool : _memoryPools) {
			if (pool.contains(allocation)) {
				allocPool = &pool;
			}
		}
		assert(allocPool);
		insertFreeAllocation(allocation, true, allocPool);
	}

	void TLSF::dump() {
		size_t allocCount = 0;
		size_t freeCount = 0;
		size_t freeSize = 0;
		for (auto& pool : _memoryPools) {
			auto a = (AllocHeader*)pool.ptr();
			while (pool.check_next_contains(a)) {
				++allocCount;
				if (a->free) {
					++freeCount;
					freeSize += a->size;
				}
				a = a->nextPhyAllocation();
			}
		}
		printf("allocation count : %zu \nfree count: %zu\n free size: %zu\n", allocCount, freeCount, freeSize);
	}
}