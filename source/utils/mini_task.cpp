#include <utils/mini_task.h>
#include <utils/allocator.h>

namespace chord
{
	using MiniTaskAllocator = FreeListArenaAllocator<MiniTask, 16 * 1024>;
	static MiniTaskAllocator sMiniTaskAllocator { false };

	void* MiniTask::operator new(size_t size)
	{
		assert(size == sizeof(MiniTask));
		return sMiniTaskAllocator.allocate();
	}

	void MiniTask::operator delete(void* rawMemory)
	{
		sMiniTaskAllocator.free(rawMemory);
	}
}