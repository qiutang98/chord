#include <scene/component/gltf_mesh.h>
#include <ui/ui_helper.h>
#include <scene/scene_node.h>
#include <scene/component/gltf_material.h>

namespace chord
{
	const UIComponentDrawDetails GLTFMeshComponent::kComponentUIDrawDetails = GLTFMeshComponent::createComponentUIDrawDetails();

	UIComponentDrawDetails GLTFMeshComponent::createComponentUIDrawDetails()
	{
		UIComponentDrawDetails result{};

		result.name = "GLTF Mesh";
		result.bOptionalCreated = true;
		result.decoratedName = ICON_FA_BOX + std::string("  GLTF Mesh");
		result.factory = []() { return std::make_shared<GLTFMeshComponent>(); };

		result.onDrawUI = [](ComponentRef component) -> bool
		{
			auto meshComponent = std::static_pointer_cast<GLTFMeshComponent>(component);
			bool bChanged = false;
			
			{
				ui::inspectAssetSaveInfo(meshComponent->m_gltfAssetInfo);
				if (!meshComponent->m_gltfAssetInfo.empty())
				{
					ImGui::TextDisabled("Choose mesh id: #%d.", meshComponent->m_gltfMeshId);
				}
			}

			return bChanged;
		};

		return result;
	}

	void GLTFMeshComponent::reloadResource()
	{
		m_gltfAsset = Application::get().getAssetManager().getOrLoadAsset<GLTFAsset>(m_gltfAssetInfo.path(), true);
		m_gltfGPU = m_gltfAsset->getGPUPrimitives();
	}

	static inline void fillVkAccelerationStructureInstance(VkAccelerationStructureInstanceKHR& as, uint64_t address)
	{
		as.accelerationStructureReference = address;
		as.mask = 0xFF;

		// NOTE: VK_GEOMETRY_INSTANCE_FORCE_OPAQUE_BIT_KHR // Faster.
		//       VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR // Two side.
		as.flags = VK_GEOMETRY_INSTANCE_FORCE_OPAQUE_BIT_KHR; // We current just use opaque bit.
		as.instanceShaderBindingTableRecordOffset = 0;
	}

	// TODO: Add some cache.
	void GLTFMeshComponent::onPerViewPerframeCollect(
		PerframeCollected& collector, 
		const PerframeCameraView& cameraView, 
		const ICamera* camera) const
	{
		Super::onPerViewPerframeCollect(collector, cameraView, camera);
		auto materialComp = getNode()->getComponent<GLTFMaterialComponent>();

		if (m_gltfGPU == nullptr || m_gltfAsset == nullptr || m_gltfMeshId < 0)
		{
			return;
		}

		if (!m_gltfGPU->isReady())
		{
			return;
		}

		GPUObjectGLTFPrimitive templatePrimitive { };
		templatePrimitive.basicData = getNode()->getObjectBasicData(cameraView, camera);

		const auto& meshes = m_gltfAsset->getMeshes().at(m_gltfMeshId);

		VkAccelerationStructureInstanceKHR instanceTamplate{};
		{
			// We use local to translated world matrix to build a precision TLAS.
			math::mat4 temp = math::transpose(templatePrimitive.basicData.localToTranslatedWorld);
			// 
			memcpy(&instanceTamplate.transform, &temp, sizeof(VkTransformMatrixKHR));
		}
		const bool bASReady = graphics::getContext().isRaytraceSupport() && m_gltfGPU->isBLASInit();

		uint lod0MeshletCount = 0;
		uint meshletGroupCount = 0;
		for (uint32 primitiveId = 0; primitiveId < meshes.primitives.size(); primitiveId++)
		{
			uint32 primitiveObjectId = collector.gltfPrimitives.size();

			auto materialProxy = materialComp->getProxy(primitiveId);

			templatePrimitive.GLTFPrimitiveDetail = m_gltfGPU->getGPUScenePrimitiveDetailId(m_gltfMeshId, primitiveId);
			templatePrimitive.GLTFMaterialData = materialProxy->getGPUSceneId();

			lod0MeshletCount  += meshes.primitives[primitiveId].lod0meshletCount;
			meshletGroupCount += meshes.primitives[primitiveId].meshletGroupCount;

			collector.gltfPrimitives.push_back(templatePrimitive);

			if (bASReady)
			{
				VkDeviceAddress blasAddress = m_gltfGPU->getBLASDeviceAddress(m_gltfMeshId, primitiveId);
				// Update primitive object id, used for ray hit indexing object.
				instanceTamplate.instanceCustomIndex = primitiveObjectId;
				fillVkAccelerationStructureInstance(instanceTamplate, blasAddress);

				//
				collector.asInstances.asInstances.push_back(instanceTamplate);
			}
		}

		collector.gltfLod0MeshletCount.fetch_add(lod0MeshletCount);
		collector.gltfMeshletGroupCount.fetch_add(meshletGroupCount);
	}

	bool GLTFMeshComponent::setGLTFMesh(const AssetSaveInfo& asset, int32 meshId)
	{
		if (asset != m_gltfAssetInfo || meshId != m_gltfMeshId)
		{
			m_gltfMeshId = meshId;

			if (asset != m_gltfAssetInfo)
			{
				m_gltfAssetInfo = asset;
				reloadResource();
			}

			markDirty();
			return true;
		}
		return false;
	}

	void GLTFMeshComponent::postLoad()
	{
		if (!m_gltfAssetInfo.empty())
		{
			reloadResource();
		}
	}
}