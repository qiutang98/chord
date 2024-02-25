#include <utils/image.h>

#define STB_IMAGE_IMPLEMENTATION
#include <stb/stb_image.h>

#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include <stb/stb_image_resize.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb/stb_image_write.h>

#define STB_DXT_IMPLEMENTATION
#include <stb/stb_dxt.h>

namespace chord
{
	bool ImageLdr2D::fillFromFile(const std::string& path, int32 requiredComponent)
	{
		clear();

		m_requiredComponent = requiredComponent;
		m_pixels = stbi_load(path.c_str(), &m_width, &m_height, &m_component, m_requiredComponent);

		if (m_pixels == nullptr)
		{
			clear();
			return false;
		}

		return true;
	}

	ImageLdr2D::ImageLdr2D(
		uint8 R, uint8 G, uint8 B, uint8 A, 
		int32 width, int32 height)
	{
		m_width  = width;
		m_height = height;
		m_component = m_requiredComponent = 4;

		const auto count = width * height * m_requiredComponent;
		m_pixels = (uint8*)(::malloc(count));

		uint8 colors[] = { R, G, B, A };
		for (int32 i = 0; i < count; i += m_requiredComponent)
		{
			::memcpy(&m_pixels[i], colors, m_requiredComponent);
		}
	}

	void ImageLdr2D::fillChessboard(
		uint8 R0, uint8 G0, uint8 B0, uint8 A0, 
		uint8 R1, uint8 G1, uint8 B1, uint8 A1, 
		int32 width, int32 height, int32 blockDim)
	{
		clear();

		m_width = width;
		m_height = height;
		m_component = m_requiredComponent = 4;

		const auto count = width * height * m_requiredComponent;
		m_pixels = (uint8*)(::malloc(count));

		uint8 colors0[] = { R0, G0, B0, A0 };
		uint8 colors1[] = { R1, G1, B1, A1 };


		for (int32 i = 0; i < count; i += m_requiredComponent)
		{
			// Pixel pos in 1D.
			int32 pixelIndex = i / m_requiredComponent;

			int32 x = ((pixelIndex % width) % (blockDim * 2)) / blockDim;
			int32 y = ((pixelIndex / width) % (blockDim * 2)) / blockDim;


			const bool bColor1 = (x + y == 1);
			::memcpy(&m_pixels[i], bColor1 ? colors1 : colors0, m_requiredComponent);
		}
	}

	void ImageLdr2D::clear()
	{
		if (m_pixels != nullptr)
		{
			::free(m_pixels);
		}

		m_pixels = nullptr;
		m_width = 0;
		m_height = 0;
		m_component = 0;
		m_requiredComponent = 0;
	}

	ImageLdr2D::~ImageLdr2D()
	{
		clear();
	}
}