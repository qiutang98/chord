#pragma once

#include <utils/utils.h>
#include <asset/asset_common.h>
#include <asset/asset.h>
#include <asset/texture/helper.h>

namespace chord
{
	// Asset texture binary.
	struct TextureAssetBin
	{
		std::vector<std::vector<uint8>> mipmapDatas;
		template<class Ar> void serialize(Ar& ar, uint32 const version)
		{
			ar(mipmapDatas);
		}
	};

	class TextureAsset : public IAsset
	{
		REGISTER_BODY_DECLARE(IAsset);

		friend class AssetManager;
		friend bool importFromConfig(TextureAssetImportConfigRef);
	public:
		// Color gamma space.
		enum class EColorSpace
		{
			Linear,
			Rec709, // Or sRGB
			MAX,
		};

		static const AssetTypeMeta kAssetTypeMeta;

	public:
		TextureAsset() = default;
		virtual ~TextureAsset() = default;

		// 
		explicit TextureAsset(const AssetSaveInfo& saveInfo);

	protected:
		// ~IAsset virtual function.
		// Call back when call AssetManager::createAsset
		virtual void onPostConstruct() override;

		virtual bool onSave() override;
		virtual void onUnload() override;
		// ~IAsset virtual function.

	public:
		constexpr bool isSRGB() const
		{
			return m_bSRGB;
		}

		constexpr uint32 getMipmapCount() const
		{
			return m_mipmapCount;
		}

		constexpr VkFormat getFormat() const
		{
			return m_format;
		}

		const auto& getDimension() const 
		{ 
			return m_dimension; 
		}

		float getAlphaMipmapCutOff() const 
		{ 
			return m_mipmapAlphaCutoff;
		}

	private:
		static AssetTypeMeta createTextureTypeMeta();

		void initBasicInfo(
			bool bSrgb,
			uint32_t mipmapCount,
			VkFormat format,
			const math::uvec3& dimension,
			float alphaCutoff)
		{
			m_bSRGB = bSrgb;
			m_mipmapCount = mipmapCount;
			m_format = format;
			m_dimension = dimension;
			m_mipmapAlphaCutoff = alphaCutoff;
		}

	private:
		// Current texture asset under which color space.
		EColorSpace m_colorspace;

		// Current texture is under sRGB gamma encode.
		bool m_bSRGB;

		// Texture dimension.
		math::uvec3 m_dimension;

		// Texture mipmap alpha coverage cutoff.
		float m_mipmapAlphaCutoff;

		// Texture format.
		VkFormat m_format;

		// Mipmap infos.
		uint32 m_mipmapCount;
	};
	using TextureAssetRef = std::shared_ptr<TextureAsset>;
}
