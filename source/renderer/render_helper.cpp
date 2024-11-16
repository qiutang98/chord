#include "render_helper.h"

namespace chord
{
	using namespace graphics;

	PushSetBuilder& chord::PushSetBuilder::addSRV(PoolBufferRef buffer)
	{
		asSRV(m_queue, buffer);

		CacheBindingBuilder builder;
		builder.type = CacheBindingBuilder::EType::BufferSRV;
		builder.buffer = buffer->get();

		VkDescriptorBufferInfo bufferInfo{};
		bufferInfo.buffer = buffer->get();
		bufferInfo.offset = 0;
		bufferInfo.range = buffer->get().getSize();

		builder.bufferInfo = bufferInfo;

		builder.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		m_cacheBindingBuilder.push_back(builder);

		return *this;
	}

	PushSetBuilder& PushSetBuilder::addUAV(PoolBufferRef buffer)
	{
		asUAV(m_queue, buffer);

		CacheBindingBuilder builder;
		builder.type = CacheBindingBuilder::EType::BufferUAV;
		builder.buffer = buffer->get();

		VkDescriptorBufferInfo bufferInfo{};
		bufferInfo.buffer = buffer->get();
		bufferInfo.offset = 0;
		bufferInfo.range = buffer->get().getSize();

		builder.bufferInfo = bufferInfo;
		builder.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;

		m_cacheBindingBuilder.push_back(builder);
		return *this;
	}

    PushSetBuilder& PushSetBuilder::addAccelerateStructure(graphics::helper::AccelKHRRef as)
    {
        m_queue.getActiveCmd()->pendingResources.insert(as);

        CacheBindingBuilder builder;
        builder.type = CacheBindingBuilder::EType::AccelerateStructure;
        builder.accelerateStructure = as;
        

        m_cacheBindingBuilder.push_back(builder);
        return *this;
    }

    PushSetBuilder& PushSetBuilder::addSRV(graphics::PoolTextureRef image, const VkImageSubresourceRange& range, VkImageViewType viewType)
    {
        asSRV(m_queue, image, range, viewType);

        CacheBindingBuilder builder;
        builder.type = CacheBindingBuilder::EType::TextureSRV;
        builder.image = image;
        builder.imageRange = range;
        builder.viewType = viewType;
        m_cacheBindingBuilder.push_back(builder);
        return *this;
    }

    PushSetBuilder& PushSetBuilder::addUAV(graphics::PoolTextureRef image, const VkImageSubresourceRange& range, VkImageViewType viewType)
    {
        asUAV(m_queue, image, range, viewType);

        CacheBindingBuilder builder;
        builder.type = CacheBindingBuilder::EType::TextureUAV;
        builder.image = image;
        builder.imageRange = range;
        builder.viewType = viewType;
        m_cacheBindingBuilder.push_back(builder);

        return *this;
    }

    static inline VkWriteDescriptorSet pushWriteDescriptorSetBuffer(uint32_t binding, VkDescriptorType type, const VkDescriptorBufferInfo* bufferInfo, uint32_t count = 1)
    {
        return VkWriteDescriptorSet
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstBinding = binding,
            .descriptorCount = count,
            .descriptorType = type,
            .pBufferInfo = bufferInfo,
        };
    }

    static inline VkDescriptorImageInfo descriptorImageInfoSample(VkImageView view)
    {
        return VkDescriptorImageInfo
        {
            .imageView = view,
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        };
    }

    static inline VkDescriptorImageInfo descriptorImageInfoStorage(VkImageView view)
    {
        return VkDescriptorImageInfo
        {
            .imageView = view,
            .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
        };
    }

    static inline VkWriteDescriptorSet pushWriteDescriptorSetImage(uint32_t binding, VkDescriptorType type, const VkDescriptorImageInfo* imageInfo, uint32_t count = 1)
    {
        return VkWriteDescriptorSet
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstBinding = binding,
            .descriptorCount = count,
            .descriptorType = type,
            .pImageInfo = imageInfo,
        };
    }

	void PushSetBuilder::push(ComputePipelineRef pipe, uint32 set)
    {
        std::vector<VkWriteDescriptorSet> writes(m_cacheBindingBuilder.size());
        std::vector<VkDescriptorImageInfo> images(m_cacheBindingBuilder.size());
        std::vector<VkWriteDescriptorSetAccelerationStructureKHR> ases(m_cacheBindingBuilder.size());

        for (uint32_t i = 0; i < m_cacheBindingBuilder.size(); i++)
        {
            auto& binding = m_cacheBindingBuilder[i];
            switch (m_cacheBindingBuilder[i].type)
            {
            case CacheBindingBuilder::EType::BufferSRV:
            case CacheBindingBuilder::EType::BufferUAV:
            {
                writes[i] = pushWriteDescriptorSetBuffer(i, binding.descriptorType, &binding.bufferInfo);
            }
            break;
            case CacheBindingBuilder::EType::TextureSRV:
            {
                images[i] = descriptorImageInfoSample(binding.image->get().requireView(binding.imageRange, binding.viewType, true, false).handle.get());
                writes[i] = pushWriteDescriptorSetImage(i, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, &images[i]);
            }
            break;
            case CacheBindingBuilder::EType::TextureUAV:
            {
                images[i] = descriptorImageInfoStorage(binding.image->get().requireView(binding.imageRange, binding.viewType, false, true).handle.get());
                writes[i] = pushWriteDescriptorSetImage(i, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &images[i]);
            }
            break;
            case CacheBindingBuilder::EType::AccelerateStructure:
            {
                VkWriteDescriptorSetAccelerationStructureKHR descriptorAccelerationStructureInfo{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR };
                descriptorAccelerationStructureInfo.accelerationStructureCount = 1;
                descriptorAccelerationStructureInfo.pAccelerationStructures = &binding.accelerateStructure->accel;
                ases[i] = descriptorAccelerationStructureInfo;

                VkWriteDescriptorSet accelerationStructureWrite { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
                accelerationStructureWrite.pNext = &ases[i];
                accelerationStructureWrite.dstBinding = i;
                accelerationStructureWrite.descriptorCount = 1;
                accelerationStructureWrite.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
                writes[i] = accelerationStructureWrite;
            }
            break;
            default:
            {
                checkEntry();
            }
            break;
            }
        }

        getContext().pushDescriptorSet(
            m_cmd, 
            VK_PIPELINE_BIND_POINT_COMPUTE, 
            pipe->getLayout(),
            set, 
            uint32_t(writes.size()), 
            writes.data());
    }

    DDGIConfigCPU::DDGIConfigCPU()
    {
        {
            auto& config = volumeConfigs[0];

            config.rayHitSampleTextureLod = 3;
            config.hysteresis             = 0.92f;
            config.probeDistanceExponent  = 5.0f;
            config.probeNormalBias        = 0.2f;
            config.probeViewBias          = 0.2f;
        }

        {
            auto& config = volumeConfigs[1];

            config.rayHitSampleTextureLod = 4;
            config.hysteresis             = 0.95f;
            config.probeDistanceExponent = 10.0f;
            config.probeNormalBias       = 0.8f;
            config.probeViewBias         = 0.8f;;
        }

        {
            auto& config = volumeConfigs[2];

            config.rayHitSampleTextureLod = 6;
            config.hysteresis             = 0.96f;
            config.probeDistanceExponent  = 25.0f;
            config.probeNormalBias        = 6.4f;
            config.probeViewBias          = 6.4f;
        }

        {
            auto& config = volumeConfigs[3];

            config.rayHitSampleTextureLod = 8;
            config.hysteresis = 0.97f;
            config.probeDistanceExponent = 50.0f;
            config.probeNormalBias = 102.4f;
            config.probeViewBias   = 102.4f;
        }
    }
}


