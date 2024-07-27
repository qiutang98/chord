#pragma once 

#include <utils/allocator.h>
#include <graphics/graphics.h>
#include <graphics/bufferpool.h>

#include <shader/gltf.h>
#include <asset/gltf/gltf.h>

#include <renderer/render_helper.h>

namespace chord
{
	extern void GPUSceneScatterUpload(
		graphics::GraphicsOrComputeQueue& computeQueue,
		graphics::PoolBufferGPUOnlyRef GPUSceneBuffer,
		std::vector<math::uvec4>&& indexingData,
		std::vector<math::uvec4>&& collectedData);

	template<typename T, uint32 kFloat4Count>
	class GPUScenePool
	{
	public:
		// Upload type store all need update datas.
		using UploadType = std::array<math::uvec4, kFloat4Count>;
		constexpr static uint32 kPerAllocatedSize = kFloat4Count * sizeof(float) * 4;

		// 
		explicit GPUScenePool(const std::string& name)
			: m_allocator(kFloat4Count)
			, m_name(name)
		{

		}

		struct UpdatedObject
		{
			uint32 offset;
			UploadType data;

			bool operator<(const UpdatedObject& rhs) const
			{
				return offset < rhs.offset;
			}
		};

		// Allocate object in GPU scene.
		uint32 requireId(uint64 hashId)
		{
			check(!m_allocatedElements[hashId].isValid());
			m_allocatedElements[hashId] = m_allocator.allocate();

			return m_allocatedElements[hashId].get();
		}

		// Update id in object, return offset in allocator.
		void updateId(uint32 offset, const UploadType& data)
		{
			// insert object in pending update list.
			m_updateObjects.insert(UpdatedObject{ .offset = offset, .data = data });
		}

		// Free object in GPUScene.
		uint32 free(uint64 hashId)
		{
			auto freeId = m_allocatedElements[hashId];
			check(freeId.isValid()); 
			m_allocatedElements.erase(hashId);

			// Free allocator element.
			m_allocator.free(freeId.get());

			return freeId.get();
		}

		bool shouldFlush() const
		{
			return !m_updateObjects.empty();
		}

		bool flushUpdateCommands(graphics::GraphicsOrComputeQueue& computeQueue)
		{
			// Don't need upload to GPU scene, so just return.
			if (m_updateObjects.empty())
			{
				return false;
			}

			using namespace graphics;
			size_t allocatorUsedMemory = m_allocator.getMaxSize() * kPerAllocatedSize;

			// Update GPU buffer size if overflow.
			if (m_currentGPUBuffer == nullptr || allocatorUsedMemory > m_currentGPUBuffer->get().getSize())
			{
				size_t roundUpSize = math::max(size_t(64), allocatorUsedMemory);
				roundUpSize = getNextPOT(roundUpSize);

				auto newGPUBuffer = getContext().getBufferPool().createGPUOnly(
					std::format("GPUSceneBuffer.{0}", m_name), 
					roundUpSize,
					VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);

				if (m_currentGPUBuffer != nullptr)
				{
					// Copy buffer from old to new.
					computeQueue.copyBuffer(m_currentGPUBuffer, newGPUBuffer, m_currentGPUBuffer->get().getSize(), 0, 0);
				}
				m_currentGPUBuffer = newGPUBuffer;
			}

			//
			std::vector<math::uvec4> indexingData;
			std::vector<math::uvec4> collectedData;

			for (const UpdatedObject& updateObject : m_updateObjects)
			{
				math::uvec4 indexing;
				indexing.x = collectedData.size();
				indexing.y = kFloat4Count;
				indexing.z = updateObject.offset * kFloat4Count;

				// Data copy.
				indexingData.push_back(std::move(indexing));
				collectedData.insert(collectedData.end(), updateObject.data.begin(), updateObject.data.end());
			}

			// Scatter upload.
			GPUSceneScatterUpload(computeQueue, m_currentGPUBuffer, std::move(indexingData), std::move(collectedData));

			// This frame already update, so clear.
			m_updateObjects.clear();

			return true;
		}

		uint32 getBindlessSRVId() const
		{
			if (m_currentGPUBuffer == nullptr)
			{
				return ~0;
			}

			return m_currentGPUBuffer->get().requireView(true, false).storage.get();
		}

	private:
		const std::string m_name;

		// Pool based allocator.
		PoolAllocator m_allocator;

		// Store allocated elements.
		std::map<uint64, OptionalUint32> m_allocatedElements;

		// Object which will update in GPU scene.
		std::set<UpdatedObject> m_updateObjects;

		// Current hosted all gpu buffers.
		graphics::PoolBufferGPUOnlyRef m_currentGPUBuffer = nullptr;
	};

	using GPUSceneGLTFPrimitiveAssetPool  = GPUScenePool<GPUGLTFPrimitiveAsset, GPUGLTFPrimitiveAsset::kGPUSceneDataFloat4Count>;
	using GPUSceneGLTFPrimitiveDetailPool = GPUScenePool<GPUGLTFPrimitiveAsset, GPUGLTFPrimitiveAsset::kGPUSceneDetailFloat4Count>;

	class GPUScene : NonCopyable
	{
	public:
		explicit GPUScene();
		~GPUScene();

		auto& getGLTFPrimitiveDataPool() { return m_gltfPrimitiveDataPool; }
		const auto& getGLTFPrimitiveDataPool() const { return m_gltfPrimitiveDataPool; }

		auto& getGLTFPrimitiveDetailPool() { return m_gltfPrimitiveDetailPool; }
		const auto& getGLTFPrimitiveDetailPool() const { return m_gltfPrimitiveDetailPool; }

		bool shouldFlush() const
		{
			return
				m_gltfPrimitiveDataPool.shouldFlush() ||
				m_gltfPrimitiveDetailPool.shouldFlush();
		}

	private:
		friend void enqueueGPUSceneUpdate();
		void update(uint64 frameCounter, graphics::GraphicsOrComputeQueue& computeQueue);

	private:
		uint64 m_frameCounter = -1;

		GPUSceneGLTFPrimitiveAssetPool m_gltfPrimitiveDataPool;
		GPUSceneGLTFPrimitiveDetailPool m_gltfPrimitiveDetailPool;
	};

	void enqueueGPUSceneUpdate();
}