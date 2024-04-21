
#define STB_IMAGE_IMPLEMENTATION
#include <stb/stb_image.h>

#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include <stb/stb_image_resize.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb/stb_image_write.h>

#define STB_DXT_IMPLEMENTATION
#include <stb/stb_dxt.h>

#include <utils/image.h>


/*
	STBI__CASE(1,2) { dest[0]=src[0]; dest[1]=255;                                     } break;
	STBI__CASE(1,3) { dest[0]=dest[1]=dest[2]=src[0];                                  } break;
	STBI__CASE(1,4) { dest[0]=dest[1]=dest[2]=src[0]; dest[3]=255;                     } break;
	STBI__CASE(2,1) { dest[0]=src[0];                                                  } break;
	STBI__CASE(2,3) { dest[0]=dest[1]=dest[2]=src[0];                                  } break;
	STBI__CASE(2,4) { dest[0]=dest[1]=dest[2]=src[0]; dest[3]=src[1];                  } break;
	STBI__CASE(3,4) { dest[0]=src[0];dest[1]=src[1];dest[2]=src[2];dest[3]=255;        } break;
	STBI__CASE(3,1) { dest[0]=stbi__compute_y(src[0],src[1],src[2]);                   } break;
	STBI__CASE(3,2) { dest[0]=stbi__compute_y(src[0],src[1],src[2]); dest[1] = 255;    } break;
	STBI__CASE(4,1) { dest[0]=stbi__compute_y(src[0],src[1],src[2]);                   } break;
	STBI__CASE(4,2) { dest[0]=stbi__compute_y(src[0],src[1],src[2]); dest[1] = src[3]; } break;
	STBI__CASE(4,3) { dest[0]=src[0];dest[1]=src[1];dest[2]=src[2];                    } break;


	STBI__CASE(1,4) { dest[0]=dest[1]=dest[2]=src[0]; dest[3]=255;                     } break; // PerPixel offset 0, channel 1
	STBI__CASE(2,4) { dest[0]=dest[1]=dest[2]=src[0]; dest[3]=src[1];                  } break; // PerPixel offset 2, channel 
	STBI__CASE(3,4) { dest[0]=src[0];dest[1]=src[1];dest[2]=src[2];dest[3]=255;        } break; // PerPixel offset 0
*/

namespace chord
{
	bool ImageLdr2D::fillFromFile(const std::string& path, int32 requiredComponent)
	{
		clear();

		m_requiredComponent = requiredComponent;
		m_dimension.z = 1;

		auto* pixels = stbi_load(path.c_str(), &m_dimension.x, &m_dimension.y, &m_component, m_requiredComponent);
		{
			if (pixels == nullptr)
			{
				clear();
				return false;
			}

			m_pixels.resize(m_dimension.x * m_dimension.y * m_requiredComponent);
			::memcpy(m_pixels.data(), pixels, m_pixels.size());
		}
		stbi_image_free(pixels);

		return true;
	}

	void ImageLdr2D::fillColor(RGBA c, int32 width, int32 height)
	{
		m_dimension = { width, height, 1};
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

		m_dimension = { width, height, 1 };
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

	bool ImageHalf2D::fillFromFile(const std::string& path, int32 requiredComponent)
	{
		clear();

		m_requiredComponent = requiredComponent;
		m_dimension.z = 1;

		auto* pixels = stbi_load_16(path.c_str(), &m_dimension.x, &m_dimension.y, &m_component, m_requiredComponent);
		{
			if (pixels == nullptr)
			{
				clear();
				return false;
			}

			m_pixels.resize(m_dimension.x * m_dimension.y * m_requiredComponent);
			::memcpy(m_pixels.data(), pixels, m_pixels.size() * sizeof(m_pixels[0]));
		}
		stbi_image_free(pixels);

		return true;
	}
}