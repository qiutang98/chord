#include <renderer/gpu_scene.h>
#include <renderer/renderer.h>

#include <shader/gltf.h>
#include <shader_compiler/shader.h>
#include <shader/gpuscene.hlsl>
#include <graphics/helper.h>
#include <renderer/render_helper.h>
#include <renderer/lighting.h>
#include <renderer/fullscreen.h>
#include <application/application.h>
#include <renderer/compute_pass.h>
namespace chord
{
	using namespace graphics;

	class GPUSceneScatterUploadCS : public GlobalShader
	{
	public:
		DECLARE_SUPER_TYPE(GlobalShader);

		static void modifyCompileEnvironment(ShaderCompileEnvironment& o, int32 PermutationId)
		{
			o.setDefine("GPUSCENE_SCATTER_UPLOAD", true);
		}
	};
	IMPLEMENT_GLOBAL_SHADER(GPUSceneScatterUploadCS, "resource/shader/gpuscene.hlsl", "mainCS", EShaderStage::Compute);

	void chord::GPUSceneScatterUpload(
		graphics::GraphicsOrComputeQueue& computeQueue,
		graphics::PoolBufferGPUOnlyRef GPUSceneBuffer,
		std::vector<math::uvec4>&& inIndexingData,
		std::vector<math::uvec4>&& inCollectedData)
	{
		std::vector<math::uvec4> collectedData = inCollectedData;
		std::vector<math::uvec4> indexingData = inIndexingData;

		auto indexingDataBuffer = getContext().getBufferPool().createHostVisibleCopyUpload(
			"indexingDataBuffer", 
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, 
			SizedBuffer(sizeof(math::uvec4) * indexingData.size(), (void*)indexingData.data()));

		auto collectedDataBuffer = getContext().getBufferPool().createHostVisibleCopyUpload(
			"collectedDataBuffer",
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
			SizedBuffer(sizeof(math::uvec4) * collectedData.size(), (void*)collectedData.data()));

		GPUSceneScatterUploadPushConsts pushConst { };
		pushConst.indexingBufferId = asSRV(computeQueue, indexingDataBuffer);
		pushConst.collectedUploadDataBufferId = asSRV(computeQueue, collectedDataBuffer);
		pushConst.uploadCount = indexingData.size();
		pushConst.GPUSceneBufferId = asUAV(computeQueue, GPUSceneBuffer);

		math::uvec3 dispatchParameter = { divideRoundingUp(pushConst.uploadCount, uint32(kGPUSceneScatterUploadDimX)), 1, 1 };

		auto computePipe = getContext().computePipe<GPUSceneScatterUploadCS>("GPUSceneScatterUploadCS");
		addComputePass2(computeQueue, "GPUSceneScatterUpload", computePipe, pushConst, dispatchParameter);

		// Post update, change stage to SRV.
		asSRV(computeQueue, GPUSceneBuffer);
	}

	void chord::enqueueGPUSceneUpdate()
	{
		// Only update once in once frame.
		auto& GPUScene = Application::get().getGPUScene();
		if (GPUScene.m_frameCounter == getFrameCounter())
		{
			return;
		}

		CallOnceInOneFrameEvent::add([](const ApplicationTickData& tickData, graphics::GraphicsQueue& graphics)
		{
			auto& GPUScene = Application::get().getGPUScene();
			GPUScene.update(tickData.tickCount, graphics);
		});
	}

	GPUScene::GPUScene()
		: m_gltfPrimitiveDataPool("GLTF.PrimitiveData")
		, m_gltfPrimitiveDetailPool("GLTF.PrimitiveDetail")
		, m_gltfMaterialPool("GLTF.Material")
	{
		
	}

	GPUScene::~GPUScene()
	{
		
	}

	uint32 GPUScene::getBRDFLutSRV() const
	{
		auto range = graphics::helper::buildBasicImageSubresource();
		auto viewType = VK_IMAGE_VIEW_TYPE_2D;

		if (m_brdf == nullptr)
		{
			return getContext().getBuiltinResources().transparent->getSRV(range, viewType);
		}

		return m_brdf->get().requireView(
			range,
			viewType, true, false).SRV.get();
	}

	void GPUScene::update(uint64 frameCounter, graphics::GraphicsOrComputeQueue& computeQueue)
	{
		// Init brdf if no exist.
		if (m_brdf == nullptr)
		{
			computeQueue.beginCommand({ computeQueue.getCurrentTimeline() });
			{
				m_brdf = computeBRDFLut(computeQueue, 512);
			}
			computeQueue.endCommand();
		}

		// Prereturn if noth
		if (!shouldFlush())
		{
			return;
		}

		check(frameCounter == getFrameCounter());
		if (m_frameCounter == frameCounter)
		{
			return;
		}

		// Only update once in one frame.
		m_frameCounter = frameCounter;

		computeQueue.beginCommand({ computeQueue.getCurrentTimeline() });
		{
			// 
			m_gltfMaterialPool.flushUpdateCommands(computeQueue);
			m_gltfPrimitiveDataPool.flushUpdateCommands(computeQueue);
			m_gltfPrimitiveDetailPool.flushUpdateCommands(computeQueue);

		}
		computeQueue.endCommand();
	}
}

