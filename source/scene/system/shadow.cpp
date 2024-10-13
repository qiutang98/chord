#include <scene/scene.h>
#include <scene/system/shadow.h>
#include <fontawsome/IconsFontAwesome6.h>
#include <ui/ui_helper.h>
#include <renderer/renderer.h>
#include <shader/base.h>

using namespace chord;
using namespace chord::graphics;

ShadowManager::ShadowManager()
	: ISceneSystem("Shadow", ICON_FA_SUN + std::string("  Shadow"))
{

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
			ImGui::DragFloat("Far Split Lambda", &copyValue.farCascadeSplitLambda, 0.01f, 0.00f, 1.00f);

			ImGui::DragFloat("Near Start Distance", &copyValue.cascadeStartDistance, 10.0f, 0.0f, 200.0f);
			ImGui::DragFloat("Near End Distance", &copyValue.cascadeEndDistance, 10.0f, copyValue.cascadeStartDistance + 1.0f, 2000.0f);

			ImGui::DragFloat("Far End Distance", &copyValue.farCascadeEndDistance, 10.0f, copyValue.cascadeEndDistance + 200.0f, 2000.0f);
		}
		ImGui::PopItemWidth();
		ui::endGroupPanel();

		ImGui::Separator();

		ui::beginGroupPanel("Filtered Config");
		ImGui::PushItemWidth(100.0f);
		{
			ImGui::Checkbox("CoD PCF", &copyValue.bContactHardenPCF);

			ImGui::DragFloat("Light Size", &copyValue.lightSize, 0.01f, 0.5f, 10.0f);
			ImGui::DragFloat("Radius Scale", &copyValue.radiusScaleFixed, 0.1f, 0.1f, 10.0f);
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