#pragma once 

#include <utils/allocator.h>
#include <graphics/graphics.h>
#include <graphics/bufferpool.h>

#include <shader/gltf.h>

namespace chord
{
	// GPUScene store all.

	template<typename T>
	class GPUScenePool
	{
	public:
		static_assert(requires(const T& e) { { e.hash() } -> std::same_as<uint64>; });
		static_assert(requires {{ T::elementSize() } -> std::same_as<size_t>; });

		explicit GPUScenePool()
			: m_allocator(T::elementSize())
		{

		}

		size_t getId(const T& inT)
		{
			uint64 hashId = inT.hash();
			if (!m_allocatedElements[hashId].isValid())
			{
				m_allocatedElements[hashId] = m_allocator.allocate();
			}

			return m_allocatedElements[hashId];
		}

		void freeId(const T& inT)
		{
			uint64 hashId = inT.hash();

			auto freeId = m_allocatedElements[hashId];
			check(freeId.isValid());
			m_allocatedElements.erase(hashId);

			// Free allocator element.
			m_allocator.free(freeId.get());
		}

	private:
		PoolAllocator m_allocator;
		std::map<uint64, OptionalSizeT> m_allocatedElements;
	};

	struct GLTFPrimitiveGPUScenePool
	{
		uint64 hash() const { return 0; };
		static size_t elementSize() { return sizeof(GLTFPrimitiveDatasBuffer); }
	};

	class GPUScene
	{
	public:
		explicit GPUScene();

		auto& getGLTFPrimitiveDataPool() { return m_gltfPrimitiveDataPool; }
		const auto& getGLTFPrimitiveDataPool() const { return m_gltfPrimitiveDataPool; }


	private:
		GPUScenePool<GLTFPrimitiveGPUScenePool> m_gltfPrimitiveDataPool;
	};
}