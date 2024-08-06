#pragma once
#include <graphics/common.h>
#include <graphics/resource.h>

namespace chord::graphics
{
	// Static uploader: allocate static stage buffer and never release.
	// Dynamic uploader: allocate dynamic stage buffer when need, and release when no task.

	class AsyncUploaderManager;
	using AsyncUploadTaskFunc = std::function<void(uint32 offset, uint32 queueFamily, void* mapped, VkCommandBuffer cmd, VkBuffer buffer)>;
	using AsyncUploadFinishFunc = std::function<void()>;
	struct AsyncUploadTask
	{
		uint32 size;
		AsyncUploadTaskFunc task = nullptr;
		AsyncUploadFinishFunc finishCallback = nullptr;
	};
	using AsyncUploadTaskRef = std::shared_ptr<AsyncUploadTask>;

	class IAsyncUploader : NonCopyable
	{
	protected:
		std::string m_name;
		AsyncUploaderManager& m_manager;
		VkFence m_fence = VK_NULL_HANDLE;

		std::future<void> m_future;
		std::atomic<bool> m_bRun = true;
		std::atomic<bool> m_bWorking = false;

		// Pool and cmd buffer created and used in async thread.
		VkCommandPool m_poolAsync = VK_NULL_HANDLE;
		VkCommandBuffer m_commandBufferAsync = VK_NULL_HANDLE;

	protected:
		virtual void threadFunction() {}

		void startRecordAsync();
		void endRecordAsync();

	public:
		IAsyncUploader(const std::string& name, AsyncUploaderManager& in);

		const VkFence& getFence() const 
		{
			return m_fence; 
		}

		const VkCommandBuffer& getCommandBuffer() const 
		{ 
			return m_commandBufferAsync; 
		}

		void wait() 
		{ 
			m_future.wait(); 
		}

		void stop() 
		{ 
			m_bRun.store(false); 
		}

		bool working() const 
		{ 
			return m_bWorking.load(); 
		}

		virtual void onFinished();
	};

	class DynamicAsyncUploader : public IAsyncUploader
	{
	private:
		AsyncUploadTaskRef m_processingTask = nullptr;
		HostVisibleGPUBufferRef m_stageBuffer = nullptr;

	protected:
		virtual void threadFunction() override;

	public:
		DynamicAsyncUploader(const std::string& name, AsyncUploaderManager& in)
			: IAsyncUploader(name, in)
		{

		}

		virtual ~DynamicAsyncUploader() { }

		virtual void onFinished() override;
	};

	class StaticAsyncUploader : public IAsyncUploader
	{
	private:
		std::vector<AsyncUploadTaskRef> m_processingTasks;
		HostVisibleGPUBufferRef m_stageBuffer = nullptr;

	private:
		virtual void threadFunction() override;

	public:
		StaticAsyncUploader(const std::string& name, AsyncUploaderManager& in)
			: IAsyncUploader(name, in)
		{
		}
		virtual ~StaticAsyncUploader() { }

		virtual void onFinished() override;
	};

	class AsyncUploaderManager : NonCopyable
	{
	public:
		explicit AsyncUploaderManager(uint32 staticUploaderMaxSize, uint32 dynamicUploaderMinSize);
		~AsyncUploaderManager();

		void tick(const ApplicationTickData& tickData);
		void addTask(size_t requireSize, AsyncUploadTaskFunc&& func, AsyncUploadFinishFunc&& finishCallback);
		void pushSubmitFunctions(IAsyncUploader* f);

	protected:
		void submitObjects();
		void syncPendingObjects();

	public:
		// Is uploader manager busy or not.
		bool busy() const;

		// Flush all task in uploader.
		void flushTask();

		// Get working queue family.
		auto getQueueFamily() const{ return m_queueFamily;}

		auto getStaticUploadMaxSize() const { return m_staticUploaderMaxSize; }
		auto getDynamicUploadMinSize() const { return m_dynamicUploaderMinSize; }

		auto& getStaticMutex() { return m_staticContext.mutex; }
		auto& getDynamicMutex() { return m_dynamicContext.mutex; }

		auto& getStaticCondition() { return m_staticContext.cv; }
		auto& getDynamicCondition() { return m_dynamicContext.cv; }

		bool staticLoadAssetTaskEmpty() const
		{
			std::lock_guard lock(m_staticContext.mutex);
			return m_staticContext.tasks.empty();
		}
		bool dynamicLoadAssetTaskEmpty() const
		{
			std::lock_guard lock(m_dynamicContext.mutex);
			return m_dynamicContext.tasks.empty();
		}

		void staticTasksAction(std::function<void(std::queue<AsyncUploadTaskRef>&)>&& func)
		{
			std::lock_guard lock(m_staticContext.mutex);
			func(m_staticContext.tasks);
		}
		void dynamicTasksAction(std::function<void(std::queue<AsyncUploadTaskRef>&)>&& func)
		{
			std::lock_guard lock(m_dynamicContext.mutex);
			func(m_dynamicContext.tasks);
		}

	private:
		// Async copy queue family.
		const uint32 m_queueFamily;

		// Task need to load use static stage buffer.
		struct UploaderContext
		{
			std::condition_variable cv;
			mutable std::mutex mutex;
			std::queue<AsyncUploadTaskRef> tasks;
		};
		UploaderContext m_staticContext;
		UploaderContext m_dynamicContext;

		std::vector<std::unique_ptr<StaticAsyncUploader>> m_staticUploaders;
		std::vector<std::unique_ptr<DynamicAsyncUploader>> m_dynamicUploaders;

		// Size threshold of uploader.
		const uint32 m_staticUploaderMaxSize;
		const uint32 m_dynamicUploaderMinSize;

		std::mutex m_submitObjectsMutex;
		std::vector<IAsyncUploader*> m_submitObjects;
		std::vector<IAsyncUploader*> m_pendingObjects;
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
		void setLoadingState(bool bState)
		{
			m_bLoading = bState;
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
			const VmaAllocationCreateInfo& vmaCreateInfo
		);

		explicit GPUTextureAsset(GPUTextureRef texture)
			: IUploadAsset(nullptr)
			, m_texture(texture)
		{
			// Set loading state ready.
			setLoadingState(false);
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