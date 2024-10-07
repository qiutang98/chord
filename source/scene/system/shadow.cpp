#include <scene/scene.h>
#include <scene/system/shadow.h>
#include <fontawsome/IconsFontAwesome6.h>
#include <ui/ui_helper.h>
#include <renderer/renderer.h>
#include <shader/base.h>

using namespace chord;
using namespace chord::graphics;

constexpr uint32 kShadowUseMaxCacadeZEnable = 1;

static inline float logCascadeSplit(
	const float nearZ,
	const float farDepthPlane,
	const float nearDepthPlane,
	const float clipRange,
	const uint  cascadeId,
	const uint  cascadeCount,
	const float cascadeSplitLambda)
{
	float range = farDepthPlane - nearDepthPlane;
	float ratio = farDepthPlane / nearDepthPlane;

	// get current part factor.
	float p = float(cascadeId + 1) / float(cascadeCount);

	// get log scale factor and uniform scale factor.
	float logScale = nearDepthPlane * pow(abs(ratio), p);
	float uniformScale = nearDepthPlane + range * p;

	// final get split distance.
	float d = cascadeSplitLambda * (logScale - uniformScale) + uniformScale;
	return (d - nearZ) / clipRange;
}

static inline std::vector<CascadeInfo> cascadeSetup(
	const ICamera& camera, 
	const CascadeShadowMapConfig& config,
	const float4x4& invertViewProj, // Relative camera world space.
	const float3& lightDirection)
{
	// Always y up.
	const float3 upDir = float3(0.0, 1.0, 0.0);
	const float3 lightDir = normalize(lightDirection);

	std::vector<CascadeInfo> result(config.cascadeCount);

	const float nearZ = camera.getZNear();
	const float farZ  = camera.getZFar();

	// Relative camera world space.
	float3 frustumCorner[8];
	{
		frustumCorner[0] = float3(-1.0f,  1.0f, 1.0f);
		frustumCorner[1] = float3( 1.0f,  1.0f, 1.0f);
		frustumCorner[2] = float3( 1.0f, -1.0f, 1.0f);
		frustumCorner[3] = float3(-1.0f, -1.0f, 1.0f);

		frustumCorner[4] = float3(-1.0f,  1.0f, 0.0f);
		frustumCorner[5] = float3( 1.0f,  1.0f, 0.0f);
		frustumCorner[6] = float3( 1.0f, -1.0f, 0.0f);
		frustumCorner[7] = float3(-1.0f, -1.0f, 0.0f);

		for (uint i = 0; i < 8; i++)
		{
			float4 invCorner = invertViewProj * float4(frustumCorner[i], 1.0f);
			frustumCorner[i].x = invCorner.x / invCorner.w;
			frustumCorner[i].y = invCorner.y / invCorner.w;
			frustumCorner[i].z = invCorner.z / invCorner.w;
		}
	}

	float4x4 globalMatrix;
	{
		// Calculate center pos of view frustum.
		float3 frustumCenter = float3(0.0f);
		for (uint i = 0; i < 8; i++)
		{
			frustumCenter += frustumCorner[i];
		}
		frustumCenter /= 8.0f;

		float sphereRadius = 0.0f;
		for (uint i = 0; i < 8; ++i)
		{
			float dist = length(frustumCorner[i] - frustumCenter);
			sphereRadius = math::max(sphereRadius, dist);
		}

		float3 maxExtents = float3(sphereRadius);
		float3 minExtents = -maxExtents;

		float3 extents = maxExtents - minExtents;

		// create temporary view project matrix for cascade.
		float3 shadowCameraPos = frustumCenter - normalize(lightDir) * extents.z * 0.5f;

		float nearZProj = 0.0f;
		float farZProj = extents.z;

		float4x4 shadowView = math::lookAtRH(shadowCameraPos, frustumCenter, upDir);
		float4x4 shadowProj = math::orthoRH_ZO(
			minExtents.x,
			maxExtents.x,
			minExtents.y,
			maxExtents.y,
			farZProj, // Also reverse z for shadow depth.
			nearZProj);

		globalMatrix = shadowProj * shadowView;
	}

	const float clipRange = farZ - nearZ;
	const float minZ = nearZ + config.cascadeStartDistance;
	const float maxZ = nearZ + config.cascadeEndDistance;
	check(maxZ > minZ);

	std::vector<float> cascadeSplits(config.cascadeCount);



	// Get split factor.
	for (uint cascadeId = 0; cascadeId < config.cascadeCount; cascadeId++)
	{
		cascadeSplits[cascadeId] = logCascadeSplit(nearZ, maxZ, minZ, clipRange, cascadeId, config.cascadeCount, config.splitLambda);
	}

	// 
	std::vector<float> cascadeSphereRadius(config.cascadeCount);
	std::vector<float3> cascadeFrustumCenters(config.cascadeCount);
	for (uint cascadeId = 0; cascadeId < config.cascadeCount; cascadeId++)
	{
		float splitDist = cascadeSplits[cascadeId];
		float prevSplitDist = cascadeId == 0 ? (config.cascadeStartDistance) / clipRange : cascadeSplits[cascadeId - 1];

		float3 cascadeFrustumCorner[8];
		for (uint i = 0; i < 8; i++)
		{
			cascadeFrustumCorner[i] = frustumCorner[i];
		}

		// Calculate 4 corner world pos of cascade view frustum.
		for (uint i = 0; i < 4; i++)
		{
			float3 cornerRay = cascadeFrustumCorner[i + 4] - cascadeFrustumCorner[i]; // distance ray.

			float3 nearCornerRay = cornerRay * prevSplitDist;
			float3 farCornerRay = cornerRay * splitDist;

			cascadeFrustumCorner[i + 4] = cascadeFrustumCorner[i] + farCornerRay;
			cascadeFrustumCorner[i + 0] = cascadeFrustumCorner[i] + nearCornerRay;
		}

		// Calculate center pos of view frustum.
		float3 cascadeFrustumCenter = float3(0.0f);
		for (uint i = 0; i < 8; i++)
		{
			cascadeFrustumCenter += cascadeFrustumCorner[i];
		}
		cascadeFrustumCenter /= 8.0f;

		// Get view sphere bounds radius.
		float sphereRadius = 0.0f;
		for (uint i = 0; i < 8; ++i)
		{
			float dist = length(cascadeFrustumCorner[i] - cascadeFrustumCenter);
			sphereRadius = math::max(sphereRadius, dist);
		}
		// Round 16.
		cascadeSphereRadius[cascadeId] = ceil(sphereRadius * 16.0f) / 16.0f;
		
		// 
		cascadeFrustumCenters[cascadeId] = cascadeFrustumCenter;
	}

	for (uint cascadeId = 0; cascadeId < config.cascadeCount; cascadeId++)
	{
		float3 maxExtents = float3(cascadeSphereRadius[cascadeId]);
		float3 minExtents = -maxExtents;

		float3 cascadeExtents = maxExtents - minExtents;

		const uint lastCascade = config.cascadeCount - 1;
		if (kShadowUseMaxCacadeZEnable != 0)
		{
			cascadeExtents.z = cascadeSphereRadius[lastCascade] * 2.0f;
		}

		// 
		float radiusScale = cascadeSphereRadius[0] / cascadeSphereRadius[cascadeId];

		// create temporary view project matrix for cascade.
		float3 shadowCameraPos = cascadeFrustumCenters[cascadeId] - normalize(lightDir) * cascadeExtents.z * 0.5f;

		float nearZProj = 0.0f;
		float farZProj  = cascadeExtents.z;

		float4x4 shadowView = math::lookAtRH(shadowCameraPos, cascadeFrustumCenters[cascadeId], upDir);
		float4x4 shadowProj = math::orthoRH_ZO(
			minExtents.x,
			maxExtents.x,
			minExtents.y,
			maxExtents.y,
			farZProj, // Also reverse z for shadow depth.
			nearZProj);

		// Texel align.
		const float sMapSize = float(config.cascadeDim);
		float4x4 shadowViewProjMatrix = shadowProj * shadowView;
		float4 shadowOrigin = float4(0.0f, 0.0f, 0.0f, 1.0f);
		shadowOrigin = shadowViewProjMatrix * shadowOrigin;
		shadowOrigin *= (sMapSize / 2.0f);

		// Move to center uv pos
		float3 roundedOrigin = math::round(float3(shadowOrigin));
		float3 roundOffset = roundedOrigin - float3(shadowOrigin);
		roundOffset = roundOffset * (2.0f / sMapSize);
		roundOffset.z = 0.0f;

		// Push back round offset data to project matrix.
		shadowProj[3][0] += roundOffset.x;
		shadowProj[3][1] += roundOffset.y;

		// Final proj view matrix
		float4x4 shadowFinalViewProj = shadowProj * shadowView;
		float4x4 reverseToWorld = inverse(shadowFinalViewProj);

		// Build frustum plane.
		float3 p[8];
		float4 planes[6] { };
		{
			p[0] = float3(-1.0f,  1.0f, 1.0f);
			p[1] = float3( 1.0f,  1.0f, 1.0f);
			p[2] = float3( 1.0f, -1.0f, 1.0f);
			p[3] = float3(-1.0f, -1.0f, 1.0f);

			p[4] = float3(-1.0f,  1.0f, 0.0f);
			p[5] = float3( 1.0f,  1.0f, 0.0f);
			p[6] = float3( 1.0f, -1.0f, 0.0f);
			p[7] = float3(-1.0f, -1.0f, 0.0f);

			for (uint i = 0; i < 8; i++)
			{
				float4 invCorner = reverseToWorld * float4(p[i], 1.0f);
				p[i] = float3(invCorner) / invCorner.w;
			}

			// left
			float3 leftN = normalize(cross((p[4] - p[7]), (p[3] - p[7])));
			planes[Frustum::eLeft] = float4(leftN, -dot(leftN, p[7]));

			// down
			float3 downN = normalize(cross((p[6] - p[2]), (p[3] - p[2])));
			planes[Frustum::eDown] = float4(downN, -dot(downN, p[2]));

			// right
			float3 rightN = normalize(cross((p[6] - p[5]), (p[1] - p[5])));
			planes[Frustum::eRight] = float4(rightN, -dot(rightN, p[5]));

			// top
			float3 topN = normalize(cross((p[5] - p[4]), (p[0] - p[4])));
			planes[Frustum::eTop] = float4(topN, -dot(topN, p[4]));

			// front
			float3 frontN = normalize(cross((p[1] - p[0]), (p[3] - p[0])));
			planes[Frustum::eFront] = float4(frontN, -dot(frontN, p[0]));

			// back
			float3 backN = normalize(cross((p[5] - p[6]), (p[7] - p[6])));
			planes[Frustum::eBack] = float4(backN, -dot(frontN, p[6]));
		}

		{
			// Construct cascade shadow map corner position and reproject to world space.
			float3 position00_RS = float3(-1.0,  1.0, 1.0); // reverse z.
			float3 position11_RS = float3( 1.0, -1.0, 0.0); // reverse z.
			{
				float4 invCorner_00 = reverseToWorld * float4(position00_RS, 1.0f);
				position00_RS = float3(invCorner_00) / invCorner_00.w;

				float4 invCorner_11 = reverseToWorld * float4(position11_RS, 1.0f);
				position11_RS = float3(invCorner_11) / invCorner_11.w;
			}


			float4 v00 = globalMatrix * float4(position00_RS, 1.0f);
			float4 v11 = globalMatrix * float4(position11_RS, 1.0f);

			result[cascadeId].globalMatrix = globalMatrix;
			result[cascadeId].cascadeGlobalScale = float4(float3(1.0f) / abs(float3(v00) / v00.w - float3(v11) / v11.w), 1.0f);
		}

		// 
		result[cascadeId].orthoDepthConvertToView = { shadowProj[2][2], shadowProj[3][2], cascadeSphereRadius[cascadeId], radiusScale };
		result[cascadeId].viewProjectMatrix = shadowFinalViewProj;
		for (uint i = 0; i < 6; i++)
		{
			result[cascadeId].frustumPlanes[i] = planes[i];
		}
	}

	return result;
}

ShadowManager::ShadowManager()
	: ISceneSystem("Shadow", ICON_FA_SUN + std::string("  Shadow"))
{

}

const std::vector<CascadeInfo>& ShadowManager::update(const ApplicationTickData& tickData, const ICamera& camera, const float3& lightDirection)
{
	if (m_updateFrame != tickData.tickCount)
	{
		// Update once per frame.
		m_updateFrame = tickData.tickCount;


		//
		const float4x4& projectionWithZFar    = camera.getProjectMatrixExistZFar();
		const float4x4& relativeView          = camera.getRelativeCameraViewMatrix();
		const float4x4 relativeViewProjection = projectionWithZFar * relativeView;
		const float4x4 invertViewProj         = math::inverse(relativeViewProjection);

		// Update cascade infos.
		m_cacheCascadeInfos = cascadeSetup(camera, m_shadowConfig.cascadeConfig, invertViewProj, lightDirection);
	}

	return m_cacheCascadeInfos;
}

static inline bool drawCascadeConfig(CascadeShadowMapConfig& inout)
{
	bool bChangedValue = false;
	auto copyValue = inout;

	if (ImGui::CollapsingHeader("Cascade Shadow Setting"))
	{
		ui::beginGroupPanel("Cascade Config");
		ImGui::PushItemWidth(100.0f);
		{
			ImGui::DragInt("Count", &copyValue.cascadeCount, 1.0f, 1, (int)kMaxCascadeCount);
			ImGui::DragInt("Realtime Count", &copyValue.realtimeCascadeCount, 1.0f, 1, copyValue.cascadeCount);

			int cascadeDim = copyValue.cascadeDim;
			ImGui::DragInt("Dimension", &cascadeDim, 512, 512, 4096);
			copyValue.cascadeDim = cascadeDim;

			ImGui::DragFloat("Split Lambda", &copyValue.splitLambda, 0.01f, 0.00f, 1.00f);

			ImGui::DragFloat("Start Distance", &copyValue.cascadeStartDistance, 10.0f, 0.0f, 200.0f);
			ImGui::DragFloat("End Distance", &copyValue.cascadeEndDistance, 10.0f, copyValue.cascadeStartDistance + 1.0f, 2000.0f);


		}
		ImGui::PopItemWidth();
		ui::endGroupPanel();

		ImGui::Separator();

		ui::beginGroupPanel("Filtered Config");
		ImGui::PushItemWidth(100.0f);
		{
			ImGui::Checkbox("CoD PCF", &copyValue.bContactHardenPCF);

			ImGui::DragFloat("Light Size", &copyValue.lightSize, 0.01f, 0.5f, 10.0f);

			ImGui::DragInt("Min PCF", &copyValue.minPCFSampleCount, 1, 2, 127);
			ImGui::DragInt("Max PCF", &copyValue.maxPCFSampleCount, 1, copyValue.minPCFSampleCount, 128);
			ImGui::DragInt("Min Blocker Search", &copyValue.minBlockerSearchSampleCount, 1, 2, 127);
			ImGui::DragInt("Max Blocker Search", &copyValue.maxBlockerSearchSampleCount, 1, copyValue.minBlockerSearchSampleCount, 128);
			ImGui::DragFloat("Blocker Range Scale", &copyValue.blockerSearchMaxRangeScale, 0.01f, 0.01f, 1.0f);

			copyValue.blockerSearchMaxRangeScale = math::clamp(copyValue.blockerSearchMaxRangeScale, 0.01f, 1.0f);
		}
		ImGui::PopItemWidth();
		ui::endGroupPanel();

		ImGui::Separator();

		ui::beginGroupPanel("Bias Config");
		ImGui::PushItemWidth(100.0f);
		{
			ImGui::DragFloat("Cascade Border Jitter", &copyValue.cascadeBorderJitterCount, 0.01f, 1.0, 4.0f);

			ImGui::DragFloat("PCF Bias Scale", &copyValue.pcfBiasScale, 0.01f, 0.0f, 40.0f);
			ImGui::DragFloat("Bias Lerp Min", &copyValue.biasLerpMin_const, 0.01f, 1.0f, 10.0f);
			ImGui::DragFloat("Bias Lerp Max", &copyValue.biasLerpMax_const, 0.01f, 5.0f, 100.0f);

			ImGui::DragFloat("Normal Offset Scale", &copyValue.normalOffsetScale, 0.1f, 0.0f, 100.0f);
			ImGui::DragFloat("Bias Const", &copyValue.shadowBiasConst, 0.01f, -5.0f, 5.0f);
			ImGui::DragFloat("Bias Slope", &copyValue.shadowBiasSlope, 0.01f, -5.0f, 5.0f);

			ImGui::DragInt("ShadowMask Blur Pass Count", &copyValue.shadowMaskFilterCount, 0, 5);
		}
		ImGui::PopItemWidth();
		ui::endGroupPanel();
	}

	copyValue.minPCFSampleCount = math::min(copyValue.minPCFSampleCount, 127);
	copyValue.maxPCFSampleCount = math::min(copyValue.maxPCFSampleCount, 128);
	copyValue.minBlockerSearchSampleCount = math::min(copyValue.minBlockerSearchSampleCount, 127);
	copyValue.maxBlockerSearchSampleCount = math::min(copyValue.maxBlockerSearchSampleCount, 128);

	copyValue.cascadeDim = math::clamp(getNextPOT(copyValue.cascadeDim), 512U, 4096U);
	copyValue.cascadeCount = math::clamp(copyValue.cascadeCount, 1, (int)kMaxCascadeCount);

	copyValue.biasLerpMax_const = math::max(copyValue.biasLerpMax_const, copyValue.biasLerpMin_const);

	copyValue.shadowMaskFilterCount = math::clamp(copyValue.shadowMaskFilterCount, 0, 5);
	if (copyValue != inout)
	{
		inout = copyValue;
		bChangedValue = true;
	}

	return bChangedValue;
}

void ShadowManager::onDrawUI(SceneRef scene)
{
	auto cacheConfig = m_shadowConfig;

	bool bChange = false;
	
	bChange |= drawCascadeConfig(m_shadowConfig.cascadeConfig);
}