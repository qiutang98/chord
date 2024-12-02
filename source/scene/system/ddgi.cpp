#include <scene/scene.h>
#include <scene/system/ddgi.h>
#include <fontawsome/IconsFontAwesome6.h>
#include <ui/ui_helper.h>
#include <renderer/renderer.h>
#include <shader/base.h>

using namespace chord;
using namespace chord::graphics;

DDGIManager::DDGIManager()
	: ISceneSystem("DDGI", ICON_FA_LIGHTBULB + std::string("  DDGI"))
{

}

static inline bool drawDDGIConfig(DDGIConfigCPU& inout)
{
	bool bChangedValue = false;


	if (ImGui::CollapsingHeader("DDGI Setting"))
	{
		for (uint i = 0; i < 4; i++)
		{
			auto copyValue = inout.volumeConfigs[i];

			ImGui::PushID(i);

			ImGui::Separator();
			std::string titleName = std::format("DDGI cascade #{}", i); // std::to_string(i);
			ImGui::TextDisabled(titleName.c_str());
			ImGui::Spacing();

			ui::beginGroupPanel("Raytrace Config");
			ImGui::PushItemWidth(100.0f);
			{
				ImGui::DragInt("Rayhit sample texture LOD", &copyValue.rayHitSampleTextureLod, 1.0f, 2, 10);
			}
			ImGui::PopItemWidth();
			ui::endGroupPanel();

			ui::beginGroupPanel("Blend Config");
			ImGui::PushItemWidth(100.0f);
			{
				ImGui::DragFloat("Hysteresis", &copyValue.hysteresis, 0.01f, 0.00f, 1.00f);
				ImGui::InputFloat("Distance Exponent", &copyValue.probeDistanceExponent);
			}
			ImGui::PopItemWidth();
			ui::endGroupPanel();

			ui::beginGroupPanel("Sample Config");
			ImGui::PushItemWidth(100.0f);
			{
				ImGui::InputFloat("Normal Bias Offset", &copyValue.probeNormalBias);
				ImGui::InputFloat("View Bias Offset", &copyValue.probeViewBias);
			}
			ImGui::PopItemWidth();
			ui::endGroupPanel();

			copyValue.probeDistanceExponent = math::max(copyValue.probeDistanceExponent, 1.0f);
			copyValue.probeNormalBias = math::max(copyValue.probeNormalBias, 0.0f);
			copyValue.probeViewBias = math::max(copyValue.probeViewBias, 0.0f);

			if (copyValue != inout.volumeConfigs[i])
			{
				inout.volumeConfigs[i] = copyValue;
				bChangedValue = true;
			}

			ImGui::PopID();
		}
	}

	return bChangedValue;
}

void DDGIManager::onDrawUI(SceneRef scene)
{
	auto cacheConfig = m_config;
	bool bChange = false;

	bChange |= drawDDGIConfig(m_config);
}