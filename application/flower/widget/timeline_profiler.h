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
	struct Style
	{
		float scrollBarSize = 15.0f;
		float barHeight = 1.0f;
		float calibrationWidth = 4.0f;

		ImVec4 bgTextColor = ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
		ImVec4 fgTextColor = ImVec4(0.9f, 0.9f, 0.9f, 1.0f);
	} m_style;

	double m_timelineScale;
	double m_timelineOffset = 0.0;

	chord::int32 m_maxTime = 100;
};