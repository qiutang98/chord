#include "viewport.h"

#include <ui/ui_helper.h>

using namespace chord::graphics;
using namespace chord;
using namespace chord::ui;

const static std::string kIconViewport = ICON_FA_EARTH_ASIA;

WidgetViewport::WidgetViewport(size_t index)
	: IWidget(
		combineIcon("Viewport", kIconViewport).c_str(),
		combineIcon(combineIndex("Viewport", index), kIconViewport).c_str())
	, m_index(index)
{
	m_flags = ImGuiWindowFlags_NoScrollWithMouse;
}

void WidgetViewport::onInit()
{

}

void WidgetViewport::onBeforeTick(const ApplicationTickData& tickData)
{
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
}

void WidgetViewport::onTick(const ApplicationTickData& tickData)
{

}

void WidgetViewport::onVisibleTick(const ApplicationTickData& tickData)
{

}

void WidgetViewport::onAfterTick(const ApplicationTickData& tickData)
{
	ImGui::PopStyleVar(1);
}

void WidgetViewport::onRelease()
{

}
