#include "timeline_profiler.h"
#include "../flower.h"
#include <ui/imgui/implot.h>
#include <ui/ui_helper.h>

using namespace chord::graphics;
using namespace chord;
using namespace chord::ui;

constexpr const char* kIconProfilerTitle = ICON_FA_CLOCK ;


constexpr double kTimelineMs      = 50.0; //
constexpr double kTimelineMsx20   = kTimelineMs      * 20.0;
constexpr double kTimelineSeconds = kTimelineMsx20   * 50.0;
constexpr double kTimelineMinutes = kTimelineSeconds * 60.0;
constexpr double kTimelineHours   = kTimelineMinutes * 60.0;

enum ETimelineInspectedUnit
{
	Ms,
	Msx20,
	Seconds,
	Minutes,
	Hours,
};

WidgetTimelineProfiler::WidgetTimelineProfiler()
	: IWidget(combineIcon("Profiler", kIconProfilerTitle).c_str(), combineIcon("Profiler", kIconProfilerTitle).c_str())
{

}

void WidgetTimelineProfiler::onInit()
{
	m_timelineScale = kTimelineMsx20;
}

void WidgetTimelineProfiler::onTick(const chord::ApplicationTickData& tickData)
{

}

void WidgetTimelineProfiler::onVisibleTick(const chord::ApplicationTickData& tickData)
{
	ImDrawList* pDraw = ImGui::GetWindowDrawList();
	static const ImGuiID timelineID = ImGui::GetID("WidgetTimelineProfiler");

	// 
	const float unitCalibrationWidth = ImGui::GetTextLineHeight() * m_style.calibrationWidth;
	const float barHeight = m_style.barHeight * ImGui::GetTextLineHeightWithSpacing();

	// 
	math::vec2 timelineRegion = ImGui::CalcItemSize(ImVec2(0.0f, 0.0f), ImGui::GetContentRegionAvail().x, ImGui::GetContentRegionAvail().y);

	ImRect timelineRect(ImGui::GetCursorScreenPos(), math::vec2(ImGui::GetCursorScreenPos()) + timelineRegion);
	ImGui::ItemSize(timelineRect.GetSize());

	double calibrationUnit = kTimelineHours;
	if (m_timelineScale < calibrationUnit) { calibrationUnit = kTimelineMinutes; }
	if (m_timelineScale < calibrationUnit) { calibrationUnit = kTimelineSeconds; }
	if (m_timelineScale < calibrationUnit) { calibrationUnit = kTimelineMsx20;   }
	if (m_timelineScale < calibrationUnit) { calibrationUnit = kTimelineMs;      }


	double modUnits = m_timelineOffset; // math::mod(m_timelineOffset, calibrationUnit);
	int32 offsetUnits = int32(m_timelineOffset / calibrationUnit);
	// Offset how many unit?

	math::vec2 cursor = math::vec2(timelineRect.Min) + float(m_timelineOffset);



	const uint32 maxTime = timelineRect.GetWidth() / unitCalibrationWidth;

	if (ImGui::ItemAdd(timelineRect, timelineID))
	{
		ImGui::PushClipRect(timelineRect.Min, timelineRect.Max, true);

		pDraw->AddRectFilled(timelineRect.Min, math::vec2(timelineRect.Max.x, timelineRect.Min.y + barHeight), ImColor(0.0f, 0.0f, 0.0f, 0.1f));
		pDraw->AddRect(
			math::vec2(timelineRect.Min.x - 100.0f, timelineRect.Min.y), // -100.0f and +100.0f to hide timeline rect left and right line.
			math::vec2(timelineRect.Max.x + 100.0f, timelineRect.Min.y + barHeight), ImColor(1.0f, 1.0f, 1.0f, 0.4f));
		
		const float timelineWidth = timelineRect.GetWidth() * m_timelineScale;
		const float perCalibrationDistance = timelineWidth / maxTime;

		for (int32 i = 0; i < maxTime; i++)
		{
			math::vec2 calibrationPos = { cursor.x + perCalibrationDistance * i, timelineRect.Min.y };
			pDraw->AddLine(
				calibrationPos + math::vec2(0.0f, 0.0f),
				calibrationPos + math::vec2(0.0f, barHeight * 0.5f), ImColor(m_style.bgTextColor));

			if (i % 2 == 0)
			{
				pDraw->AddRectFilled(
					calibrationPos + math::vec2(0.0f, barHeight),
					calibrationPos + math::vec2(perCalibrationDistance, timelineRect.Max.y), ImColor(1.0f, 1.0f, 1.0f, 0.0075f));

				const char* pBarText;
				ImFormatStringToTempBuffer(&pBarText, nullptr, "%d ms", i);
				pDraw->AddText(calibrationPos + math::vec2(ImGui::GetTextLineHeight() * 0.25f, 0.0f), ImColor(m_style.bgTextColor), pBarText);
			}
		}

		ImGui::PopClipRect();
	}

	if (ImGui::IsWindowFocused())
	{
		// Zoom
		float zoomDelta = 0.0f;
		if (ImGui::IsKeyDown(ImGuiKey_LeftCtrl) || ImGui::IsKeyDown(ImGuiKey_RightCtrl))
		{
			zoomDelta += ImGui::GetIO().MouseWheel / 5.0f;
		}
			
		// Logarithmic scale
		if (zoomDelta != 0.0f)
		{
			double logScale = log(m_timelineScale);
			logScale += zoomDelta;
			double newScale = ImClamp(exp(logScale), 1.0, kTimelineHours);

			//
			double scaleFactor = newScale / m_timelineScale;
			m_timelineScale *= scaleFactor;

			// 
			math::vec2 mousePos = math::vec2(ImGui::GetMousePos()) - math::vec2(timelineRect.Min);
			m_timelineOffset = mousePos.x - (mousePos.x - m_timelineOffset) * scaleFactor;
		}
	}
}

void WidgetTimelineProfiler::onRelease()
{

}
