#include <scene/scene.h>
#include <scene/system/postprocessing.h>
#include <fontawsome/IconsFontAwesome6.h>
#include <ui/ui_helper.h>
#include <renderer/renderer.h>
#include <shader/base.h>

using namespace chord;
using namespace chord::graphics;

PostProcessingManager::PostProcessingManager()
	: ISceneSystem("PostProcessing", ICON_FA_STAR_HALF_STROKE + std::string("  PostProcessing"))
{

}

void PostProcessingManager::onDrawUI(SceneRef scene)
{
	bool bChangedValue = false;
	auto copyValue = m_config;

	if (ImGui::CollapsingHeader("AutoExposure Setting"))
	{
		ui::beginGroupPanel("Bias Config");
		ImGui::PushItemWidth(100.0f);
		ImGui::Checkbox("Auto Exposure", (bool*)&copyValue.bAutoExposureEnable);
		if (copyValue.bAutoExposureEnable)
		{
			ImGui::DragFloat("Low Percent", &copyValue.autoExposureLowPercent, 1e-3f, 0.01f, 0.5f);
			ImGui::DragFloat("High Percent", &copyValue.autoExposureHighPercent, 1e-3f, 0.5f, 0.99f);

			ImGui::DragFloat("Min Brightness", &copyValue.autoExposureMinBrightness, 0.5f, -9.0f, 9.0f);
			ImGui::DragFloat("Max Brightness", &copyValue.autoExposureMaxBrightness, 0.5f, -9.0f, 9.0f);

			ImGui::DragFloat("Speed Down", &copyValue.autoExposureSpeedDown, 0.1f, 0.0f);
			ImGui::DragFloat("Speed Up", &copyValue.autoExposureSpeedUp, 0.1f, 0.0f);

			ImGui::DragFloat("Exposure Compensation", &copyValue.autoExposureExposureCompensation, 0.01f, 0.1f, 1.0f);
		}
		else
		{
			ImGui::DragFloat("Fix exposure", &copyValue.autoExposureFixExposure, 0.1f, 0.1f, 100.0f);
		}
		ImGui::PopItemWidth();
		ui::endGroupPanel();
	}

	if (ImGui::CollapsingHeader("Bloom Setting"))
	{
		ui::beginGroupPanel("Bias Config");
		ImGui::PushItemWidth(100.0f);

		ImGui::DragFloat("Intensity", &copyValue.bloomIntensity, 0.01f, 0.0f, 1.0);
		ImGui::DragFloat("Radius", &copyValue.bloomRadius, 0.01f, 0.0f, 1.0);

		ImGui::DragFloat("Threshold", &copyValue.bloomThreshold, 0.01f, 0.0f, 1.0f);
		ImGui::DragFloat("Threshold soft", &copyValue.bloomThresholdSoft, 0.01f, 0.0f, 1.0f);

		ImGui::DragFloat("Sample count", &copyValue.bloomSampleCount, 1.0f, 4.0f, 10.0f);
		ImGui::DragFloat("Gaussian sigma", &copyValue.bloomGaussianSigma, 0.01f, 0.01f, 1.0f);

		ImGui::PopItemWidth();
		ui::endGroupPanel();
	}

	if (copyValue != m_config)
	{
		m_config = copyValue;
		bChangedValue = true;
	}
}