#include <utils/mini_task.h>
#include <utils/allocator.h>

namespace chord
{
	using MiniTaskAllocator = FreeListArenaAllocator<MiniTask, 16 * 1024>;
	static MiniTaskAllocator* sMiniTaskAllocator = new MiniTaskAllocator(false);

	void* MiniTask::operator new(size_t size)
	{
		assert(sMiniTaskAllocator && size == sizeof(MiniTask));
		return sMiniTaskAllocator->allocate();
	}

	void MiniTask::operator delete(void* rawMemory)
	{
		assert(sMiniTaskAllocator);
		sMiniTaskAllocator->free(rawMemory);
	}
}