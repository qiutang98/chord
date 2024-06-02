#pragma once
#include <ui/ui.h>
#include <fontawsome/IconsFontAwesome6.h>
#include <fontawsome/IconsFontAwesome6Brands.h>
#include <graphics/graphics.h>

namespace chord
{
	class AssetSaveInfo;
}

namespace chord::ui
{
	extern void disableLambda(std::function<void()>&& lambda, bool bDisable);
	extern void hoverTip(const char* desc);
	extern bool treeNodeEx(const char* idLabel, const char* showlabel, ImGuiTreeNodeFlags flags);
	extern bool drawVector3(const std::string& label, math::vec3& values, const math::vec3& resetValue, float labelWidth);
	extern void helpMarker(const char* desc);
	extern bool buttonCenteredOnLine(const char* label, math::vec2 size, float alignment = 0.5f);

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

	
	extern void inspectAssetSaveInfo(const AssetSaveInfo& info);

	extern void drawImage(
		graphics::GPUTextureAssetRef image, 
		const VkImageSubresourceRange& subRange,
		const ImVec2& size, const ImVec2& uv0 = ImVec2(0, 0), const ImVec2& uv1 = ImVec2(1, 1), const ImVec4& tint_col = ImVec4(1, 1, 1, 1), const ImVec4& border_col = ImVec4(0, 0, 0, 0));

	extern void drawImage(
		graphics::GPUTextureRef  image,
		const VkImageSubresourceRange& subRange,
		const ImVec2& size, const ImVec2& uv0 = ImVec2(0, 0), const ImVec2& uv1 = ImVec2(1, 1), const ImVec4& tint_col = ImVec4(1, 1, 1, 1), const ImVec4& border_col = ImVec4(0, 0, 0, 0));
}


