// Precompute atmosphere model, reference from https://github.com/ebruneton/precomputed_atmospheric_scattering
#pragma once

#include <utils/engine.h>
#include <shader/atmosphere.hlsli>
#include <graphics/graphics.h>
#include <renderer/compute_pass.h>
#include <renderer/graphics_pass.h>
#include <scene/manager/scene_manager.h>

namespace chord
{
	extern float3 getEarthCenterKm();
	extern double3 getCameraToEarthCenterKm(const double3 cameraPosition, double3& cameraKm);

	struct AtmosphereLut
	{
		graphics::PoolTextureRef transmittance     = nullptr;
		graphics::PoolTextureRef scatteringTexture = nullptr;
		graphics::PoolTextureRef irradianceTexture = nullptr;
		graphics::PoolTextureRef optionalSingleMieScatteringTexture = nullptr;
	};

	enum class Luminance
	{
		// Render the spectral radiance at kLambdaR, kLambdaG, kLambdaB.
		NONE,

		// Render the sRGB luminance, using an approximate (on the fly) conversion
		// from 3 spectral radiance values only (see section 14.3 in <a href=
		// "https://arxiv.org/pdf/1612.04336.pdf">A Qualitative and Quantitative
		//  Evaluation of 8 Clear Sky Models</a>).
		APPROXIMATE,

		// Render the sRGB luminance, precomputed from 15 spectral radiance values
		// (see section 4.4 in <a href=
		// "http://www.oskee.wz.cz/stranka/uploads/SCCG10ElekKmoch.pdf">Real-time
		//  Spectral Scattering in Large-scale Natural Participating Media</a>).
		PRECOMPUTED,

		MAX
	};

	struct AtmosphereConfig
	{
		CHORD_DEFAULT_COMPARE_ARCHIVE(AtmosphereConfig);

		bool bUseConstantSolarSpectrum = false; // True use kConstantSolarIrradiance, false use realistic spectral.
		bool bUseHalfPrecision         = true;  // True 16bit, false use 32 bit.
		bool bUseCombinedTexture       = false;
		bool bUseOzone                 = true;
		int iterationTimes             = 5;
		Luminance luminance = Luminance::PRECOMPUTED;
	};

	struct AtmosphereRawData
	{
		CHORD_DEFAULT_COMPARE_ARCHIVE(AtmosphereRawData);

		double maxSunZenithAngle;
		DensityProfileLayer rayleighLayer;
		DensityProfileLayer mieLayer;
		std::array<DensityProfileLayer, 2> ozoneLayer;

		std::vector<double> wavelengths;
		std::vector<double> solarIrradiance;
		std::vector<double> rayleighScattering;
		std::vector<double> mieScattering;
		std::vector<double> mieExtinction;
		std::vector<double> absorptionExtinction;
		std::vector<double> groundAlbedo;

		uint32 numPrecomputedWavelengths;
		float3 skySpectralRadianceToLumiance;
		float3 sunSpectralRadianceToLumiance;

		AtmosphereParameters buildAtmosphereParameters(const float3 lambdas, const AtmosphereConfig& config) const;
	};

	class AtmosphereManager : NonCopyable, public ISceneManager
	{
	public:
		ARCHIVE_DECLARE;

		explicit AtmosphereManager();
		virtual ~AtmosphereManager() = default;

		AtmosphereParameters update(const ApplicationTickData& tickData);

		const AtmosphereLut& render(
			const ApplicationTickData& tickData, 
			graphics::CommandList& cmd, 
			graphics::GraphicsQueue& queue);

		const AtmosphereLut& getLuts() const { return m_luts; }

		// Clear GPU lut assets.
		void clearLuts() { m_luts = {}; }

		SkyLightInfo getSunLightInfo() const;
		float3 getSunDirection() const;

		static constexpr math::vec3 kDefaultSunDirection = math::vec3(0.1f, -0.8f, 0.2f);

	protected:
		virtual void onDrawUI(SceneRef scene) override;

		void updateCacheParameter();

	private:
		void createTextures();

		void computeLuts(graphics::CommandList& cmd, graphics::GraphicsQueue& queue, int32 numScatteringOrders = 5);
		void precompute(
			graphics::CommandList& cmd,
			graphics::GraphicsQueue& queue,
			graphics::PoolTextureRef deltaIrradianceTexture,
			graphics::PoolTextureRef deltaRayleighScatteringTexture,
			graphics::PoolTextureRef deltaMieScatteringTexture,
			graphics::PoolTextureRef deltaScatteringDensityTexture,
			graphics::PoolTextureRef deltaMultipleScatteringTexture,
			const float3& lambdas,
			const float luminanceFromRadiance[9],
			bool bBlend,
			int32 numScatteringOrders);

		AtmosphereParameters buildAtmosphereParameters(const float3 lambdas) const;

	private:
		// Current atmosphere dirty and need update?
		bool m_dirty = true;

		// Allocated luts.
		AtmosphereLut m_luts;

		// Update frame.
		uint64 m_updatedFrame = -1;

		// Cache raw data.
		AtmosphereRawData m_cacheAtmosphereRawData { };

	private: // Archives.
		AtmosphereConfig m_config{ };

		//
		math::vec3 m_sunRotation = kDefaultSunDirection;
	};
}