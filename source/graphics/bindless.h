#pragma once
#include <graphics/common.h>
#include <shader/binding.h>

namespace chord::graphics
{
	using BindlessIndex = OptionalValue<uint32, ~0>;
	class BindlessManager : NonCopyable
	{
	public:
		explicit BindlessManager();
		~BindlessManager();

		CHORD_NODISCARD BindlessIndex registerSampler(VkSampler sampler);
		CHORD_NODISCARD BindlessIndex registerSRV(VkImageView view);
		CHORD_NODISCARD BindlessIndex registerUAV(VkImageView view);

		CHORD_NODISCARD BindlessIndex registerBuffer(VkBuffer buffer, VkDeviceSize offset, VkDeviceSize range);

		void freeSRV(BindlessIndex& index, ImageView fallback);
		void freeUAV(BindlessIndex& index, ImageView fallback);

		// Free ssbo bindless.
		void freeBuffer(BindlessIndex& index, GPUBufferRef fallback);

		const VkDescriptorSetLayout& getSetLayout() const { return m_setLayout; }
		const VkDescriptorSet& getSet() const { return m_set; }

		void bind(VkCommandBuffer cmd, VkPipelineBindPoint bindPoint, VkPipelineLayout layout) const
		{
			vkCmdBindDescriptorSets(cmd, bindPoint, layout, 0, 1, &m_set, 0, nullptr);
		}

	private:
		uint32 requireIndex(EBindingType type);
		void freeIndex(EBindingType type, uint32 index);

	private:
		static constexpr auto kBindingCount = static_cast<uint32>(EBindingType::MAX);

		VkDescriptorPool m_pool = VK_NULL_HANDLE;
		VkDescriptorSet m_set = VK_NULL_HANDLE;
		VkDescriptorSetLayout m_setLayout = VK_NULL_HANDLE;

		struct BindingConfig
		{
			VkDescriptorType type;
			uint32 count;
			uint32 limit;
		};
		BindingConfig m_bindingConfigs[kBindingCount];

		std::mutex m_lockCount;
		std::queue<uint32> m_freeCount[kBindingCount];
		uint32 m_usedCount[kBindingCount];
	};
}