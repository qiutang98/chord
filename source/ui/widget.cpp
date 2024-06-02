#include <ui/widget.h>

namespace chord
{
	using namespace graphics;

	static int32 requireWidgetUniqueId()
	{
		static int32 uniqueId = 0;

		if (uniqueId == std::numeric_limits<int32>::max())
		{
			uniqueId = 0;
		}

		return uniqueId++;
	}

	IWidget::IWidget(const char* widgetName, const char* name)
		: m_name(name)
		, m_widgetName(widgetName)
		, m_bShow(true)
		, m_bPrevShow(false)
		, m_uniqueId(requireWidgetUniqueId())
	{

	}

	void IWidget::tick(const ApplicationTickData& tickData)
	{
		// Sync visible state.
		if (m_bPrevShow != m_bShow)
		{
			m_bPrevShow = m_bShow;
			if (m_bShow)
			{
				// Last time is hide, current show.
				onShow(tickData);
			}
			else
			{
				// Last time show, current hide.
				onHide(tickData);
			}
		}

		onBeforeTick(tickData);

		onTick(tickData);

		if (m_bShow)
		{
			ImGui::SetNextWindowSize(ImVec2(800, 400), ImGuiCond_FirstUseEver);

			if (ImGui::Begin(m_name.c_str(), &m_bShow, m_flags, true))
			{
				ImGui::PushID(m_uniqueId);
				onVisibleTick(tickData);
				ImGui::PopID();
			}
			ImGui::End(true);
		}

		onAfterTick(tickData);

		// Add on tick command.
		if (auto viewport = ImGui::GetCurrentWindow()->Viewport)
		{
			if (auto* vrd = (ImGuiViewportData*)viewport->RendererUserData)
			{
				vrd->onTickWithCmds.add([this](const ApplicationTickData& tickData, graphics::CommandList& commandList)
				{
					onTickCmd(tickData, commandList);

					if (m_bShow)
					{
						onVisibleTickCmd(tickData, commandList);
					}
				});
			}
		}
	}
}