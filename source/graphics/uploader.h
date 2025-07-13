#pragma once
#include <graphics/common.h>
#include <graphics/resource.h>
#include <graphics/buffer_pool.h>
#include <utils/job_system.h>
#include <utils/mpsc_queue.h>

namespace chord::graphics
{
	using AsyncUploadTaskFunc = std::function<void(uint32 offset, uint32 queueFamily, void* mapped, VkCommandBuffer cmd, VkBuffer buffer)>;
	using AsyncUploadFinishFunc = std::function<void()>;
	struct AsyncUploadTask
	{
		uint32 size;
		AsyncUploadTaskFunc task = nullptr;
		AsyncUploadFinishFunc finishCallback = nullptr;
	};
	using AsyncUploadTaskRef = std::shared_ptr<AsyncUploadTask>;
	

	class AsyncUploaderBase : NonCopyable
	{
	protected:
		const std::string m_name;
		AsyncUploaderManager& m_manager;

		// 
		VkFence m_fence = VK_NULL_HANDLE;

		// Pool and cmd buffer created and used in async thread.
		VkCommandPool m_poolAsync = VK_NULL_HANDLE;
		VkCommandBuffer m_commandBufferAsync = VK_NULL_HANDLE;

		//
		using UploadTaskQueue = MPSCQueue<AsyncUploadTaskRef, MPSCQueueHeapAllocator<AsyncUploadTaskRef>>;
		UploadTaskQueue m_pendingTasks;
		JobDependencyRef m_dispatchJob = nullptr;

		// 
		struct ExecutingTask
		{
			GPUBufferRef buffer;
			AsyncUploadTaskRef task;
		};
		std::queue<ExecutingTask> m_processingTasks;

		void startRecordAsync() const;
		void endRecordAsync() const;

		void pushTaskToProcessingQueue(AsyncUploadTaskRef task, GPUBufferRef buffer);
		virtual bool doTask() = 0;

	public:
		AsyncUploaderBase(const std::string& name, AsyncUploaderManager& manager);
		virtual ~AsyncUploaderBase();

		// Add task from any thread.
		void addTask(AsyncUploadTaskRef task);

		bool gpuTaskFinish() const;
		bool allTaskFinish() const;

		// Only execute on main thread.
		void triggleSubmitJob(bool bForceDispatch = false);
	};

	class DynamicAsyncUploader : public AsyncUploaderBase
	{
	protected:
		PoolBufferHostVisible m_stageBuffer = nullptr;
		virtual bool doTask() override;

	public:
		DynamicAsyncUploader(const std::string& name, AsyncUploaderManager& in)
			: AsyncUploaderBase(name, in)
		{
		}
	};

	class StaticAsyncUploader : public AsyncUploaderBase
	{
	protected:
		HostVisibleGPUBufferRef m_stageBuffer = nullptr;
		virtual bool doTask() override;

	public:
		StaticAsyncUploader(const std::string& name, AsyncUploaderManager& manager)
			: AsyncUploaderBase(name, manager)
		{
		}
	};


	class AsyncUploaderManager : NonCopyable
	{
	public:
		explicit AsyncUploaderManager(uint32 staticUploaderMaxSize, uint32 dynamicUploaderMinSize);
		~AsyncUploaderManager();

		void addTask(size_t requireSize, AsyncUploadTaskFunc&& func, AsyncUploadFinishFunc&& finishCallback);

	public:
		// Is uploader manager busy or not.
		inline bool busy() const
		{
			return !m_staticUploader->allTaskFinish() || !m_dynamicUploader->allTaskFinish();
		}

		// Flush all task in uploader.
		void flushTask();

		// Get working queue family.
		inline auto getQueueFamily() const
		{ 
			return m_queueFamily;
		}

		inline auto getStaticUploadMaxSize() const 
		{ 
			return m_staticUploaderMaxSize; 
		}

		inline auto getDynamicUploadMinSize() const 
		{ 
			return m_dynamicUploaderMinSize; 
		}

	private:
		// Size threshold of uploader.
		const uint32 m_staticUploaderMaxSize;
		const uint32 m_dynamicUploaderMinSize;

		// Async copy queue family.
		const uint32 m_queueFamily;

		// 
		std::unique_ptr<StaticAsyncUploader> m_staticUploader = nullptr;
		std::unique_ptr<DynamicAsyncUploader> m_dynamicUploader = nullptr;
	};

	class IUploadAsset : public IResource
	{
	public:
		explicit IUploadAsset(IUploadAsset* fallback)
			: m_fallback(fallback)
		{
			if (m_fallback)
			{
				checkMsgf(m_fallback->isReady(), "Fallback asset must already load.");
			}
		}

		virtual ~IUploadAsset() { }

		// Is this asset still loading or ready.
		bool isLoading() const { return  m_bLoading; }
		bool isReady()   const { return !m_bLoading; }

		// Set async load state.
		void setLoadingReady()
		{
			check(m_bLoading == true);
			m_bLoading = false;

		}

		template<typename T>
		T* getReady()
		{
			static_assert(std::is_base_of_v<IUploadAsset, T>, "Type must derived from IUploadAsset");
			if (isLoading())
			{
				checkMsgf(m_fallback, "Loading asset must exist one fallback.");
				return dynamic_cast<T*>(m_fallback);
			}
			return dynamic_cast<T*>(this);
		}

		bool existFallback() const
		{
			return m_fallback;
		}

	private:
		// The asset is under loading.
		std::atomic<bool> m_bLoading = true;

		// Fallback asset when the asset is still loading.
		IUploadAsset* m_fallback = nullptr;
	};

	class GPUTextureAsset : public IUploadAsset
	{
	public:
		GPUTextureAsset(
			GPUTextureAsset* fallback,
			const std::string& name,
			const VkImageCreateInfo& createInfo,
			const VmaAllocationCreateInfo& vmaCreateInfo);

		explicit GPUTextureAsset(GPUTextureRef texture)
			: IUploadAsset(nullptr)
			, m_texture(texture)
		{
			// Set loading state ready.
			setLoadingReady();
		}

		virtual ~GPUTextureAsset();

		size_t getSize() const 
		{ 
			return m_texture->getSize();
		}

		// Prepare image layout when start to upload.
		void prepareToUpload(VkCommandBuffer cmd, uint32 destFamily, VkImageSubresourceRange range);

		// Finish image layout when ready for shader read.
		void finishUpload(VkCommandBuffer cmd, uint32 destFamily, VkImageSubresourceRange range);

		uint32 getSRV(const VkImageSubresourceRange& range, VkImageViewType viewType);

		// Get ready image.
		GPUTextureRef getReadyImage()
		{
			if (existFallback())
			{
				return getReady<GPUTextureAsset>()->m_texture;
			}

			return m_texture;
		}

		// Self owner image handle.
		GPUTextureRef getOwnHandle() const 
		{ 
			return m_texture;
		}

	protected:
		// Image handle.
		GPUTextureRef m_texture = nullptr;
	};
	using GPUTextureAssetRef = std::shared_ptr<GPUTextureAsset>;
	using GPUTextureAssetWeak = std::weak_ptr<GPUTextureAsset>;
}