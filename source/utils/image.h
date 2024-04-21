#pragma once

#include <utils/noncopyable.h>
#include <utils/utils.h>
#include <utils/rgb.h>

namespace chord
{
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
		auto getRequiredComponent() const { return m_requiredComponent; }

		uint32 getSize() const 
		{ 
			return m_dimension.x * m_dimension.y * m_dimension.z * m_requiredComponent * sizeof(DataType);
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
			m_requiredComponent = 0;
		}

	protected:
		math::ivec3 m_dimension;

		//
		int32 m_component = 0;
		int32 m_requiredComponent = 0;

		// 
		std::vector<DataType> m_pixels = { };

	public:
		template<class Ar> void serialize(Ar& ar, uint32 ver)
		{
			ar(m_pixels, m_dimension, m_component, m_requiredComponent);
		}
	};

	class ImageLdr2D : public ImageGeneric<uint8>
	{
	public:
		void fillColor(RGBA c, int32 width = 1, int32 height = 1);
		void fillChessboard(RGBA c0, RGBA c1, int32 width, int32 height, int32 blockDim);
		bool fillFromFile(const std::string& path, int32 requiredComponent = 4);

	public:
		template<class Ar> void serialize(Ar& ar, uint32 ver)
		{
			ar(cereal::base_class<ImageGeneric<uint8>>(this));
		}
	};

	class ImageHalf2D : public ImageGeneric<uint16>
	{
	public:
		bool fillFromFile(const std::string& path, int32 requiredComponent = 4);

	public:
		template<class Ar> void serialize(Ar& ar, uint32 ver)
		{
			ar(cereal::base_class<ImageGeneric<uint16>>(this));
		}
	};
}