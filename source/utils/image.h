#pragma once

#include <utils/noncopyable.h>
#include <utils/utils.h>
#include <utils/rgb.h>

namespace chord
{
	// Only suport these case.
	enum class EImageChannelRemapType
	{
		eR    = 1,
		eRG   = 2,
		eRGB  = 3,
		eRGBA = 4,
	};

	template<class DataType>
	class ImageGeneric
	{
	public:
		explicit ImageGeneric() = default;
		virtual ~ImageGeneric()
		{
			clear();
		}

		auto getWidth() const { return m_dimension.x; }
		auto getHeight() const { return m_dimension.y; }
		auto getDepth() const { return m_dimension.z; }
		auto getComponent() const { return m_component; }

		uint32 getSize() const 
		{ 
			return m_dimension.x * m_dimension.y * m_dimension.z * m_component * sizeof(DataType);
		}

		const auto* getPixels() const 
		{
			return m_pixels.data(); 
		}

		bool isEmpty() const 
		{ 
			return m_pixels.empty(); 
		}

		SizedBuffer getSizeBuffer() const
		{ 
			return SizedBuffer(getSize(), (void*)getPixels()); 
		}

	protected:
		void clear()
		{
			m_dimension = { };
			m_component = 0;
			m_pixels    = { };
		}

	protected:
		//
		int32 m_component = 0;
		math::ivec3 m_dimension;
		std::vector<DataType> m_pixels = { };

	public:
		template<class Ar> void serialize(Ar& ar, uint32 ver)
		{
			ar(m_pixels, m_dimension, m_component);
		}
	};

	class ImageLdr3D : public ImageGeneric<uint8>
	{
	public:
		bool fillFromFile(int32 depth, EImageChannelRemapType channelRemapType, std::function<std::string(uint32)>&& pathLambda);

		virtual ~ImageLdr3D() { }
	public:
		template<class Ar> void serialize(Ar& ar, uint32 ver)
		{
			ar(cereal::base_class<ImageGeneric<uint8>>(this));
		}
	};

	class ImageLdr2D : public ImageGeneric<uint8>
	{
	public:
		void fillColor(RGBA c, int32 width = 1, int32 height = 1);
		void fillChessboard(RGBA c0, RGBA c1, int32 width, int32 height, int32 blockDim);
		bool fillFromFile(const std::string& path, EImageChannelRemapType remapType = EImageChannelRemapType::eRGBA);

		virtual ~ImageLdr2D(){ }
	public:
		template<class Ar> void serialize(Ar& ar, uint32 ver)
		{
			ar(cereal::base_class<ImageGeneric<uint8>>(this));
		}
	};

	class ImageHalf2D : public ImageGeneric<uint16>
	{
	public:
		bool fillFromFile(const std::string& path, EImageChannelRemapType remapType = EImageChannelRemapType::eRGBA);
		virtual ~ImageHalf2D() { }
	public:
		template<class Ar> void serialize(Ar& ar, uint32 ver)
		{
			ar(cereal::base_class<ImageGeneric<uint16>>(this));
		}
	};
}