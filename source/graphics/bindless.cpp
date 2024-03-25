#include <graphics/bindless.h>
#include <graphics/graphics.h>
#include <graphics/helper.h>
#include <graphics/resource.h>

namespace chord::graphics
{
	BindlessManager::BindlessManager()
	{
		const auto& indexingProps = getContext().getPhysicalDeviceDescriptorIndexingProperties();

		// Configs init.
		m_bindingConfigs[static_cast<uint32>(EBindingType::StorageBuffer)] = { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 500000u, indexingProps.maxDescriptorSetUpdateAfterBindStorageBuffers };
		m_bindingConfigs[static_cast<uint32>(EBindingType::UniformBuffer)] = { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 500000u, indexingProps.maxDescriptorSetUpdateAfterBindUniformBuffers };
		m_bindingConfigs[static_cast<uint32>(EBindingType::SampledImage)]  = { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,  500000u, indexingProps.maxDescriptorSetUpdateAfterBindSampledImages  };
		m_bindingConfigs[static_cast<uint32>(EBindingType::StorageImage)]  = { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,  500000u, indexingProps.maxDescriptorSetUpdateAfterBindStorageImages  };
		m_bindingConfigs[static_cast<uint32>(EBindingType::Sampler)]       = { VK_DESCRIPTOR_TYPE_SAMPLER,        100000u, indexingProps.maxDescriptorSetUpdateAfterBindSamplers       };

		// Range clamp.
		for (auto& config : m_bindingConfigs)
		{
			// Default use half percentage as max count.
			constexpr uint32 kUsedCountPercentage = 2;

			// Clamp to valid range.
			config.count = math::clamp(config.count, 1u, config.limit / kUsedCountPercentage);
		}
		
		// Clear used count.
		for (auto& count : m_usedCount)
		{
			count = 0;
		}

		// Create bindings.
		{
			std::array<VkDescriptorSetLayoutBinding, kBindingCount> bindings{};
			std::array<VkDescriptorBindingFlags, kBindingCount> flags{ };

			// Fill binding infos.
			for (uint32_t i = 0; i < kBindingCount; ++i)
			{
				bindings[i].binding = i;
				bindings[i].descriptorType  = m_bindingConfigs[i].type;
				bindings[i].descriptorCount = m_bindingConfigs[i].count;
				bindings[i].stageFlags = VK_SHADER_STAGE_ALL;

				// Flag for bindless set.
				flags[i] =
					VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT |
					VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;
			}

			VkDescriptorSetLayoutBindingFlagsCreateInfo bindingFlags{ };
			bindingFlags.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
			bindingFlags.pNext = nullptr;
			bindingFlags.pBindingFlags = flags.data();
			bindingFlags.bindingCount = kBindingCount;

			VkDescriptorSetLayoutCreateInfo createInfo{};
			createInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
			createInfo.bindingCount = kBindingCount;
			createInfo.pBindings = bindings.data();
			createInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
			createInfo.pNext = &bindingFlags;

			// Create bindless set layout.
			m_setLayout = helper::createDescriptorSetLayout(createInfo);
		}

		// Create pool.
		{
			std::array<VkDescriptorPoolSize, kBindingCount> poolSize{ };
			for (uint32_t i = 0; i < kBindingCount; ++i)
			{
				poolSize[i].type = m_bindingConfigs[i].type;
				poolSize[i].descriptorCount = m_bindingConfigs[i].count;
			}

			VkDescriptorPoolCreateInfo poolCreateInfo{};
			poolCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
			poolCreateInfo.poolSizeCount = kBindingCount;
			poolCreateInfo.pPoolSizes = poolSize.data();
			poolCreateInfo.maxSets = 1;
			poolCreateInfo.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;

			m_pool = helper::createDescriptorPool(poolCreateInfo);
		}

		// Create bindless set.
		{
			VkDescriptorSetAllocateInfo allocateInfo{};
			allocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
			allocateInfo.pNext = nullptr;
			allocateInfo.descriptorPool = m_pool;
			allocateInfo.pSetLayouts = &m_setLayout;
			allocateInfo.descriptorSetCount = 1;

			// Create descriptor.
			checkVkResult(vkAllocateDescriptorSets(getDevice(), &allocateInfo, &m_set));
		}
	}

	BindlessManager::~BindlessManager()
	{
		// Destroy all device resource.
		helper::destroyDescriptorSetLayout(m_setLayout);
		helper::destroyDescriptorPool(m_pool);
	}

	BindlessIndex BindlessManager::registerSampler(VkSampler sampler)
	{
		VkDescriptorImageInfo imageInfo { };
		imageInfo.sampler = sampler;
		imageInfo.imageView = VK_NULL_HANDLE;
		imageInfo.imageLayout = VK_IMAGE_LAYOUT_UNDEFINED;

		VkWriteDescriptorSet write { };
		write.sType  = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		write.dstSet          = m_set;
		write.descriptorType  = m_bindingConfigs[static_cast<uint32>(EBindingType::Sampler)].type;
		write.dstBinding      = static_cast<uint32>(EBindingType::Sampler);
		write.pImageInfo      = &imageInfo;
		write.descriptorCount = 1;
		write.dstArrayElement = requireIndex(EBindingType::Sampler);

		vkUpdateDescriptorSets(getDevice(), 1, &write, 0, nullptr);

		return write.descriptorCount;
	}

	BindlessIndex BindlessManager::registerSRV(VkImageView view)
	{
		VkDescriptorImageInfo imageInfo{ };
		imageInfo.sampler     = VK_NULL_HANDLE;
		imageInfo.imageView   = view;
		imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		VkWriteDescriptorSet write{ };
		write.sType  = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		write.dstSet = m_set;
		write.descriptorType = m_bindingConfigs[static_cast<uint32>(EBindingType::SampledImage)].type;
		write.dstBinding = static_cast<uint32>(EBindingType::SampledImage);
		write.pImageInfo = &imageInfo;
		write.descriptorCount = 1;
		write.dstArrayElement = requireIndex(EBindingType::SampledImage);

		vkUpdateDescriptorSets(getDevice(), 1, &write, 0, nullptr);

		return write.descriptorCount;
	}

	void BindlessManager::freeSRV(BindlessIndex& index, ImageView fallback)
	{
		if (fallback.handle.isValid())
		{
			VkDescriptorImageInfo imageInfo{};
			imageInfo.sampler     = VK_NULL_HANDLE;
			imageInfo.imageView   = fallback.handle.get();
			imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

			VkWriteDescriptorSet  write{};
			write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			write.dstSet = m_set;
			write.descriptorType = m_bindingConfigs[static_cast<uint32>(EBindingType::SampledImage)].type;
			write.dstBinding = static_cast<uint32>(EBindingType::SampledImage);
			write.pImageInfo = &imageInfo;
			write.descriptorCount = 1;
			write.dstArrayElement = index.get();

			vkUpdateDescriptorSets(getDevice(), 1, &write, 0, nullptr);
		}

		freeIndex(EBindingType::SampledImage, index.get());
		index = { };
	}

	BindlessIndex BindlessManager::registerUAV(VkImageView view)
	{
		VkDescriptorImageInfo imageInfo{ };
		imageInfo.sampler = VK_NULL_HANDLE;
		imageInfo.imageView = view;
		imageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

		VkWriteDescriptorSet write{ };
		write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		write.dstSet = m_set;
		write.descriptorType = m_bindingConfigs[static_cast<uint32>(EBindingType::StorageImage)].type;
		write.dstBinding = static_cast<uint32>(EBindingType::StorageImage);
		write.pImageInfo = &imageInfo;
		write.descriptorCount = 1;
		write.dstArrayElement = requireIndex(EBindingType::StorageImage);

		vkUpdateDescriptorSets(getDevice(), 1, &write, 0, nullptr);

		return write.descriptorCount;
	}

	void BindlessManager::freeUAV(BindlessIndex& index, ImageView fallback)
	{
		if (fallback.handle.isValid())
		{
			VkDescriptorImageInfo imageInfo{};
			imageInfo.sampler = VK_NULL_HANDLE;
			imageInfo.imageView = fallback.handle.get();
			imageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

			VkWriteDescriptorSet  write{};
			write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			write.dstSet = m_set;
			write.descriptorType = m_bindingConfigs[static_cast<uint32>(EBindingType::StorageImage)].type;
			write.dstBinding = static_cast<uint32>(EBindingType::StorageImage);
			write.pImageInfo = &imageInfo;
			write.descriptorCount = 1;
			write.dstArrayElement = index.get();

			vkUpdateDescriptorSets(getDevice(), 1, &write, 0, nullptr);
		}

		freeIndex(EBindingType::StorageImage, index.get());
		index = { };
	}

	uint32 BindlessManager::requireIndex(EBindingType type)
	{
		std::lock_guard lock(m_lockCount);

		const auto& typeIndex = static_cast<uint32>(type);
		const auto& config = m_bindingConfigs[typeIndex];

		// We only reuse when free number is large enough.
		const auto minStartFreeSize = config.count / 4;

		// Final index.
		uint32 index = 0;

		// Reuse or increment new one.
		auto& freeCounts = m_freeCount[typeIndex];
		auto& usedCount  = m_usedCount[typeIndex];
		if (freeCounts.size() < minStartFreeSize)
		{
			index = usedCount;
			usedCount++;

			if (usedCount >= config.count)
			{
				LOG_GRAPHICS_ERROR("Too much item use in this set, current bindless count already reach {0}, and the config max is {1}, the device limit is {2}. ",
					usedCount, config.count, config.limit);

				LOG_GRAPHICS_ERROR("We reset bindless set count now, maybe cause some render error after this.");
				usedCount = 0;
			}
		}
		else
		{
			index = freeCounts.front();
			freeCounts.pop();
		}

		// Return result.
		return index;
	}

	void BindlessManager::freeIndex(EBindingType type, uint32 index)
	{
		std::lock_guard lock(m_lockCount);

		const auto& typeIndex = static_cast<uint32>(type);
		m_freeCount[typeIndex].push(index);
	}

}