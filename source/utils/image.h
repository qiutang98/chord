#pragma once

#include <utils/noncopyable.h>
#include <utils/utils.h>
#include <utils/rgb.h>

namespace chord
{
	class ImageLdr2D : NonCopyable
	{
	private:
		std::vector<uint8> m_pixels = { };

		int32  m_width = 0;
		int32  m_height = 0;
		int32  m_component = 0;
		int32  m_requiredComponent = 0;

	protected:
		void clear();

	public:
		virtual ~ImageLdr2D();
		explicit ImageLdr2D() = default;

		void fillColor(RGBA c, int32 width = 1, int32 height = 1);
		void fillChessboard(RGBA c0, RGBA c1, int32 width, int32 height, int32 blockDim);
		bool fillFromFile(const std::string& path, int32 requiredComponent = 4);

		auto getWidth() const { return m_width; }
		auto getHeight() const { return m_height; }
		auto getComponent() const { return m_component; }
		auto getRequiredComponent() const { return m_requiredComponent; }

		const auto* getPixels() const { return m_pixels.data(); }

		bool isEmpty() const { return m_pixels.empty(); }
	};
}