#pragma once

#include <ui/widget.h>

class WidgetTimelineProfiler : public chord::IWidget
{
public:
	explicit WidgetTimelineProfiler();

protected:
	// event init.
	virtual void onInit() override;

	// event always tick.
	virtual void onTick(const chord::ApplicationTickData& tickData) override;

	// event when widget visible tick.
	virtual void onVisibleTick(const chord::ApplicationTickData& tickData) override;

	// event release.
	virtual void onRelease() override;

private:
	struct StyleOptions
	{
		int maxDepth = 32;
		int maxTime  = 80;

		float barHeight = 25;
		float BarPadding = 2;
		float ScrollBarSize = 15.0f;

		ImVec4 BarColorMultiplier = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
		ImVec4 BGTextColor = ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
		ImVec4 FGTextColor = ImVec4(0.9f, 0.9f, 0.9f, 1.0f);
		ImVec4 BarHighlightColor = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);

		bool DebugMode = false;
	};

	bool m_bPaused = false;
	bool m_bPauseThreshold = false;
	bool m_bSelectingRange = false;

	//
	float m_pauseThresholdTime = 100.0f;

	//
	float  m_timelineScale  = 5.0f;
	float2 m_timelineOffset = float2(0.0f, 0.0f);

	//
	float m_rangeSelectionStart = 0.0f;
	char  m_searchString[128];
};