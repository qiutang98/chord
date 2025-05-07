#include "timeline_profiler.h"
#include "../flower.h"
#include <ui/imgui/implot.h>
#include <ui/ui_helper.h>

using namespace chord::graphics;
using namespace chord;
using namespace chord::ui;

constexpr const char* kIconProfilerTitle = ICON_FA_CLOCK ;

WidgetTimelineProfiler::WidgetTimelineProfiler()
	: IWidget(combineIcon("Profiler", kIconProfilerTitle).c_str(), combineIcon("Profiler", kIconProfilerTitle).c_str())
{

}

void WidgetTimelineProfiler::onInit()
{

}

void WidgetTimelineProfiler::onTick(const chord::ApplicationTickData& tickData)
{

}

void WidgetTimelineProfiler::onVisibleTick(const chord::ApplicationTickData& tickData)
{

}

void WidgetTimelineProfiler::onRelease()
{

}
