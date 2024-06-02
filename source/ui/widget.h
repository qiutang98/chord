#pragma once

#include <utils/utils.h>
#include <ui/ui.h>
#include <application/application.h>

namespace chord
{
	inline std::string combineIcon(const std::string& name, const std::string& icon)
	{
		return std::format("   {}  {}", icon, name);
	}

	inline std::string combineIndex(const std::string& name, size_t index)
	{
		return std::format("{} #{}", name, index);
	}

	class IWidget : NonCopyable
	{
	public:
		explicit IWidget(const char* widgetName, const char* name);

		virtual ~IWidget()
		{

		}

		const std::string& getName() const 
		{ 
			return m_name; 
		}

		const std::string& getWidgetName() const 
		{ 
			return m_widgetName; 
		}

		bool setVisible(bool bVisible) 
		{ 
			if (m_bShow != bVisible)
			{
				m_bShow = bVisible;
				return true;
			}
			return false;
		}

		bool getVisible() const 
		{ 
			return m_bShow; 
		}

	public:
		// Init current widget.
		void init() 
		{ 
			onInit(); 
		}

		// Tick current widget.
		void tick(const ApplicationTickData& tickData);

		// Release widget.
		void release() 
		{ 
			onRelease(); 
		}

	protected:
		// Event on init.
		virtual void onInit() { }

		// Event on widget visible state change from show to hide. sync on tick function first.
		virtual void onHide(const ApplicationTickData& tickData) {  }

		// Event on widget show state change from hide to show.
		virtual void onShow(const ApplicationTickData& tickData) {  }

		// Event before tick.
		virtual void onBeforeTick(const ApplicationTickData& tickData) { }

		// Event always tick.
		virtual void onTick(const ApplicationTickData& tickData) {  }

		// Event when widget visible tick, draw imgui logic here.
		virtual void onVisibleTick(const ApplicationTickData& tickData) {  }

		// Event after tick.
		virtual void onAfterTick(const ApplicationTickData& tickData) { }

		// Tick with graphics command, always run.
		virtual void onTickCmd(const ApplicationTickData& tickData, graphics::CommandList& commandList) {  }

		// Tick with graphics command, only run when widget is visible.
		virtual void onVisibleTickCmd(const ApplicationTickData& tickData, graphics::CommandList& commandList) {  }

		// Event release.
		virtual void onRelease() {  }

	protected:
		// Window show flags.
		ImGuiWindowFlags m_flags = 0;

		// Widget show state.
		bool m_bShow;

		// Widget prev frame show state.
		bool m_bPrevShow;

		// 
		std::string m_widgetName;
		std::string m_name;

	private:
		// Unique id of this widget.
		const int32 m_uniqueId;
	};

	class WidgetManager : NonCopyable
	{
	public:
		WidgetManager() = default;

		template<typename T, typename... Args>
		CHORD_NODISCARD T* addWidget(Args... args)
		{
			static_assert(std::is_base_of_v<IWidget, T>, "T must derived from IWidget.");
			static_assert(std::is_constructible_v<T, Args...>, "T must constructable with default constructor.");

			auto newWidget = std::make_shared<T>(args...);

			// Init and push in vector.
			newWidget->init();
			m_widgets.push_back(newWidget);

			return newWidget.get();
		}

		template<typename T>
		CHORD_NODISCARD bool removeWidget(T* handle)
		{
			size_t i = 0;
			for (auto& widget : m_widgets)
			{
				if (widget.get() == handle)
				{
					break;
				}
				i++;
			}

			if (i >= m_widgets.size())
			{
				return false;
			}

			// Call release before.
			m_widgets[i]->release();

			// Remove element but still keep widget order.
			{
				auto it = m_widgets.begin() + i;
				std::rotate(it, it + 1, m_widgets.end());

				m_widgets.pop_back();
			}

			return true;
		}

		void clearAllWidgets()
		{
			for (int32 i = m_widgets.size() - 1; i >= 0; i--)
			{
				m_widgets[i]->release();
			}
			m_widgets.clear();
		}

		void release()
		{
			clearAllWidgets();
		}

		void tick(const ApplicationTickData& tickData)
		{
			for (auto& widget : m_widgets)
			{
				widget->tick(tickData);
			}
		}

	protected:
		std::vector<std::shared_ptr<IWidget>> m_widgets;
	};
}