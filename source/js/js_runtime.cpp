#include "js_runtime.hpp"
#include "quickjs.h"

#include <cstdlib>
#include <limits>

namespace
{
constexpr size_t kJsHeapLimit = 4u * 1024u * 1024u;
constexpr size_t kJsStackLimit = 96u * 1024u;

// newlib n'expose pas malloc_usable_size. Garder la taille demandee dans un
// petit en-tete permet a QuickJS de suivre correctement son tas et d'appliquer
// JS_SetMemoryLimit sur la 3DS.
struct alignas(8) AllocationHeader
{
	size_t size;
};

static_assert(sizeof(AllocationHeader) == 8, "L'entete QuickJS doit faire 8 octets");
constexpr size_t kAllocationOverhead = sizeof(AllocationHeader);

bool allocationFits(const JSMallocState* state, size_t size)
{
	if (size > std::numeric_limits<size_t>::max() - kAllocationOverhead)
		return false;
	if (state->malloc_size > state->malloc_limit)
		return false;
	return size + kAllocationOverhead <= state->malloc_limit - state->malloc_size;
}

void* quickJsMalloc(JSMallocState* state, size_t size)
{
	if (!allocationFits(state, size)) return nullptr;
	AllocationHeader* header = static_cast<AllocationHeader*>(
		std::malloc(sizeof(AllocationHeader) + size));
	if (!header) return nullptr;
	header->size = size;
	++state->malloc_count;
	state->malloc_size += size + kAllocationOverhead;
	return header + 1;
}

void quickJsFree(JSMallocState* state, void* ptr)
{
	if (!ptr) return;
	AllocationHeader* header = static_cast<AllocationHeader*>(ptr) - 1;
	--state->malloc_count;
	state->malloc_size -= header->size + kAllocationOverhead;
	std::free(header);
}

void* quickJsRealloc(JSMallocState* state, void* ptr, size_t size)
{
	if (!ptr) return size ? quickJsMalloc(state, size) : nullptr;
	if (!size)
	{
		quickJsFree(state, ptr);
		return nullptr;
	}

	AllocationHeader* oldHeader = static_cast<AllocationHeader*>(ptr) - 1;
	const size_t oldSize = oldHeader->size;
	const size_t oldCharge = oldSize + kAllocationOverhead;
	if (state->malloc_size < oldCharge) return nullptr;
	const size_t baseSize = state->malloc_size - oldCharge;
	if (size > std::numeric_limits<size_t>::max() - kAllocationOverhead ||
		baseSize > state->malloc_limit ||
		size + kAllocationOverhead > state->malloc_limit - baseSize)
		return nullptr;

	AllocationHeader* newHeader = static_cast<AllocationHeader*>(
		std::realloc(oldHeader, sizeof(AllocationHeader) + size));
	if (!newHeader) return nullptr;
	newHeader->size = size;
	state->malloc_size = baseSize + size + kAllocationOverhead;
	return newHeader + 1;
}

size_t quickJsUsableSize(const void* ptr)
{
	if (!ptr) return 0;
	const AllocationHeader* header = static_cast<const AllocationHeader*>(ptr) - 1;
	return header->size;
}

const JSMallocFunctions kQuickJsAllocator = {
	quickJsMalloc,
	quickJsFree,
	quickJsRealloc,
	quickJsUsableSize
};
}

bool jsPageContextCreate(JSRuntime*& runtime, JSContext*& context)
{
	runtime = nullptr;
	context = nullptr;
	runtime = JS_NewRuntime2(&kQuickJsAllocator, nullptr);
	if (!runtime) return false;
	JS_SetMemoryLimit(runtime, kJsHeapLimit);
	JS_SetMaxStackSize(runtime, kJsStackLimit);
	context = JS_NewContext(runtime);
	if (context) return true;
	JS_FreeRuntime(runtime);
	runtime = nullptr;
	return false;
}

void jsPageContextDestroy(JSRuntime*& runtime, JSContext*& context)
{
	if (context)
	{
		JS_FreeContext(context);
		context = nullptr;
	}
	if (runtime)
	{
		JS_FreeRuntime(runtime);
		runtime = nullptr;
	}
}
