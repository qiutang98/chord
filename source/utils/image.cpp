
#include <utils/image.h>
#include <utils/log.h>

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
	static inline int getComponentFromChannelRemapType(EImageChannelRemapType remapType)
	{
		switch (remapType)
		{
		case chord::EImageChannelRemapType::eR:
			return 1;
		case chord::EImageChannelRemapType::eRG:
			return 2;
		case chord::EImageChannelRemapType::eRGB:
			return 3;
		case chord::EImageChannelRemapType::eRGBA:
			return 4;
		}

		checkEntry();
		return 0;
	}

	bool ImageLdr2D::fillFromFile(const std::string& path, EImageChannelRemapType remapType)
	{
		clear();

		// Remodify component.
		m_component = getComponentFromChannelRemapType(remapType);
		m_dimension.z = 1;

		int component;
		auto* pixels = stbi_load(path.c_str(), &m_dimension.x, &m_dimension.y, &component, 4);
		if (pixels == nullptr)
		{
			clear();
			return false;
		}

		//
		m_pixels.resize(m_dimension.x * m_dimension.y * m_component);

		//
		check( remapType == EImageChannelRemapType::eR
			|| remapType == EImageChannelRemapType::eRG
			|| remapType == EImageChannelRemapType::eRGB
			|| remapType == EImageChannelRemapType::eRGBA);

		if (remapType == EImageChannelRemapType::eRGBA)
		{
			::memcpy(m_pixels.data(), pixels, m_pixels.size());
		}
		else
		{
			for (int i = 0; i < m_dimension.x * m_dimension.y; i++)
			{
				for (int j = 0; j < m_component; j++)
				{
					m_pixels[i * m_component + j] = pixels[i * 4 + j];
				}
			}
		}

		stbi_image_free(pixels);

		return true;
	}

	bool ImageLdr3D::fillFromFile(int32 depth, EImageChannelRemapType remapType, std::function<std::string(uint32)>&& pathLambda)
	{
		clear();

		// 
		m_dimension.z = depth;
		m_component = getComponentFromChannelRemapType(remapType);

		math::ivec2 cacheDim = { -1, -1 };
		int32 offset = 0;
		for (int32 i = 0; i < depth; i++)
		{
			std::string path = pathLambda(i);

			int component;
			auto* pixels = stbi_load(path.c_str(), &m_dimension.x, &m_dimension.y, &component, 4);

			if (pixels == nullptr)
			{
				clear();
				return false;
			}

			int32 perPlaneSize = m_dimension.x * m_dimension.y * m_component;

			if (cacheDim.x < 0)
			{
				cacheDim.x = m_dimension.x;
				cacheDim.y = m_dimension.y;

				// Memory allocate. 
				m_pixels.resize(perPlaneSize * m_dimension.z);
			}
			else
			{
				check(cacheDim.x == m_dimension.x);
				check(cacheDim.y == m_dimension.y);
			}

			check( remapType == EImageChannelRemapType::eR
				|| remapType == EImageChannelRemapType::eRG
				|| remapType == EImageChannelRemapType::eRGB
				|| remapType == EImageChannelRemapType::eRGBA);

			if (remapType == EImageChannelRemapType::eRGBA)
			{
				::memcpy(m_pixels.data() + offset, pixels, perPlaneSize);
			}
			else
			{
				for (int i = 0; i < m_dimension.x * m_dimension.y; i++)
				{
					for (int j = 0; j < m_component; j++)
					{
						m_pixels[offset + i * m_component + j] = pixels[i * 4 + j];
					}
				}
			}

			stbi_image_free(pixels);

			// 
			offset += perPlaneSize;
		}
		check(offset == m_dimension.x * m_dimension.y * m_dimension.z * m_component);

		// 
		return true;
	}

	void ImageLdr2D::fillColor(RGBA c, int32 width, int32 height)
	{
		m_dimension = { width, height, 1};
		m_component = 4;

		const auto count = width * height * m_component;
		m_pixels.resize(count);

		for (int32 i = 0; i < count; i += m_component)
		{
			::memcpy(&m_pixels[i], c.getData(), m_component);
		}
	}

	void ImageLdr2D::fillChessboard(RGBA c0, RGBA c1, int32 width, int32 height, int32 blockDim)
	{
		clear();

		m_dimension = { width, height, 1 };
		m_component = 4;

		const auto count = width * height * m_component;
		m_pixels.resize(count);

		for (int32 i = 0; i < count; i += m_component)
		{
			// Pixel pos in 1D.
			int32 pixelIndex = i / m_component;

			int32 x = ((pixelIndex % width) % (blockDim * 2)) / blockDim;
			int32 y = ((pixelIndex / width) % (blockDim * 2)) / blockDim;

			const bool bColor1 = (x + y == 1);
			::memcpy(&m_pixels[i], bColor1 ? c1.getData() : c0.getData(), m_component);
		}
	}

	bool ImageHalf2D::fillFromFile(const std::string& path, EImageChannelRemapType remapType)
	{
		clear();

		m_component = getComponentFromChannelRemapType(remapType);
		m_dimension.z = 1;

		int component;
		auto* pixels = stbi_load_16(path.c_str(), &m_dimension.x, &m_dimension.y, &component, 4);

		if (pixels == nullptr)
		{
			clear();
			return false;
		}

		m_pixels.resize(m_dimension.x * m_dimension.y * m_component);

		check( remapType == EImageChannelRemapType::eR
			|| remapType == EImageChannelRemapType::eRG
			|| remapType == EImageChannelRemapType::eRGB
			|| remapType == EImageChannelRemapType::eRGBA);

		if (remapType == EImageChannelRemapType::eRGBA)
		{
			::memcpy(m_pixels.data(), pixels, m_pixels.size() * sizeof(m_pixels[0]));
		}
		else
		{
			for (int i = 0; i < m_dimension.x * m_dimension.y; i++)
			{
				for (int j = 0; j < m_component; j++)
				{
					m_pixels[i * m_component + j] = pixels[i * 4 + j];
				}
			}
		}

		stbi_image_free(pixels);

		return true;
	}
}