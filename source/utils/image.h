#pragma once

#include <utils/noncopyable.h>
#include <utils/utils.h>

namespace chord
{


	class ImageLdr2D : NonCopyable
	{
	private:
		uint8* m_pixels = nullptr;

		int32  m_width = 0;
		int32  m_height = 0;
		int32  m_component = 0;
		int32  m_requiredComponent = 0;

	protected:
		void clear();

	public:
		virtual ~ImageLdr2D();

		explicit ImageLdr2D() = default;

		explicit ImageLdr2D(uint8 R, uint8 G, uint8 B, uint8 A, int32 width = 1, int32 height = 1);


		void fillChessboard(
			uint8 R0, uint8 G0, uint8 B0, uint8 A0,
			uint8 R1, uint8 G1, uint8 B1, uint8 A1,
			int32 width, int32 height, int32 blockDim);

		bool fillFromFile(const std::string& path, int32 requiredComponent = 4);

		auto getWidth() const { return m_width; }
		auto getHeight() const { return m_height; }
		auto getComponent() const { return m_component; }
		auto getRequiredComponent() const { return m_requiredComponent; }

		const auto* getPixels() const { return m_pixels; }

		bool isEmpty() const { return m_pixels == nullptr; }
	};
}