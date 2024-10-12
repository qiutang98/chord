#include <asset/serialize.h>
#include <asset/texture/helper.h>
#include <ui/ui_helper.h>
#include <asset/texture/texture.h>
#include <asset/asset.h>
#include <application/application.h>
#include <project.h>

#include <stb/stb_dxt.h>
#include <stb/stb_image_resize.h>

#include <shader/colorspace.h>
#include <graphics/resource.h>
#include <asset/asset_common.h>
#include <graphics/uploader.h>
#include <graphics/helper.h>


namespace chord
{
	using namespace graphics;

	// block compression functions.


	namespace dxt
	{
		constexpr uint32 kBCBlockDim = 4U;
		constexpr uint32 kBCBlockSize = kBCBlockDim * kBCBlockDim;

		uint16_t uint8PackTo565(uint8_t* c)
		{
			uint16_t result = 0;
			result |= ((uint16_t)math::floor(31.0f * c[2] / 255.0f) << 0);
			result |= ((uint16_t)math::floor(63.0f * c[1] / 255.0f) << 5);
			result |= ((uint16_t)math::floor(31.0f * c[0] / 255.0f) << 11);
			return result;
		}

		// My simple implement.
		static void compressBC3(uint8_t* dest, const uint8_t* src)
		{
			// Alpha pack.
			{
				uint8_t minAlpha = 255;
				uint8_t maxAlpha = 0;

				for (int i = 0; i < 16; i++)
				{
					uint8_t alpha = *(src + i * 4 + 3);
					minAlpha = math::min(minAlpha, alpha);
					maxAlpha = math::max(maxAlpha, alpha);
				}

				// Use six lerp point.
				dest[0] = maxAlpha; // alpha 0
				dest[1] = minAlpha; // alpha 1

				if (maxAlpha > minAlpha)
				{
					uint64_t packAlphaBit = 0;

					const float minAlphaFloat = float(minAlpha);
					const float maxAlphaFloat = float(maxAlpha);

					const float alphaRange = maxAlphaFloat - minAlphaFloat;
					const float alphaMult = 7.0f / alphaRange;

					for (int i = 0; i < 16; i++)
					{
						const uint8_t alpha = *(src + i * 4 + 3);
						uint64_t index = uint64_t(math::round(float(alpha) - minAlphaFloat) * alphaMult);

						if (index == 7) { index = 0; }
						else { index++; }

						packAlphaBit |= (index << (i * 3));
					}

					for (int i = 2; i < 8; i++)
					{
						dest[i] = (packAlphaBit >> ((i - 2) * 8)) & 0xff;
					}
				}
				else
				{
					for (int i = 2; i < 8; i++)
					{
						dest[i] = 0; // All alpha value same.
					}
				}
			}

			// Color pack.
			{
				uint8_t minColor[3] = { 255, 255, 255 };
				uint8_t maxColor[3] = { 0,   0,   0 };

				for (int i = 0; i < 16; i++)
				{
					for (int j = 0; j < 3; j++)
					{
						uint8_t c = *(src + i * 4 + j);
						minColor[j] = math::min(minColor[j], c);
						maxColor[j] = math::max(maxColor[j], c);
					}
				}

				uint16_t minColor565 = uint8PackTo565(minColor);
				uint16_t maxColor565 = uint8PackTo565(maxColor);

				// Fill max color 565 as color 0.
				dest[8] = uint8_t((maxColor565 >> 0) & 0xff);
				dest[9] = uint8_t((maxColor565 >> 8) & 0xff);

				// Fill min color 565 as color 1.
				dest[10] = uint8_t((minColor565 >> 0) & 0xff);
				dest[11] = uint8_t((minColor565 >> 8) & 0xff);

				if (maxColor565 > minColor565)
				{
					uint32_t packColorIndex = 0;

					// TODO: Color error diffusion avoid too much block artifact.
					math::vec3 minColorVec = math::vec3(minColor[0], minColor[1], minColor[2]);
					math::vec3 maxColorVec = math::vec3(maxColor[0], maxColor[1], maxColor[2]);

					// Color vector max -> min.
					math::vec3 maxMinVec = maxColorVec - minColorVec;

					// Color vector direction and length.
					float lenInvert = 1.0f / math::length(maxMinVec);
					math::vec3 colorDirVec = maxMinVec * lenInvert;

					for (int i = 0; i < 16; i++)
					{
						math::vec3 computeVec =
							math::vec3(*(src + i * 4 + 0), *(src + i * 4 + 1), *(src + i * 4 + 2)) - minColorVec;

						// Project current color into color direction vector and scale to [0, 3]
						uint32_t index = uint32_t(math::round(dot(computeVec, colorDirVec) * 3.0f * lenInvert));

						if (index == 3) { index = 0; }
						else { index++; }

						packColorIndex |= (index << (i * 2));
					}

					for (int i = 12; i < 16; i++)
					{
						dest[i] = (packColorIndex >> ((i - 12) * 8)) & 0xff;
					}
				}
				else
				{
					for (int i = 12; i < 16; i++)
					{
						dest[i] = 0; // All color value same.
					}
				}
			}
		}

		template<size_t kComponentCount, size_t kCompressionRatio, size_t kPerBlockCompressedSize>
		static void executeTaskForBC(
			uint32 mipWidth,
			uint32 mipHeight,
			std::vector<uint8>& compressMipData,
			const std::vector<uint8>& srcMipData,
			std::function<void(unsigned char* dest, const unsigned char* src)>&& functor)
		{
			compressMipData.resize(math::max(kPerBlockCompressedSize, srcMipData.size() / kCompressionRatio));

			const uint mipBlockWidth = math::max(1u, mipWidth / kBCBlockDim);
			const uint mipBlockHeight = math::max(1u, mipHeight / kBCBlockDim);

			const auto buildBC = [&](const size_t loopStart, const size_t loopEnd)
			{

				std::array<uint8, kBCBlockSize* kComponentCount> block{ };
				for (size_t taskIndex = loopStart; taskIndex < loopEnd; ++taskIndex)
				{
					const uint32 pixelPosX = (taskIndex % mipBlockWidth) * kBCBlockDim;
					const uint32 pixelPosY = (taskIndex / mipBlockWidth) * kBCBlockDim;

					const uint32 bufferOffset = taskIndex * kPerBlockCompressedSize;
					uint32 blockLocation = 0;

					// Fill block for bc.
					for (uint32 j = 0; j < kBCBlockDim; j++)
					{
						for (uint32 i = 0; i < kBCBlockDim; i++)
						{
							uint32 dimX = pixelPosX + i;
							uint32 dimY = pixelPosY + j;

							// BC compression require 4x4 block.
							// It evaluate a lookup curve to fit the pixel value.
							// So if no enough pixel to fill the block, just keep repeat one, auto fit by hardware decode.
							if (dimX >= mipWidth)  { dimX %= mipWidth;  }
							if (dimY >= mipHeight) { dimY %= mipHeight; }

							const uint32 pixelLocation = (dimX + dimY * mipWidth) * kComponentCount;
							check(pixelLocation < srcMipData.size());
							{
								const uint8* src = srcMipData.data() + pixelLocation;
								uint8* dest = block.data() + blockLocation;

								memcpy(dest, src, kComponentCount);
								blockLocation += kComponentCount;
							}
						}
					}
					functor(&compressMipData[bufferOffset], block.data());
				}
			};
			Application::get().getThreadPool().parallelizeLoop(0, mipBlockWidth * mipBlockHeight, buildBC).wait();
		}

		void mipmapCompressBC3(
			std::vector<uint8>& compressMipData,
			const std::vector<uint8>& srcMipData,
			uint32 mipWidth,
			uint32 mipHeight)
		{
			// src  = (4 * 4) * 4 = 64 byte.
			// dest = 16 byte.
			constexpr size_t kCompressionRatio = 4;
			constexpr size_t kPerBlockCompressedSize = 16;
			constexpr size_t kComponentCount = 4;

			executeTaskForBC<kComponentCount, kCompressionRatio, kPerBlockCompressedSize>(
				mipWidth, mipHeight, compressMipData, srcMipData, [](unsigned char* dest, const unsigned char* src)
				{
				#if 1
					stb_compress_dxt_block(dest, src, 1, STB_DXT_HIGHQUAL);
				#else
					compressBC3(dest, src);
				#endif
				});
		}

		void mipmapCompressBC4(
			std::vector<uint8>& compressMipData,
			const std::vector<uint8>& srcMipData,
			const TextureAsset& meta,
			uint32 mipWidth,
			uint32 mipHeight)
		{
			// src  = (4 * 4) * 1 = 16 byte.
			// dest = 8 byte.
			constexpr size_t kCompressionRatio = 2;
			constexpr size_t kPerBlockCompressedSize = 8;
			constexpr size_t kComponentCount = 1;

			executeTaskForBC<kComponentCount, kCompressionRatio, kPerBlockCompressedSize>(
				mipWidth, mipHeight, compressMipData, srcMipData, [](unsigned char* dest, const unsigned char* src)
				{
					stb_compress_bc4_block(dest, src);
				});
		}

		void mipmapCompressBC1(
			std::vector<uint8>& compressMipData,
			const std::vector<uint8>& srcMipData,
			const TextureAsset& meta,
			uint32 mipWidth,
			uint32 mipHeight)
		{
			// src  = (4 * 4) * 4 = 64 byte.
			// dest = 8 byte.
			constexpr size_t kCompressionRatio = 8;
			constexpr size_t kPerBlockCompressedSize = 8;
			constexpr size_t kComponentCount = 4;

			executeTaskForBC<kComponentCount, kCompressionRatio, kPerBlockCompressedSize>(
				mipWidth, mipHeight, compressMipData, srcMipData, [](unsigned char* dest, const unsigned char* src)
				{
					stb_compress_dxt_block(dest, src, 0, STB_DXT_HIGHQUAL);
				});
		}

		void mipmapCompressBC5(
			std::vector<uint8>& compressMipData,
			const std::vector<uint8>& srcMipData,
			const TextureAsset& meta,
			uint32 mipWidth,
			uint32 mipHeight)
		{
			// src  = (4 * 4) * 2 = 32 byte.
			// dest = 16 byte.
			constexpr size_t kCompressionRatio = 2;
			constexpr size_t kPerBlockCompressedSize = 16;
			constexpr size_t kComponentCount = 2;

			executeTaskForBC<kComponentCount, kCompressionRatio, kPerBlockCompressedSize>(
				mipWidth, mipHeight, compressMipData, srcMipData, [](unsigned char* dest, const unsigned char* src)
				{
					stb_compress_bc5_block(dest, src);
				});
		}

		static void mipmapCompressBC(TextureAssetBin& inOutBin, const TextureAsset& meta)
		{
			std::vector<std::vector<uint8>> compressedMipdatas;
			compressedMipdatas.resize(inOutBin.mipmapDatas.size());

			for (size_t mipIndex = 0; mipIndex < compressedMipdatas.size(); mipIndex++)
			{
				auto& compressMipData = compressedMipdatas[mipIndex];
				const auto& srcMipData = inOutBin.mipmapDatas[mipIndex];

				uint32 mipWidth = math::max<uint32>(meta.getDimension().x >> mipIndex, 1);
				uint32 mipHeight = math::max<uint32>(meta.getDimension().y >> mipIndex, 1);

				if (meta.getFormat() == VK_FORMAT_BC3_SRGB_BLOCK || meta.getFormat() == VK_FORMAT_BC3_UNORM_BLOCK)
				{
					mipmapCompressBC3(compressMipData, srcMipData, mipWidth, mipHeight);
				}
				else if (meta.getFormat() == VK_FORMAT_BC5_UNORM_BLOCK)
				{
					mipmapCompressBC5(compressMipData, srcMipData, meta, mipWidth, mipHeight);
				}
				else if (meta.getFormat() == VK_FORMAT_BC1_RGB_UNORM_BLOCK || meta.getFormat() == VK_FORMAT_BC1_RGB_SRGB_BLOCK)
				{
					mipmapCompressBC1(compressMipData, srcMipData, meta, mipWidth, mipHeight);
				}
				else if (meta.getFormat() == VK_FORMAT_BC4_UNORM_BLOCK)
				{
					mipmapCompressBC4(compressMipData, srcMipData, meta, mipWidth, mipHeight);
				}
				else
				{
					LOG_FATAL("Format {} still no process, need developer fix.", std::string(nameof::nameof_enum(meta.getFormat())));
				}
			}

			inOutBin.mipmapDatas = std::move(compressedMipdatas);
		}
	}

	namespace mipmap
	{
		constexpr int32 kFindBestAlphaCount = 50;

		template<typename T> double getQuantifySize() { checkEntry(); return 0.0; }
		
		// When mipmap downsample average, we 
		template<> double getQuantifySize<uint8 >() { return double(1 <<  8) - 1.0; }
		template<> double getQuantifySize<uint16>() { return double(1 << 16) - 1.0; }
		template<> double getQuantifySize<float >() { return 1.0; }

		void checkMipmapParams(uint32 channelCount, uint32 pixelOffsetPerSample, float alphaMipmapCutoff)
		{
			check(channelCount == 1 || channelCount == 2 || channelCount == 4);
			check(channelCount + pixelOffsetPerSample <= 4);
			check(alphaMipmapCutoff >= 0.0f && alphaMipmapCutoff <= 1.0f);
		}

		template<typename T>
		static double getAlphaCoverage(const T* data, uint32 width, uint32 height, double scale, double cutoff)
		{
			const double kQuantitySize = getQuantifySize<T>();

			double value = 0.0;
			T* pImg = (T*)data;

			// Loop all texture to get coverage alpha data.
			for (uint32 y = 0; y < height; y++)
			{
				for (uint32 x = 0; x < width; x++)
				{
					T* pPixel = pImg;
					pImg += 4;

					double alpha = scale * double(pPixel[3]) / kQuantitySize;

					if (alpha > 1.0) { alpha = 1.0; }
					if (alpha < cutoff) { continue; }

					// Accumulate.
					value += alpha;
				}
			}

			// Normalize.
			return value / double(height * width);
		}

		// Apply scale in alpha channel.
		template<typename T>
		static void scaleAlpha(const T* data, uint32 width, uint32 height, double scale)
		{
			constexpr double kQuantitySize = getQuantifySize<T>();

			T* pImg = (T*)data;
			for (uint32 y = 0; y < height; y++)
			{
				for (uint32 x = 0; x < width; x++)
				{
					T* pPixel = pImg;
					pImg += 4;

					// Load and apply scale in alpha.
					double alpha = scale * double(pPixel[3]) / kQuantitySize;
					alpha = math::clamp(alpha, 0.0, 1.0);

					pPixel[3] = T(alpha * kQuantitySize);
				}
			}
		}

		template<typename T>
		void buildMipmapData(
			uint32 channelCount,
			uint32 pixelOffsetPerSample,
			const T* srcPixels,
			TextureAssetBin& outBinData,
			uint32 mipmapCount,
			uint32 inWidth,
			uint32 inHeight,
			float alphaMipmapCutoff,
			bool bSRGB)
		{
			if (bSRGB)
			{
				constexpr bool bUnit8 = std::is_same_v<T, uint8>;
				check(bUnit8);
			}

			checkMipmapParams(channelCount, pixelOffsetPerSample, alphaMipmapCutoff);
			const double kQuantitySize = getQuantifySize<T>();

			float alphaCoverage = 1.0f;
			const bool bKeepAlphaCoverage = (alphaMipmapCutoff < 1.0f) && (channelCount == 4);

			// Prepare mipmap datas.
			outBinData.mipmapDatas.resize(mipmapCount);

			// Get strip size.
			const auto kStripSize = sizeof(T) * channelCount;

			// Now build each mipmap.
			for (auto mip = 0; mip < outBinData.mipmapDatas.size(); mip++)
			{
				auto& destMipData = outBinData.mipmapDatas[mip];

				// Compute mipmap size.
				uint32 destWidth  = math::max<uint32>(inWidth  >> mip, 1);
				uint32 destHeight = math::max<uint32>(inHeight >> mip, 1);

				// Allocate memory.
				destMipData.resize(destWidth * destHeight * kStripSize);

				T* pDestData = (T*)destMipData.data();
				if (mip == 0)
				{
					if (channelCount == 4)
					{
						// Source data copy.
						memcpy((void*)pDestData, srcPixels, destMipData.size() * sizeof(destMipData[0]));
					}
					else
					{
						for (auto i = 0; i < destWidth * destHeight; i++)
						{
							for (auto j = 0; j < channelCount; j++)
							{
								pDestData[i * channelCount + j] = srcPixels[i * 4 + j + pixelOffsetPerSample];
							}
						}
					}

					if (bKeepAlphaCoverage)
					{
						alphaCoverage = getAlphaCoverage<T>((const T*)destMipData.data(), destWidth, destHeight, 1.0, alphaMipmapCutoff);
					}
				}
				else
				{
					const auto srcMip = mip - 1;
					const T* pSrcData = (const T*)outBinData.mipmapDatas[srcMip].data();

					for (auto y = 0; y < destHeight; y++)
					{
						for (auto x = 0; x < destWidth; x++)
						{
							// Get src data.
							uint32 srcWidth  = std::max<uint32>(inWidth  >> srcMip, 1);
							uint32 srcHeight = std::max<uint32>(inHeight >> srcMip, 1);

							// Clamp src data fetech edge.
							auto srcX0 = std::min<uint32>(uint32(x * 2 + 0), srcWidth  - 1);
							auto srcX1 = std::min<uint32>(uint32(x * 2 + 1), srcWidth  - 1);
							auto srcY0 = std::min<uint32>(uint32(y * 2 + 0), srcHeight - 1);
							auto srcY1 = std::min<uint32>(uint32(y * 2 + 1), srcHeight - 1);

							uint32 srcPixelStart[] =
							{
								(srcY0 * srcWidth + srcX0) * channelCount, // X0Y0
								(srcY0 * srcWidth + srcX1) * channelCount, // X1Y0
								(srcY1 * srcWidth + srcX0) * channelCount, // X0Y1
								(srcY1 * srcWidth + srcX1) * channelCount, // X1Y1
							};
							
							auto destPixelPosStart = (y * destWidth + x) * channelCount;

							for (auto channelId = 0; channelId < channelCount; channelId++)
							{
								double sumValue = 0.0;
								const bool bAlphaChannel = (channelId + pixelOffsetPerSample == 3);

								for (auto srcPixelId = 0; srcPixelId < 4; srcPixelId++)
								{
									T rawData = pSrcData[srcPixelStart[srcPixelId] + channelId];

									double normalizeValue = double(rawData) / kQuantitySize;
									if (!bAlphaChannel && bSRGB)
									{
										normalizeValue = double(rec709GammaDecode(float(normalizeValue)));
									}

									sumValue += normalizeValue;
								}
								sumValue *= 0.25;

								if (!bAlphaChannel && bSRGB)
								{
									sumValue = double(rec709GammaEncode(float(sumValue)));
								}
								pDestData[destPixelPosStart + channelId] = T(sumValue * kQuantitySize);
							}
						}
					}
				}
				check(destHeight * destWidth * kStripSize == destMipData.size());
			}
		}
	}
	
	VkFormat getFormatFromConfig(const TextureAssetImportConfig& config, bool bCanCompressed)
	{
		if (config.format == ETextureFormat::R16Unorm)
		{
			return VK_FORMAT_R16_UNORM;
		}

		if (config.format == ETextureFormat::RGBA16Unorm)
		{
			return VK_FORMAT_R16G16B16A16_UNORM;
		}

		if (config.format == ETextureFormat::R8G8B8A8)
		{
			return config.bSRGB ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
		}
		if (ETextureFormat::R8G8 == config.format) return VK_FORMAT_R8G8_UNORM;
		if (ETextureFormat::Greyscale == config.format ||
			ETextureFormat::R8 == config.format ||
			ETextureFormat::B8 == config.format ||
			ETextureFormat::G8 == config.format ||
			ETextureFormat::A8 == config.format) return VK_FORMAT_R8_UNORM;

		if (ETextureFormat::BC3 == config.format)
		{
			if (bCanCompressed)
			{
				return config.bSRGB ? VK_FORMAT_BC3_SRGB_BLOCK : VK_FORMAT_BC3_UNORM_BLOCK;
			}
			return config.bSRGB ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
		}

		if (ETextureFormat::BC1 == config.format)
		{
			if (bCanCompressed)
			{
				// Don't care BC1's alpha which quality poorly.
				return config.bSRGB ? VK_FORMAT_BC1_RGB_SRGB_BLOCK : VK_FORMAT_BC1_RGB_UNORM_BLOCK;
			}
			return config.bSRGB ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
		}

		if (ETextureFormat::BC4Greyscale == config.format ||
			ETextureFormat::BC4R8 == config.format ||
			ETextureFormat::BC4G8 == config.format ||
			ETextureFormat::BC4B8 == config.format ||
			ETextureFormat::BC4A8 == config.format)
		{
			if (bCanCompressed)
			{
				return VK_FORMAT_BC4_UNORM_BLOCK;
			}
			return VK_FORMAT_R8_UNORM;
		}

		if (ETextureFormat::BC5 == config.format)
		{
			if (bCanCompressed)
			{
				return VK_FORMAT_BC5_UNORM_BLOCK;
			}
			return VK_FORMAT_R8G8_UNORM;
		}

		checkEntry();
		return VK_FORMAT_UNDEFINED;
	}

	static inline void getChannelCountOffset(uint32& channelCount, uint32& pixelSampleOffset, ETextureFormat format)
	{
		switch (format)
		{
		case ETextureFormat::RGBA16Unorm:
		case ETextureFormat::R8G8B8A8:
		case ETextureFormat::BC3:
		case ETextureFormat::BC1:
		{
			channelCount = 4;
			pixelSampleOffset = 0;
			return;
		}
		case ETextureFormat::BC5:
		case ETextureFormat::R8G8:
		{
			channelCount = 2;
			pixelSampleOffset = 0;
			return;
		}
		case ETextureFormat::Greyscale:
		case ETextureFormat::BC4Greyscale:
		case ETextureFormat::R8:
		case ETextureFormat::BC4R8:
		case ETextureFormat::R16Unorm:
		{
			channelCount = 1;
			pixelSampleOffset = 0;
			return;
		}
		case ETextureFormat::G8:
		case ETextureFormat::BC4G8:
		{
			channelCount = 1;
			pixelSampleOffset = 1;
			return;
		}
		case ETextureFormat::B8:
		case ETextureFormat::BC4B8:
		{
			channelCount = 1;
			pixelSampleOffset = 2;
			return;
		}
		case ETextureFormat::A8:
		case ETextureFormat::BC4A8:
		{
			channelCount = 1;
			pixelSampleOffset = 3;
			return;
		}
		}

		checkEntry();
	}

	namespace snapshot
	{
		constexpr auto kMaxSnapshotDim = 128u;
		constexpr auto kPerSnapshotSize = kMaxSnapshotDim * kMaxSnapshotDim * 4 * 4;

		constexpr VkFormat kFormat = VK_FORMAT_BC3_SRGB_BLOCK;

		static void quantifyDim(uint32& widthSnapShot, uint32& heightSnapShot, uint32 texWidth, uint32 texHeight)
		{
			// For bc.
			texWidth  = math::max(texWidth,  4u);
			texHeight = math::max(texHeight, 4u);

			if (texWidth >= kMaxSnapshotDim || texHeight >= kMaxSnapshotDim)
			{
				if (texWidth > texHeight)
				{
					widthSnapShot = kMaxSnapshotDim;
					heightSnapShot = texHeight / (texWidth / kMaxSnapshotDim);
				}
				else if (texHeight > texWidth)
				{
					heightSnapShot = kMaxSnapshotDim;
					widthSnapShot = texWidth / (texHeight / kMaxSnapshotDim);
				}
				else
				{
					widthSnapShot = kMaxSnapshotDim;
					heightSnapShot = kMaxSnapshotDim;
				}
			}
			else
			{
				heightSnapShot = texHeight;
				widthSnapShot = texWidth;
			}
		}

		void build8bit(const TextureAsset& meta, std::vector<uint8>& outData, const unsigned char* pixels, int numChannel, math::uvec2& dim)
		{
			const auto& dimension = meta.getDimension();

			uint32 widthSnapShot;
			uint32 heightSnapShot;
			quantifyDim(widthSnapShot, heightSnapShot, dimension.x, dimension.y);

			dim.x = widthSnapShot;
			dim.y = heightSnapShot;

			std::vector<uint8> data(widthSnapShot * heightSnapShot * numChannel);

			// if (meta.isSRGB())
			{
				// NOTE: We assume all data inputs is srgb mode.
				stbir_resize_uint8_srgb_edgemode(
					pixels,
					dimension.x,
					dimension.y,
					0,
					data.data(),
					widthSnapShot,
					heightSnapShot,
					0,
					numChannel,
					numChannel == 4 ? 3 : STBIR_ALPHA_CHANNEL_NONE,
					STBIR_FLAG_ALPHA_PREMULTIPLIED,
					STBIR_EDGE_CLAMP
				);

				// BC compression.
				dxt::mipmapCompressBC3(outData, data, dim.x, dim.y);
			}
			/*
			else
			{
				stbir_resize_uint8(
					pixels,
					dimension.x,
					dimension.y,
					0,
					data.data(),
					widthSnapShot,
					heightSnapShot,
					0,
					numChannel
				);
			}
			*/

		}
		
		void build16bit(const TextureAsset& meta, std::vector<uint8>& ldrDatas, const uint16* pixels, int numChannel, math::uvec2& dim)
		{
			const auto& dimension = meta.getDimension();

			uint32 widthSnapShot;
			uint32 heightSnapShot;
			snapshot::quantifyDim(widthSnapShot, heightSnapShot, dimension.x, dimension.y);

			dim.x = widthSnapShot;
			dim.y = heightSnapShot;

			std::vector<uint16> snapshotData;
			snapshotData.resize(widthSnapShot * heightSnapShot * numChannel);

			stbir_resize_uint16_generic(
				pixels,
				dimension.x,
				dimension.y,
				0,
				snapshotData.data(),
				widthSnapShot,
				heightSnapShot,
				0,
				numChannel,
				numChannel == 4 ? 3 : STBIR_ALPHA_CHANNEL_NONE,
				STBIR_FLAG_ALPHA_PREMULTIPLIED,
				STBIR_EDGE_CLAMP,
				STBIR_FILTER_DEFAULT,
				STBIR_COLORSPACE_LINEAR,
				nullptr
			);

			// Remap to 255.0f. maybe need tonemaper.
			std::vector<uint8> ldrDatasRaw(snapshotData.size());
			for (size_t i = 0; i < ldrDatasRaw.size(); i += 4)
			{
				ldrDatasRaw[i] = uint8(float(snapshotData[i]) / 65535.0f * 255.0f);
				ldrDatasRaw[i] = uint8(float(snapshotData[i]) / 65535.0f * 255.0f);
				ldrDatasRaw[i] = uint8(float(snapshotData[i]) / 65535.0f * 255.0f);
				ldrDatasRaw[i] = uint8(float(snapshotData[i]) / 65535.0f * 255.0f);
			}

			// BC compression.
			dxt::mipmapCompressBC3(ldrDatas, ldrDatasRaw, dim.x, dim.y);
		}
	}

	bool IAsset::saveSnapShot(const math::uvec2& snapshot, const std::vector<uint8>& datas)
	{
		if (saveAsset(datas, ECompressionMode::Lz4, getSnapshotPath(), false))
		{
			m_snapshotDimension = snapshot;
			markDirty();

			return true;
		}

		return false;
	}

	GPUTextureAssetRef IAsset::getSnapshotImage()
	{
		if (auto cacheTexture = m_snapshotWeakPtr.lock())
		{
			return cacheTexture;
		}

		auto fallback = getContext().getBuiltinTextures().white;
		if (m_snapshotDimension.x == 0 || m_snapshotDimension.y == 0)
		{
			return fallback;
		}

		auto imageCI = helper::buildBasicUploadImageCreateInfo(
			m_snapshotDimension.x, 
			m_snapshotDimension.y,
			snapshot::kFormat);
		
		auto uploadVMACI = helper::buildVMAUploadImageAllocationCI();

		auto assetPtr = shared_from_this();
		auto textureAsset = std::make_shared<GPUTextureAsset>(
			fallback.get(),
			m_saveInfo.getSnapshotCachePath(),
			imageCI,
			uploadVMACI
		);

		getContext().getAsyncUploader().addTask(textureAsset->getSize(),
			[textureAsset, assetPtr](uint32 offset, uint32 queueFamily, void* mapped, VkCommandBuffer cmd, VkBuffer buffer)
			{
				auto texture = textureAsset->getOwnHandle();
				const auto range = helper::buildBasicImageSubresource();

				{
					std::vector<uint8> snapshotData{ };
					if (!std::filesystem::exists(assetPtr->getSnapshotPath()))
					{
						checkEntry();
					}
					else
					{
						LOG_TRACE("Found snapshot for asset {} cache in disk so just load.",
							utf8::utf16to8(assetPtr->getSaveInfo().relativeAssetStorePath().u16string()));

						loadAsset(snapshotData, assetPtr->getSnapshotPath());
					}
					check(snapshotData.size() <= textureAsset->getSize());
					memcpy(mapped, snapshotData.data(), snapshotData.size());
				}

				
				textureAsset->prepareToUpload(cmd, queueFamily, range);
				{
					const auto destLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

					VkBufferImageCopy region{ };
					region.bufferOffset = offset;
					region.bufferRowLength = 0;
					region.bufferImageHeight = 0;
					region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
					region.imageSubresource.mipLevel = 0;
					region.imageSubresource.baseArrayLayer = 0;
					region.imageSubresource.layerCount = 1;
					region.imageOffset = { 0, 0, 0 };
					region.imageExtent = texture->getExtent();

					vkCmdCopyBufferToImage(cmd, buffer, *texture, destLayout, 1, &region);
				}

				// Finish upload we change to graphics family.
				textureAsset->finishUpload(cmd, getContext().getQueuesInfo().graphicsFamily.get(), range);
			},
			[textureAsset]() // Finish loading.
			{
				textureAsset->setLoadingState(false);
			});

		// Add in weak ptr.
		m_snapshotWeakPtr = textureAsset;
		return textureAsset;
	}

	void uiDrawImportConfig(TextureAssetImportConfigRef config)
	{
		const float sizeLable = ImGui::GetFontSize();
		if (ImGui::BeginTable("##ConfigTable", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_Borders))
		{
			ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch);

			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::Text("Is sRGB");
			ImGui::TableSetColumnIndex(1);
			ImGui::Checkbox("##SRGB", &config->bSRGB);

			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::Text("Build Mipmap");
			ImGui::TableSetColumnIndex(1);
			ImGui::Checkbox("##MipMap", &config->bGenerateMipmap);

			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::Text("Alpha Cutoff");
			ImGui::TableSetColumnIndex(1);
			ImGui::SliderFloat("##AlphaCutoff", &config->alphaMipmapCutoff, 0.0f, 1.0f, "%.2f");

			int formatValue = (int)config->format;

			std::array<std::string, (size_t)ETextureFormat::MAX> formatList { };
			std::array<const char*, (size_t)ETextureFormat::MAX> formatListChar{ };
			for (size_t i = 0; i < formatList.size(); i++)
			{
				std::string prefix = (formatValue == i) ? "  * " : "    ";
				formatList[i] = std::format("{0} {1}", prefix, nameof::nameof_enum(ETextureFormat(i)));
				formatListChar[i] = formatList[i].c_str();
			}

			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::Text("Format");
			ImGui::TableSetColumnIndex(1);
			ImGui::Combo("##Format", &formatValue, formatListChar.data(), formatListChar.size());
			config->format = ETextureFormat(formatValue);
			ImGui::EndTable();
		}
	}

	bool importFromConfig(TextureAssetImportConfigRef config)
	{
		const std::filesystem::path& srcPath = config->importFilePath;
		const std::filesystem::path& savePath = config->storeFilePath;
		const auto& projectPaths = Project::get().getPath();

		auto& assetManager = Application::get().getAssetManager();
		const auto& meta = TextureAsset::kAssetTypeMeta;

		TextureAssetRef texturePtr;
		{
			// Create asset texture.
			AssetSaveInfo saveInfo = config->getSaveInfo(meta.suffix);
			texturePtr = assetManager.createAsset<TextureAsset>(saveInfo, true);
		}
		texturePtr->markDirty();

		uint32 channelCount;
		uint32 pixelSampleOffset;
		getChannelCountOffset(channelCount, pixelSampleOffset, config->format);

		auto importLdrTexture = [&]() -> bool
		{
			auto imageLdr = std::make_unique<ImageLdr2D>();
			if (!imageLdr->fillFromFile(srcPath.string(), 4))
			{
				return false;
			}

			const int32 texWidth = imageLdr->getWidth();
			const int32 texHeight = imageLdr->getHeight();

			const auto* pixels = imageLdr->getPixels();

			// Texture is power of two.
			const bool bPOT = isPOT(texWidth) && isPOT(texHeight);

			// Override mipmap state by texture dimension.
			config->bGenerateMipmap = bPOT ? config->bGenerateMipmap : false;

			// Only compressed bc when src image all dimension is small than 4.
			const bool bCanCompressed = bPOT && (texWidth >= 4) && (texHeight >= 4);

			texturePtr->initBasicInfo(
				config->bSRGB,
				config->bGenerateMipmap ? getMipLevelsCount(texWidth, texHeight) : 1U,
				getFormatFromConfig(*config, bCanCompressed),
				{ texWidth, texHeight, 1 },
				config->alphaMipmapCutoff);

			// Build snapshot.
			{
				math::uvec2 dimSnapshot;
				std::vector<uint8> data{};

				// NOTE: we current only use srgb for snapshot texture.
				snapshot::build8bit(*texturePtr, data, (const unsigned char*)pixels, 4, dimSnapshot);

				texturePtr->saveSnapShot(dimSnapshot, data);
			}

			// Build mipmap.
			{
				TextureAssetBin bin{};
				mipmap::buildMipmapData<uint8>(
					channelCount, 
					pixelSampleOffset, 
					pixels, 
					bin,
					texturePtr->m_mipmapCount,
					texturePtr->m_dimension.x,
					texturePtr->m_dimension.y,
					texturePtr->m_mipmapAlphaCutoff,
					texturePtr->m_bSRGB);

				switch (texturePtr->getFormat())
				{
				case VK_FORMAT_R8G8B8A8_UNORM:
				case VK_FORMAT_R8G8B8A8_SRGB:
				case VK_FORMAT_R8G8_UNORM:
				case VK_FORMAT_R8_UNORM:
				{
					// Do nothing.
				}
				break;
				case VK_FORMAT_BC3_UNORM_BLOCK:
				case VK_FORMAT_BC3_SRGB_BLOCK:
				case VK_FORMAT_BC1_RGB_UNORM_BLOCK:
				case VK_FORMAT_BC1_RGB_SRGB_BLOCK:
				case VK_FORMAT_BC5_UNORM_BLOCK:
				case VK_FORMAT_BC4_UNORM_BLOCK:
				{
					check(bCanCompressed);
					dxt::mipmapCompressBC(bin, *texturePtr);
				}
				break;
				default: checkEntry();
				}

				saveAsset(bin, ECompressionMode::Lz4, texturePtr->getBinPath(), false);
			}

			return true;
		};

		auto importHalfTexture = [&]() -> bool
		{
			auto imageHalf = std::make_unique<ImageHalf2D>();
			if (!imageHalf->fillFromFile(srcPath.string(), 4))
			{
				return false;
			}

			const auto* pixels = imageHalf->getPixels();

			const int32 texWidth = imageHalf->getWidth();
			const int32 texHeight = imageHalf->getHeight();

			// Texture is power of two.
			const bool bPOT = isPOT(texWidth) && isPOT(texHeight);

			// Override mipmap state by texture dimension.
			config->bGenerateMipmap = bPOT ? config->bGenerateMipmap : false;

			// Only compressed bc when src image all dimension is small than 4.
			const bool bCanCompressed = bPOT && (texWidth >= 4) && (texHeight >= 4);

			texturePtr->initBasicInfo(
				false, // No srgb for half texture.
				config->bGenerateMipmap ? getMipLevelsCount(texWidth, texHeight) : 1U,
				getFormatFromConfig(*config, bCanCompressed),
				{ texWidth, texHeight, 1 },
				config->alphaMipmapCutoff);

			// Build snapshot.
			{
				math::uvec2 dimSnapshot;
				std::vector<uint8> data{};

				snapshot::build16bit(*texturePtr, data, pixels, 4, dimSnapshot);

				texturePtr->saveSnapShot(dimSnapshot, data);
			}

			// Build mipmap.
			{
				TextureAssetBin bin{};
				mipmap::buildMipmapData<uint16>(
					channelCount,
					pixelSampleOffset,
					pixels,
					bin,
					texturePtr->m_mipmapCount,
					texturePtr->m_dimension.x,
					texturePtr->m_dimension.y,
					texturePtr->m_mipmapAlphaCutoff,
					texturePtr->m_bSRGB);

				switch (texturePtr->getFormat())
				{
				case VK_FORMAT_R16G16B16A16_UNORM:
				case VK_FORMAT_R16_UNORM:
				{
					// Do nothing.
				}
				break;
				default: checkEntry();
				}

				saveAsset(bin, ECompressionMode::Lz4, texturePtr->getBinPath(), false);
			}

			return true;
		};


		bool bImportSucceed = false;
		switch (config->format)
		{
		case ETextureFormat::R8G8B8A8:
		case ETextureFormat::BC3:
		case ETextureFormat::BC1:
		case ETextureFormat::BC5:
		case ETextureFormat::R8G8:
		case ETextureFormat::Greyscale:
		case ETextureFormat::R8:
		case ETextureFormat::G8:
		case ETextureFormat::B8:
		case ETextureFormat::A8:
		case ETextureFormat::BC4Greyscale:
		case ETextureFormat::BC4R8:
		case ETextureFormat::BC4G8:
		case ETextureFormat::BC4B8:
		case ETextureFormat::BC4A8:
		{
			bImportSucceed = importLdrTexture();
			break;
		}
		case ETextureFormat::RGBA16Unorm:
		case ETextureFormat::R16Unorm:
		{
			bImportSucceed = importHalfTexture();
			break;
		}
		default:
			checkEntry();
		}

		// Copy raw asset to project asset.
		if (bImportSucceed)
		{
			auto copyDest = savePath.u16string() + srcPath.filename().extension().u16string();
			checkMsgf(!std::filesystem::exists(copyDest), "Can't copy same resource multi times.");
			std::filesystem::copy(srcPath, copyDest);
			std::filesystem::path copyDestPath = copyDest;

			texturePtr->m_rawAssetPath = buildRelativePath(projectPaths.assetPath.u16(), copyDestPath);
		}

		LOG_TRACE("Import texture [size:({0},{1},{2}), mipmap:{6}] from '{3}' to '{4}' with format '{5}'.",
			texturePtr->m_dimension.x,
			texturePtr->m_dimension.y,
			texturePtr->m_dimension.z,
			texturePtr->m_rawAssetPath.u8(),
			utf8::utf16to8(texturePtr->m_saveInfo.relativeAssetStorePath().u16string()),
			nameof::nameof_enum(texturePtr->m_format),
			texturePtr->m_mipmapCount);
		return texturePtr->save();
	}

	AssetTypeMeta TextureAsset::createTypeMeta()
	{
		AssetTypeMeta result;
		// 
		result.name = "Texture";
		result.icon = ICON_FA_IMAGE;
		result.decoratedName = std::string("  ") + ICON_FA_IMAGE + "    Texture";

		//
		result.suffix = ".assettexture";

		// Import config.
		{
			result.importConfig.bImportable = true;
			result.importConfig.rawDataExtension = "jpg,jpeg,png,tga,exr;jpg,jpeg;png;tga;exr";
			result.importConfig.getAssetImportConfig = [] { return std::make_shared<TextureAssetImportConfig>(); };
			result.importConfig.uiDrawAssetImportConfig = [](IAssetImportConfigRef config)
			{
				uiDrawImportConfig(std::static_pointer_cast<TextureAssetImportConfig>(config));
			};
			result.importConfig.importAssetFromConfig = [](IAssetImportConfigRef config)
			{
				return importFromConfig(std::static_pointer_cast<TextureAssetImportConfig>(config));
			};
		}


		return result;
	};
}


