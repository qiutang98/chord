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
		auto* pixels = stbi_load(path.c_str(), &m_width, &m_height, &m_component, m_requiredComponent);
		{
			if (pixels == nullptr)
			{
				clear();
				return false;
			}

			m_pixels.resize(m_width * m_height * m_requiredComponent);
			::memcpy(m_pixels.data(), pixels, m_pixels.size());
		}
		stbi_image_free(pixels);

		return true;
	}

	void ImageLdr2D::fillColor(RGBA c, int32 width, int32 height)
	{
		m_width  = width;
		m_height = height;
		m_component = m_requiredComponent = 4;

		const auto count = width * height * m_requiredComponent;
		m_pixels.resize(count);

		for (int32 i = 0; i < count; i += m_requiredComponent)
		{
			::memcpy(&m_pixels[i], c.getData(), m_requiredComponent);
		}
	}

	void ImageLdr2D::fillChessboard(RGBA c0, RGBA c1, int32 width, int32 height, int32 blockDim)
	{
		clear();

		m_width = width;
		m_height = height;
		m_component = m_requiredComponent = 4;

		const auto count = width * height * m_requiredComponent;
		m_pixels.resize(count);

		for (int32 i = 0; i < count; i += m_requiredComponent)
		{
			// Pixel pos in 1D.
			int32 pixelIndex = i / m_requiredComponent;

			int32 x = ((pixelIndex % width) % (blockDim * 2)) / blockDim;
			int32 y = ((pixelIndex / width) % (blockDim * 2)) / blockDim;

			const bool bColor1 = (x + y == 1);
			::memcpy(&m_pixels[i], bColor1 ? c1.getData() : c0.getData(), m_requiredComponent);
		}
	}

	void ImageLdr2D::clear()
	{
		m_pixels = { };
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