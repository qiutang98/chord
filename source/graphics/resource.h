#pragma once
#include <graphics/common.h>

namespace chord::graphics
{
	// Sampler never release and, we store in one manager.
	class GPUSamplerManager : NonCopyable
	{
	public:
		explicit GPUSamplerManager();
		~GPUSamplerManager();

		struct Sampler
		{
			VkSampler handle = VK_NULL_HANDLE; // Sampler handle.
			OptionalUint32 index; // Index in bindless set.
		};

		Sampler getSampler(SamplerCreateInfo info);

		// Point mipmap filter & point.
		Sampler pointClampEdge();
		Sampler pointClampBorder0000();
		Sampler pointClampBorder1111();
		Sampler pointRepeat();

		// Point mipmap filter & linear.
		Sampler linearClampEdgeMipPoint();
		Sampler linearClampBorder0000MipPoint();
		Sampler linearClampBorder1111MipPoint();
		Sampler linearRepeatMipPoint();

		// Linear mipmap filter.
		Sampler linearClampEdge();
		Sampler linearRepeat();

	private:
		std::unordered_map<uint32, Sampler> m_samplers;
	};

	class GPUResource : public IResource
	{
	public:
		explicit GPUResource(const std::string& name, VkDeviceSize size);
		virtual ~GPUResource();

		const auto& getSize() const { return m_size; }
		const std::string& getName() const { return m_flattenName; }

		virtual void rename(const std::string& name) = 0;

	protected:
		// Update memory size, should only call once when allocate memory.
		void setSize(VkDeviceSize size)
		{
			m_size = size;
		}

		bool setName(const std::string& name);

	private:
		// Device size.
		VkDeviceSize m_size = 0;

		// Name of resource.
		std::string m_flattenName = {};
		std::set<std::string> m_names;
	};
	using GPUResourceRef = std::shared_ptr<GPUResource>;

	class ImageView
	{
	public:
		OptionalVkImageView handle;

		OptionalUint32 SRV;
		OptionalUint32 UAV;
	};

	struct GPUSyncBarrierMasks
	{
		uint32 queueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		VkAccessFlags accesMask = VK_ACCESS_NONE;

		bool operator==(const GPUSyncBarrierMasks&) const = default;
	};

	struct GPUTextureSyncBarrierMasks
	{
		GPUSyncBarrierMasks barrierMasks = {};
		VkImageLayout imageLayout = VK_IMAGE_LAYOUT_UNDEFINED;

		bool operator==(const GPUTextureSyncBarrierMasks&) const = default;
	};

	extern const VkImageSubresourceRange kDefaultImageSubresourceRange;
	class GPUTexture : public GPUResource
	{
	public:
		explicit GPUTexture(
			const std::string& name, 
			const VkImageCreateInfo& createInfo,
			const VmaAllocationCreateInfo& vmaCreateInfo);

		virtual ~GPUTexture();

		virtual void rename(const std::string& name) override;

		operator VkImage() const
		{
			return m_image;
		}

		VkFormat getFormat() const 
		{ 
			return m_createInfo.format; 
		}

		VkExtent3D getExtent() const 
		{ 
			return m_createInfo.extent; 
		}

		const VkImageCreateInfo& getInfo() const 
		{ 
			return m_createInfo; 
		}

		const VmaAllocationInfo& getAllocationInfo() const
		{
			return m_vmaAllocationInfo;
		}

		VkImageLayout getCurrentLayout(uint32 layerIndex, uint32 mipLevel) const
		{
			uint32_t subresourceIndex = getSubresourceIndex(layerIndex, mipLevel);
			return m_subresourceStates.at(subresourceIndex).imageLayout;
		}

		// Require image view misc info.
		ImageView requireView(
			const VkImageSubresourceRange& range, 
			VkImageViewType viewType, 
			bool bSRV, 
			bool bUAV);

		// Generic pipeline state, so don't call this in render pipe, just for queue family transfer purpose.
		void transition(VkCommandBuffer cb, const GPUTextureSyncBarrierMasks& newState, const VkImageSubresourceRange& range);
		void transitionImmediately(VkImageLayout newImageLayout, const VkImageSubresourceRange& range);
	protected:
		// Get subresource index.
		uint32 getSubresourceIndex(uint32 layerIndex, uint32 mipLevel) const;

	protected:
		// Cache image create info.
		VkImageCreateInfo m_createInfo;

		// VMA allocation info.
		VmaAllocationInfo m_vmaAllocationInfo;

		// Image handle.
		VkImage m_image = VK_NULL_HANDLE;

		// VMA allocation.
		VmaAllocation m_allocation = nullptr;

		// Global image resource state.
		std::vector<GPUTextureSyncBarrierMasks> m_subresourceStates;

		// Views of this image.
		std::unordered_map<uint64, ImageView> m_views;
	};
	using GPUTextureRef = std::shared_ptr<GPUTexture>;

	class GPUBuffer : public GPUResource
	{
	public:
		explicit GPUBuffer(
			const std::string& name,
			const VkBufferCreateInfo& createInfo,
			const VmaAllocationCreateInfo& vmaCreateInfo);

		virtual ~GPUBuffer();

		const VmaAllocationInfo& getAllocationInfo() const
		{
			return m_vmaAllocationInfo;
		}

		virtual void rename(const std::string& name) override;

		operator VkBuffer() const
		{
			return m_buffer;
		}

		const VkBufferUsageFlags getUsage() const
		{
			return m_createInfo.usage;
		}

		uint64 getDeviceAddress();

	protected:
		// Buffer create info.
		VkBufferCreateInfo m_createInfo;

		// VMA allocation info.
		VmaAllocationInfo m_vmaAllocationInfo;

		// Buffer device address.
		OptionalUint64 m_deviceAddress;

		// Buffer handle.
		VkBuffer m_buffer = VK_NULL_HANDLE;

		// Memory allocation.
		VmaAllocation m_allocation = VK_NULL_HANDLE;
	};
	using GPUBufferRef = std::shared_ptr<GPUBuffer>;

	static inline VmaAllocationCreateInfo getGPUOnlyBufferVMACI()
	{
		VmaAllocationCreateInfo vmaAllocInfo = {};
		vmaAllocInfo.usage = VMA_MEMORY_USAGE_AUTO;
		return vmaAllocInfo;
	}

	// GPU inner buffer, need to tracking it's barrier state.
	// Can used for UAV or SRV.
	class GPUOnlyBuffer : public GPUBuffer
	{
	public:
		explicit GPUOnlyBuffer(
			const std::string& name,
			const VkBufferCreateInfo& createInfo);

		virtual ~GPUOnlyBuffer();

	protected:
		// Whole GPU buffer state.
		GPUSyncBarrierMasks m_syncBarrierMasks;
	};
	using GPUOnlyBufferRef = std::shared_ptr<GPUOnlyBuffer>;

	static inline VmaAllocationCreateInfo getHostVisibleGPUBufferVMACI()
	{
		VmaAllocationCreateInfo vmaAllocInfo = {};
		vmaAllocInfo.usage = VMA_MEMORY_USAGE_AUTO;
		vmaAllocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

		return vmaAllocInfo;
	}

	// Host visibile buffer commonly used for CPU/GPU buffer copy sync.
	// Don't care gpu inner state tracking.
	// And it only can used ad SRV, no UAV.
	class HostVisibleGPUBuffer : public GPUBuffer
	{
	public:
		explicit HostVisibleGPUBuffer(
			const std::string& name, 
			const VkBufferCreateInfo& createInfo,
			SizedBuffer data = { });

		virtual ~HostVisibleGPUBuffer();

		// Get current buffer mapped pointer, see map(size) and unmap() functions.
		void* getMapped() const 
		{
			return m_mapped;
		}

		void map(VkDeviceSize size = VK_WHOLE_SIZE);
		void unmap();
		void flush(VkDeviceSize size = VK_WHOLE_SIZE, VkDeviceSize offset = 0);
		void invalidate(VkDeviceSize size = VK_WHOLE_SIZE, VkDeviceSize offset = 0);

		// Copy whole size of buffer.
		void copyTo(const void* data, VkDeviceSize size);

	protected:
		// Mapped pointer.
		void* m_mapped = nullptr;
	};
	using HostVisibleGPUBufferRef = std::shared_ptr<HostVisibleGPUBuffer>;



}