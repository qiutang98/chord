#pragma once
#include <ui/ui.h>

namespace chord::ui
{
	extern void disableLambda(std::function<void()>&& lambda, bool bDisable);
	extern void hoverTip(const char* desc);
	extern bool treeNodeEx(const char* idLabel, const char* showlabel, ImGuiTreeNodeFlags flags);

	class ImGuiPopupSelfManagedOpenState
	{
	public:
		explicit ImGuiPopupSelfManagedOpenState(
			const std::string& titleName,
			ImGuiWindowFlags flags);

		void draw();
		bool open();

	protected:
		virtual void onDraw() { }
		virtual void onClosed() { }

	private:
		const UUID m_uuid = chord::generateUUID();

		ImGuiWindowFlags m_flags;
		std::string m_popupName;
		bool m_bShouldOpenPopup = false;
		bool m_bPopupOpenState  = true;
	};

	namespace fontIcon
	{
		constexpr const char* clone      = "\ue16f";
		constexpr const char* paste      = "\ue11e";
		constexpr const char* trashCan   = "\ue107";
		constexpr const char* message    = "\ue25c";
		constexpr const char* magnifying = "\ue1a3";
		constexpr const char* console    = "\ue756";
		constexpr const char* threePoint = "\ue10c";
		constexpr const char* timer      = "\ued5a";
		constexpr const char* fileIn     = "\ue1a5";
		constexpr const char* save       = "\ue74e";
		constexpr const char* saveall    = "\uea35";
		constexpr const char* newFile    = "\uf164";
		constexpr const char* file       = "\ue7c3";
		constexpr const char* file2      = "\ue729";
		constexpr const char* folder     = "\uf89a";
		constexpr const char* folder2    = "\uf12b";
		constexpr const char* folder3    = "\ued41";
		constexpr const char* folder4    = "\ued42";
		constexpr const char* folderopen = "\ued44";
		constexpr const char* folderopen2 = "\ued25";
		constexpr const char* image      = "\ue9d9";
		constexpr const char* message2   = "\uec42";
	}
}


