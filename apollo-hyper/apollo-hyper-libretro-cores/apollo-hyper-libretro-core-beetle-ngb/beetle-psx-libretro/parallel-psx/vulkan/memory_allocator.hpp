/* Copyright (c) 2017-2018 Hans-Kristian Arntzen
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef FRAMEWORK_MEMORY_ALLOCATOR_HPP
#define FRAMEWORK_MEMORY_ALLOCATOR_HPP

#include "intrusive.hpp"
#include "object_pool.hpp"
#include "intrusive_list.hpp"
#include "vulkan.hpp"
#include <assert.h>
#include <memory>
#include <stddef.h>
#include <stdint.h>
#include <vector>

#ifdef GRANITE_VULKAN_MT
#include <mutex>
#endif

namespace Vulkan
{
static inline uint32_t log2_integer(uint32_t v)
{
	v--;
	return 32 - leading_zeroes(v);
}

enum MemoryClass
{
	MEMORY_CLASS_SMALL = 0,
	MEMORY_CLASS_MEDIUM,
	MEMORY_CLASS_LARGE,
	MEMORY_CLASS_HUGE,
	MEMORY_CLASS_COUNT
};

enum AllocationTiling
{
	ALLOCATION_TILING_LINEAR = 0,
	ALLOCATION_TILING_OPTIMAL,
	ALLOCATION_TILING_COUNT
};

enum MemoryAccessFlag
{
	MEMORY_ACCESS_WRITE_BIT = 1,
	MEMORY_ACCESS_READ_BIT = 2,
	MEMORY_ACCESS_READ_WRITE_BIT = MEMORY_ACCESS_WRITE_BIT | MEMORY_ACCESS_READ_BIT
};
using MemoryAccessFlags = uint32_t;

struct DeviceAllocation;
class DeviceAllocator;

class Block
{
public:
	enum
	{
		NumSubBlocks = 32u,
		AllFree = ~0u
	};

	Block(const Block &) = delete;
	void operator=(const Block &) = delete;

	Block()
	{
		for (auto &v : free_blocks)
			v = AllFree;
		longest_run = 32;
	}

	~Block()
	{
		if (free_blocks[0] != AllFree)
			LOGE("Memory leak in block detected.\n");
	}

	inline bool full() const
	{
		return free_blocks[0] == 0;
	}

	inline bool empty() const
	{
		return free_blocks[0] == AllFree;
	}

	inline uint32_t get_longest_run() const
	{
		return longest_run;
	}

	void allocate(uint32_t num_blocks, DeviceAllocation *block);
	void free(uint32_t mask);

private:
	uint32_t free_blocks[NumSubBlocks];
	uint32_t longest_run = 0;

	inline void update_longest_run()
	{
		uint32_t f = free_blocks[0];
		longest_run = 0;

		while (f)
		{
			free_blocks[longest_run++] = f;
			f &= f >> 1;
		}
	}
};

struct MiniHeap;
class ClassAllocator;
class DeviceAllocator;
class Allocator;

struct DeviceAllocation
{
	friend class ClassAllocator;
	friend class Allocator;
	friend class Block;
	friend class DeviceAllocator;

public:
	inline VkDeviceMemory get_memory() const
	{
		return base;
	}

	inline bool allocation_is_global() const
	{
		return !alloc && base;
	}

	inline uint32_t get_offset() const
	{
		return offset;
	}

	inline uint32_t get_size() const
	{
		return size;
	}

	inline uint32_t get_mask() const
	{
		return mask;
	}

	void free_immediate();
	void free_immediate(DeviceAllocator &allocator);

	static DeviceAllocation make_imported_allocation(VkDeviceMemory memory, VkDeviceSize size, uint32_t memory_type);

private:
	VkDeviceMemory base = VK_NULL_HANDLE;
	uint8_t *host_base = nullptr;
	ClassAllocator *alloc = nullptr;
	Util::IntrusiveList<MiniHeap>::Iterator heap = {};
	uint32_t offset = 0;
	uint32_t mask = 0;
	uint32_t size = 0;

	uint8_t tiling = 0;
	uint8_t memory_type = 0;
	bool hierarchical = false;

	void free_global(DeviceAllocator &allocator, uint32_t size, uint32_t memory_type);

	inline uint8_t *get_host_memory() const
	{
		return host_base;
	}
};

struct MiniHeap : Util::IntrusiveListEnabled<MiniHeap>
{
	DeviceAllocation allocation;
	Block heap;
};

class Allocator;

class ClassAllocator
{
public:
	friend class Allocator;
	~ClassAllocator();

	inline void set_tiling_mask(uint32_t mask)
	{
		tiling_mask = mask;
	}

	inline void set_sub_block_size(uint32_t size)
	{
		sub_block_size_log2 = log2_integer(size);
		sub_block_size = size;
	}

	bool allocate(uint32_t size, AllocationTiling tiling, DeviceAllocation *alloc, bool hierarchical);
	void free(DeviceAllocation *alloc);

private:
	ClassAllocator() = default;
	struct AllocationTilingHeaps
	{
		Util::IntrusiveList<MiniHeap> heaps[Block::NumSubBlocks];
		Util::IntrusiveList<MiniHeap> full_heaps;
		uint32_t heap_availability_mask = 0;
	};
	ClassAllocator *parent = nullptr;
	AllocationTilingHeaps tiling_modes[ALLOCATION_TILING_COUNT];
	Util::ObjectPool<MiniHeap> object_pool;

	uint32_t sub_block_size = 1;
	uint32_t sub_block_size_log2 = 0;
	uint32_t tiling_mask = ~0u;
	uint32_t memory_type = 0;
#ifdef GRANITE_VULKAN_MT
	std::mutex lock;
#endif
	DeviceAllocator *global_allocator = nullptr;

	void set_global_allocator(DeviceAllocator *allocator)
	{
		global_allocator = allocator;
	}

	void set_memory_type(uint32_t type)
	{
		memory_type = type;
	}

	void suballocate(uint32_t num_blocks, uint32_t tiling, uint32_t memory_type, MiniHeap &heap,
	                 DeviceAllocation *alloc);

	inline void set_parent(ClassAllocator *allocator)
	{
		parent = allocator;
	}
};

class Allocator
{
public:
	Allocator();
	void operator=(const Allocator &) = delete;
	Allocator(const Allocator &) = delete;

	bool allocate(uint32_t size, uint32_t alignment, AllocationTiling tiling, DeviceAllocation *alloc);
	bool allocate_global(uint32_t size, DeviceAllocation *alloc);
	bool allocate_dedicated(uint32_t size, DeviceAllocation *alloc, VkImage image);
	inline ClassAllocator &get_class_allocator(MemoryClass clazz)
	{
		return classes[static_cast<unsigned>(clazz)];
	}

	static void free(DeviceAllocation *alloc)
	{
		alloc->free_immediate();
	}

	void set_memory_type(uint32_t memory_type)
	{
		for (auto &sub : classes)
			sub.set_memory_type(memory_type);
		this->memory_type = memory_type;
	}

	void set_global_allocator(DeviceAllocator *allocator)
	{
		for (auto &sub : classes)
			sub.set_global_allocator(allocator);
		global_allocator = allocator;
	}

private:
	ClassAllocator classes[MEMORY_CLASS_COUNT];
	DeviceAllocator *global_allocator = nullptr;
	uint32_t memory_type = 0;
};

class DeviceAllocator
{
public:
	void init(VkPhysicalDevice gpu, VkDevice device);
	void set_supports_dedicated_allocation(bool enable)
	{
		use_dedicated = enable;
	}

	~DeviceAllocator();

	bool allocate(uint32_t size, uint32_t alignment, uint32_t memory_type, AllocationTiling tiling,
	              DeviceAllocation *alloc);
	bool allocate_image_memory(uint32_t size, uint32_t alignment, uint32_t memory_type, AllocationTiling tiling,
	                           DeviceAllocation *alloc, VkImage image);

	bool allocate_global(uint32_t size, uint32_t memory_type, DeviceAllocation *alloc);

	void garbage_collect();
	void *map_memory(const DeviceAllocation &alloc, MemoryAccessFlags flags);
	void unmap_memory(const DeviceAllocation &alloc, MemoryAccessFlags flags);

	bool allocate(uint32_t size, uint32_t memory_type, VkDeviceMemory *memory, uint8_t **host_memory, VkImage dedicated_image);
	void free(uint32_t size, uint32_t memory_type, VkDeviceMemory memory, uint8_t *host_memory);
	void free_no_recycle(uint32_t size, uint32_t memory_type, VkDeviceMemory memory, uint8_t *host_memory);

private:
	std::vector<std::unique_ptr<Allocator>> allocators;
	VkDevice device = VK_NULL_HANDLE;
	VkPhysicalDeviceMemoryProperties mem_props;
	VkDeviceSize atom_alignment = 1;
#ifdef GRANITE_VULKAN_MT
	std::mutex lock;
#endif
	bool use_dedicated = false;

	struct Allocation
	{
		VkDeviceMemory memory;
		uint8_t *host_memory;
		uint32_t size;
		uint32_t type;
	};

	struct Heap
	{
		uint64_t size = 0;
		std::vector<Allocation> blocks;
		void garbage_collect(VkDevice device);
	};

	std::vector<Heap> heaps;
};
}

#endif
