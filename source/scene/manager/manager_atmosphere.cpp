#include <scene/manager/manager_atmosphere.h>
#include <renderer/renderer.h>
#include <shader/atmosphere.hlsl>
#include <renderer/compute_pass.h>
#include <renderer/graphics_pass.h>
#include <shader/colorspace.h>
#include <fontawsome/IconsFontAwesome6.h>
#include <ui/ui_helper.h>
#include <scene/scene.h>

namespace chord
{
    // kilometers unit.
    constexpr double kLengthUnitInMeters = 1000.0;
    constexpr double kPi = 3.1415926;
    constexpr double kSunAngularRadius = 0.00935 / 2.0;
    constexpr double kSunSolidAngle = kPi * kSunAngularRadius * kSunAngularRadius;

    // Values from "Reference Solar Spectral Irradiance: ASTM G-173", ETR column
    // (see http://rredc.nrel.gov/solar/spectra/am1.5/ASTMG173/ASTMG173.html),
    // summed and averaged in each bin (e.g. the value for 360nm is the average
    // of the ASTM G-173 values for all wavelengths between 360 and 370nm).
    // Values in W.m^-2.
    constexpr int kLambdaMin = 360;
    constexpr int kLambdaMax = 830;
    constexpr double kSolarIrradiance[48] =
    {
        1.11776, 1.14259, 1.01249, 1.14716, 1.72765, 1.73054, 1.6887, 1.61253,
        1.91198, 2.03474, 2.02042, 2.02212, 1.93377, 1.95809, 1.91686, 1.8298,
        1.8685, 1.8931, 1.85149, 1.8504, 1.8341, 1.8345, 1.8147, 1.78158, 1.7533,
        1.6965, 1.68194, 1.64654, 1.6048, 1.52143, 1.55622, 1.5113, 1.474, 1.4482,
        1.41018, 1.36775, 1.34188, 1.31429, 1.28303, 1.26758, 1.2367, 1.2082,
        1.18737, 1.14683, 1.12362, 1.1058, 1.07124, 1.04992
    };

    // Values from http://www.iup.uni-bremen.de/gruppen/molspec/databases/
    // referencespectra/o3spectra2011/index.html for 233K, summed and averaged in
    // each bin (e.g. the value for 360nm is the average of the original values
    // for all wavelengths between 360 and 370nm). Values in m^2.
    constexpr double kOzoneCrossSection[48] =
    {
        1.18e-27, 2.182e-28, 2.818e-28, 6.636e-28, 1.527e-27, 2.763e-27, 5.52e-27,
        8.451e-27, 1.582e-26, 2.316e-26, 3.669e-26, 4.924e-26, 7.752e-26, 9.016e-26,
        1.48e-25, 1.602e-25, 2.139e-25, 2.755e-25, 3.091e-25, 3.5e-25, 4.266e-25,
        4.672e-25, 4.398e-25, 4.701e-25, 5.019e-25, 4.305e-25, 3.74e-25, 3.215e-25,
        2.662e-25, 2.238e-25, 1.852e-25, 1.473e-25, 1.209e-25, 9.423e-26, 7.455e-26,
        6.566e-26, 5.105e-26, 4.15e-26, 4.228e-26, 3.237e-26, 2.451e-26, 2.801e-26,
        2.534e-26, 1.624e-26, 1.465e-26, 2.078e-26, 1.383e-26, 7.105e-27
    };

    // From https://en.wikipedia.org/wiki/Dobson_unit, in molecules.m^-2.
    constexpr double kDobsonUnit = 2.687e20;

    // Values from "CIE (1931) 2-deg color matching functions", see
    // "http://web.archive.org/web/20081228084047/
    //  http://www.cvrl.org/database/data/cmfs/ciexyz31.txt".
    constexpr double CIE_2_DEG_COLOR_MATCHING_FUNCTIONS[380] = 
    {
        360, 0.000129900000, 0.000003917000, 0.000606100000,
        365, 0.000232100000, 0.000006965000, 0.001086000000,
        370, 0.000414900000, 0.000012390000, 0.001946000000,
        375, 0.000741600000, 0.000022020000, 0.003486000000,
        380, 0.001368000000, 0.000039000000, 0.006450001000,
        385, 0.002236000000, 0.000064000000, 0.010549990000,
        390, 0.004243000000, 0.000120000000, 0.020050010000,
        395, 0.007650000000, 0.000217000000, 0.036210000000,
        400, 0.014310000000, 0.000396000000, 0.067850010000,
        405, 0.023190000000, 0.000640000000, 0.110200000000,
        410, 0.043510000000, 0.001210000000, 0.207400000000,
        415, 0.077630000000, 0.002180000000, 0.371300000000,
        420, 0.134380000000, 0.004000000000, 0.645600000000,
        425, 0.214770000000, 0.007300000000, 1.039050100000,
        430, 0.283900000000, 0.011600000000, 1.385600000000,
        435, 0.328500000000, 0.016840000000, 1.622960000000,
        440, 0.348280000000, 0.023000000000, 1.747060000000,
        445, 0.348060000000, 0.029800000000, 1.782600000000,
        450, 0.336200000000, 0.038000000000, 1.772110000000,
        455, 0.318700000000, 0.048000000000, 1.744100000000,
        460, 0.290800000000, 0.060000000000, 1.669200000000,
        465, 0.251100000000, 0.073900000000, 1.528100000000,
        470, 0.195360000000, 0.090980000000, 1.287640000000,
        475, 0.142100000000, 0.112600000000, 1.041900000000,
        480, 0.095640000000, 0.139020000000, 0.812950100000,
        485, 0.057950010000, 0.169300000000, 0.616200000000,
        490, 0.032010000000, 0.208020000000, 0.465180000000,
        495, 0.014700000000, 0.258600000000, 0.353300000000,
        500, 0.004900000000, 0.323000000000, 0.272000000000,
        505, 0.002400000000, 0.407300000000, 0.212300000000,
        510, 0.009300000000, 0.503000000000, 0.158200000000,
        515, 0.029100000000, 0.608200000000, 0.111700000000,
        520, 0.063270000000, 0.710000000000, 0.078249990000,
        525, 0.109600000000, 0.793200000000, 0.057250010000,
        530, 0.165500000000, 0.862000000000, 0.042160000000,
        535, 0.225749900000, 0.914850100000, 0.029840000000,
        540, 0.290400000000, 0.954000000000, 0.020300000000,
        545, 0.359700000000, 0.980300000000, 0.013400000000,
        550, 0.433449900000, 0.994950100000, 0.008749999000,
        555, 0.512050100000, 1.000000000000, 0.005749999000,
        560, 0.594500000000, 0.995000000000, 0.003900000000,
        565, 0.678400000000, 0.978600000000, 0.002749999000,
        570, 0.762100000000, 0.952000000000, 0.002100000000,
        575, 0.842500000000, 0.915400000000, 0.001800000000,
        580, 0.916300000000, 0.870000000000, 0.001650001000,
        585, 0.978600000000, 0.816300000000, 0.001400000000,
        590, 1.026300000000, 0.757000000000, 0.001100000000,
        595, 1.056700000000, 0.694900000000, 0.001000000000,
        600, 1.062200000000, 0.631000000000, 0.000800000000,
        605, 1.045600000000, 0.566800000000, 0.000600000000,
        610, 1.002600000000, 0.503000000000, 0.000340000000,
        615, 0.938400000000, 0.441200000000, 0.000240000000,
        620, 0.854449900000, 0.381000000000, 0.000190000000,
        625, 0.751400000000, 0.321000000000, 0.000100000000,
        630, 0.642400000000, 0.265000000000, 0.000049999990,
        635, 0.541900000000, 0.217000000000, 0.000030000000,
        640, 0.447900000000, 0.175000000000, 0.000020000000,
        645, 0.360800000000, 0.138200000000, 0.000010000000,
        650, 0.283500000000, 0.107000000000, 0.000000000000,
        655, 0.218700000000, 0.081600000000, 0.000000000000,
        660, 0.164900000000, 0.061000000000, 0.000000000000,
        665, 0.121200000000, 0.044580000000, 0.000000000000,
        670, 0.087400000000, 0.032000000000, 0.000000000000,
        675, 0.063600000000, 0.023200000000, 0.000000000000,
        680, 0.046770000000, 0.017000000000, 0.000000000000,
        685, 0.032900000000, 0.011920000000, 0.000000000000,
        690, 0.022700000000, 0.008210000000, 0.000000000000,
        695, 0.015840000000, 0.005723000000, 0.000000000000,
        700, 0.011359160000, 0.004102000000, 0.000000000000,
        705, 0.008110916000, 0.002929000000, 0.000000000000,
        710, 0.005790346000, 0.002091000000, 0.000000000000,
        715, 0.004109457000, 0.001484000000, 0.000000000000,
        720, 0.002899327000, 0.001047000000, 0.000000000000,
        725, 0.002049190000, 0.000740000000, 0.000000000000,
        730, 0.001439971000, 0.000520000000, 0.000000000000,
        735, 0.000999949300, 0.000361100000, 0.000000000000,
        740, 0.000690078600, 0.000249200000, 0.000000000000,
        745, 0.000476021300, 0.000171900000, 0.000000000000,
        750, 0.000332301100, 0.000120000000, 0.000000000000,
        755, 0.000234826100, 0.000084800000, 0.000000000000,
        760, 0.000166150500, 0.000060000000, 0.000000000000,
        765, 0.000117413000, 0.000042400000, 0.000000000000,
        770, 0.000083075270, 0.000030000000, 0.000000000000,
        775, 0.000058706520, 0.000021200000, 0.000000000000,
        780, 0.000041509940, 0.000014990000, 0.000000000000,
        785, 0.000029353260, 0.000010600000, 0.000000000000,
        790, 0.000020673830, 0.000007465700, 0.000000000000,
        795, 0.000014559770, 0.000005257800, 0.000000000000,
        800, 0.000010253980, 0.000003702900, 0.000000000000,
        805, 0.000007221456, 0.000002607800, 0.000000000000,
        810, 0.000005085868, 0.000001836600, 0.000000000000,
        815, 0.000003581652, 0.000001293400, 0.000000000000,
        820, 0.000002522525, 0.000000910930, 0.000000000000,
        825, 0.000001776509, 0.000000641530, 0.000000000000,
        830, 0.000001251141, 0.000000451810, 0.000000000000,
    };

    static constexpr double kLambdaR = 680.0;
    static constexpr double kLambdaG = 550.0;
    static constexpr double kLambdaB = 440.0;

    constexpr double XYZ_TO_SRGB[9] =
    {
        +3.2409699419, -1.5373831776, -0.4986107603,
        -0.9692436363, +1.8759675015, +0.0415550574,
        +0.0556300797, -0.2039769589, +1.0569715142,
    };

    static double CieColorMatchingFunctionTableValue(double wavelength, int column) 
    {
        if (wavelength <= kLambdaMin || wavelength >= kLambdaMax) 
        {
            return 0.0;
        }

        double u = (wavelength - kLambdaMin) / 5.0;
        int row = static_cast<int>(std::floor(u));
        check(row >= 0 && row + 1 < 95);
        check(CIE_2_DEG_COLOR_MATCHING_FUNCTIONS[4 * row] <= wavelength && CIE_2_DEG_COLOR_MATCHING_FUNCTIONS[4 * (row + 1)] >= wavelength);
        u -= row;

        return CIE_2_DEG_COLOR_MATCHING_FUNCTIONS[4 * row + column] * (1.0 - u) + CIE_2_DEG_COLOR_MATCHING_FUNCTIONS[4 * (row + 1) + column] * u;
    }

    static double Interpolate(const std::vector<double>& wavelengths, const std::vector<double>& wavelength_function, double wavelength) 
    {
        check(wavelength_function.size() == wavelengths.size());
        if (wavelength < wavelengths[0]) 
        {
            return wavelength_function[0];
        }

        for (uint32 i = 0; i < wavelengths.size() - 1; ++i) 
        {
            if (wavelength < wavelengths[i + 1]) 
            {
                double u = (wavelength - wavelengths[i]) / (wavelengths[i + 1] - wavelengths[i]);
                return wavelength_function[i] * (1.0 - u) + wavelength_function[i + 1] * u;
            }
        }
        return wavelength_function[wavelength_function.size() - 1];
    }

    // The returned constants are in lumen.nm / watt.
    void ComputeSpectralRadianceToLuminanceFactors(
        const std::vector<double>& wavelengths,
        const std::vector<double>& solar_irradiance,
        double lambda_power, double* k_r, double* k_g, double* k_b) 
    {
        *k_r = 0.0;
        *k_g = 0.0;
        *k_b = 0.0;

        double solar_r = Interpolate(wavelengths, solar_irradiance, kLambdaR);
        double solar_g = Interpolate(wavelengths, solar_irradiance, kLambdaG);
        double solar_b = Interpolate(wavelengths, solar_irradiance, kLambdaB);
        int dlambda = 1;

        for (int lambda = kLambdaMin; lambda < kLambdaMax; lambda += dlambda) 
        {
            double x_bar = CieColorMatchingFunctionTableValue(lambda, 1);
            double y_bar = CieColorMatchingFunctionTableValue(lambda, 2);
            double z_bar = CieColorMatchingFunctionTableValue(lambda, 3);

            double r_bar = XYZ_TO_SRGB[0] * x_bar + XYZ_TO_SRGB[1] * y_bar + XYZ_TO_SRGB[2] * z_bar;
            double g_bar = XYZ_TO_SRGB[3] * x_bar + XYZ_TO_SRGB[4] * y_bar + XYZ_TO_SRGB[5] * z_bar;
            double b_bar = XYZ_TO_SRGB[6] * x_bar + XYZ_TO_SRGB[7] * y_bar + XYZ_TO_SRGB[8] * z_bar;

            double irradiance = Interpolate(wavelengths, solar_irradiance, lambda);

            *k_r += r_bar * irradiance / solar_r * pow(lambda / kLambdaR, lambda_power);
            *k_g += g_bar * irradiance / solar_g * pow(lambda / kLambdaG, lambda_power);
            *k_b += b_bar * irradiance / solar_b * pow(lambda / kLambdaB, lambda_power);
        }

        *k_r *= MAX_LUMINOUS_EFFICACY * dlambda;
        *k_g *= MAX_LUMINOUS_EFFICACY * dlambda;
        *k_b *= MAX_LUMINOUS_EFFICACY * dlambda;
    }

    static inline float3 interpolateFloat3(const std::vector<double>& wavelengths, const std::vector<double>& v, const float3& lambdas, double scale)
    {
        double r = Interpolate(wavelengths, v, lambdas[0]) * scale;
        double g = Interpolate(wavelengths, v, lambdas[1]) * scale;
        double b = Interpolate(wavelengths, v, lambdas[2]) * scale;

        return float3(r, g, b);
    }

    static inline DensityProfileLayer processDensityLayer(const DensityProfileLayer& layer)
    {
        DensityProfileLayer result{ };
        result.width          = layer.width / kLengthUnitInMeters;
        result.exp_term       = layer.exp_term;
        result.exp_scale      = layer.exp_scale * kLengthUnitInMeters;
        result.linear_term    = layer.linear_term * kLengthUnitInMeters;
        result.constant_term  = layer.constant_term;
        return result;
    };

    // Maximum number density of ozone molecules, in m^-3 (computed so at to get
    // 300 Dobson units of ozone - for this we divide 300 DU by the integral of
    // the ozone density profile defined below, which is equal to 15km).
    constexpr double kMaxOzoneNumberDensity = 300.0 * kDobsonUnit / 15000.0;

    // Wavelength independent solar irradiance "spectrum" (not physically
    // realistic, but was used in the original implementation).
    constexpr double kConstantSolarIrradiance   = 1.5;
    constexpr double kBottomRadius              = 6360000.0;
    constexpr double kTopRadius                 = 6420000.0;
    constexpr double kRayleigh                  = 1.24062e-6;
    constexpr double kRayleighScaleHeight       = 8000.0;
    constexpr double kMieScaleHeight            = 1200.0;
    constexpr double kMieAngstromAlpha          = 0.0;
    constexpr double kMieAngstromBeta           = 5.328e-3;
    constexpr double kMieSingleScatteringAlbedo = 0.9;
    constexpr double kMiePhaseFunctionG         = 0.8;
    constexpr double kGroundAlbedo              = 0.1;

    // Earth center relative to camera. TODO: Add longitude and latitude positioning.
    float3 chord::getEarthCenterKm()
    {
        return float3(0.0f, -kBottomRadius / kLengthUnitInMeters, 0.0f);
    }

    double3 chord::getCameraToEarthCenterKm(const double3 cameraPosition, double3& cameraKm)
    {
        const float3 earthRelativeCenter = getEarthCenterKm();

        // 
        cameraKm = cameraPosition / kLengthUnitInMeters;

        // 
        return cameraKm - double3(earthRelativeCenter);
    }

    AtmosphereManager::AtmosphereManager()
        : ISceneManager("Atmosphere", ICON_FA_MOON + std::string("  Atmosphere"))
    {

    }

    SkyLightInfo AtmosphereManager::getSunLightInfo() const
    {
        SkyLightInfo info{ };
        info.direction = getSunDirection();

        return std::move(info);
    }

    float3 AtmosphereManager::getSunDirection() const
    {
        constexpr math::vec3 forward = math::vec3(0.0f, -1.0f, 0.0f);
        auto result = math::normalize(math::vec3(math::toMat4(glm::quat(m_sunRotation)) * math::vec4(forward, 0.0f)));
        if (result == forward) CHORD_UNLIKELY
        {
            result = math::normalize(math::vec3(1e-3f, -1.0f, 1e-3f));
        }
        return result;
    }

    void AtmosphereManager::onDrawUI(SceneRef scene)
    {
        auto cacheConfig = m_config;

        const float sizeLable = ImGui::GetFontSize();

       // 
        static const std::string sunIcon = std::string("  ") + ICON_FA_SUN + "  ";
        ui::drawVector3(sunIcon.c_str(), m_sunRotation, kDefaultSunDirection, sizeLable * 2);

        if (ImGui::BeginTable("##ConfigTable", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_Borders))
        {


            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch);

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("Ozone");
            ImGui::TableSetColumnIndex(1);
            ImGui::Checkbox("##Ozone", &m_config.bUseOzone);

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("Store Mie In Alpha");
            ImGui::TableSetColumnIndex(1);
            ImGui::Checkbox("##StoreMieInAlpha", &m_config.bUseCombinedTexture);

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("Constant Solar Spectrum");
            ImGui::TableSetColumnIndex(1);
            ImGui::Checkbox("##ConstantSolarSpectrum", &m_config.bUseConstantSolarSpectrum);

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("Half Precision");
            ImGui::TableSetColumnIndex(1);
            ImGui::Checkbox("##HalfPrecision", &m_config.bUseHalfPrecision);

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("Iteration Times");
            ImGui::TableSetColumnIndex(1);
            ImGui::DragInt("##IterationTimes", &m_config.iterationTimes, 1.0f, 4, 100);

            int formatValue = (int)m_config.luminance;
            std::array<std::string, (size_t)Luminance::MAX> formatList{ };
            std::array<const char*, (size_t)Luminance::MAX> formatListChar{ };
            for (size_t i = 0; i < formatList.size(); i++)
            {
                std::string prefix = (formatValue == i) ? "  * " : "    ";
                formatList[i] = std::format("{0} {1}", prefix, nameof::nameof_enum(Luminance(i)));
                formatListChar[i] = formatList[i].c_str();
            }

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("Lumiance Type");
            ImGui::TableSetColumnIndex(1);
            ImGui::Combo("##Format", &formatValue, formatListChar.data(), formatListChar.size());
            m_config.luminance = Luminance(formatValue);
            ImGui::EndTable();
        }

        if (cacheConfig != m_config)
        {
            if (m_config.bUseHalfPrecision != cacheConfig.bUseHalfPrecision ||
                m_config.bUseCombinedTexture != cacheConfig.bUseCombinedTexture)
            {
                // Recreate luts.
                m_luts = {};
            }

            m_dirty = true;
            scene->markDirty();
        }
    }

    void chord::AtmosphereManager::updateCacheParameter()
    {
        auto& rawData = m_cacheAtmosphereRawData;

        rawData.maxSunZenithAngle = (m_config.bUseHalfPrecision ? 102.0 : 120.0) / 180.0 * kPi;
        rawData.rayleighLayer = {};
        {
            rawData.rayleighLayer.width = 0.0f;
            rawData.rayleighLayer.exp_term = 1.0f;
            rawData.rayleighLayer.exp_scale = -1.0f / kRayleighScaleHeight;
            rawData.rayleighLayer.linear_term = 0.0f;
            rawData.rayleighLayer.constant_term = 0.0f;
        };
        rawData.mieLayer = {};
        {
            rawData.mieLayer.width = 0.0f;
            rawData.mieLayer.exp_term = 1.0f;
            rawData.mieLayer.exp_scale = -1.0f / kMieScaleHeight;
            rawData.mieLayer.linear_term = 0.0f;
            rawData.mieLayer.constant_term = 0.0f;
        };
        rawData.ozoneLayer[0] = {};
        {
            rawData.ozoneLayer[0].width = 25000.0f;
            rawData.ozoneLayer[0].exp_term = 0.0f;
            rawData.ozoneLayer[0].exp_scale = 0.0f;
            rawData.ozoneLayer[0].linear_term = 1.0f / 15000.0f;
            rawData.ozoneLayer[0].constant_term = -2.0f / 3.0f;
        };
        rawData.ozoneLayer[1] = {};
        {
            rawData.ozoneLayer[1].width = 0.0f;
            rawData.ozoneLayer[1].exp_term = 0.0f;
            rawData.ozoneLayer[1].exp_scale = 0.0f;
            rawData.ozoneLayer[1].linear_term = -1.0f / 15000.0f;
            rawData.ozoneLayer[1].constant_term = 8.0f / 3.0f;
        };

        for (int l = kLambdaMin; l <= kLambdaMax; l += 10)
        {
            double lambda = static_cast<double>(l) * 1e-3;  // micro-meters
            double mie = kMieAngstromBeta / kMieScaleHeight * pow(lambda, -kMieAngstromAlpha);
            rawData.wavelengths.push_back(l);

            if (m_config.bUseConstantSolarSpectrum)
            {
                rawData.solarIrradiance.push_back(kConstantSolarIrradiance);
            }
            else
            {
                rawData.solarIrradiance.push_back(kSolarIrradiance[(l - kLambdaMin) / 10]);
            }

            rawData.rayleighScattering.push_back(kRayleigh * pow(lambda, -4));
            rawData.mieScattering.push_back(mie * kMieSingleScatteringAlbedo);
            rawData.mieExtinction.push_back(mie);
            rawData.absorptionExtinction.push_back(m_config.bUseOzone ? kMaxOzoneNumberDensity * kOzoneCrossSection[(l - kLambdaMin) / 10] : 0.0);
            rawData.groundAlbedo.push_back(kGroundAlbedo);
        }

        rawData.numPrecomputedWavelengths = (m_config.luminance == Luminance::PRECOMPUTED) ? 15 : 3;

        // Compute the values for the SKY_RADIANCE_TO_LUMINANCE constant. In theory
        // this should be 1 in precomputed illuminance mode (because the precomputed
        // textures already contain illuminance values). In practice, however, storing
        // true illuminance values in half precision textures yields artefacts
        // (because the values are too large), so we store illuminance values divided
        // by MAX_LUMINOUS_EFFICACY instead. This is why, in precomputed illuminance
        // mode, we set SKY_RADIANCE_TO_LUMINANCE to MAX_LUMINOUS_EFFICACY.
        bool bPrecomputeIlluminance = rawData.numPrecomputedWavelengths > 3;

        double sky_k_r = 1.0, sky_k_g = 1.0, sky_k_b = 1.0;
        if (bPrecomputeIlluminance)
        {
            sky_k_r = sky_k_g = sky_k_b = MAX_LUMINOUS_EFFICACY;
        }
        else
        {
            ComputeSpectralRadianceToLuminanceFactors(rawData.wavelengths, rawData.solarIrradiance, -3 /* lambda_power */, &sky_k_r, &sky_k_g, &sky_k_b);
        }

        // Compute the values for the SUN_RADIANCE_TO_LUMINANCE constant.
        double sun_k_r, sun_k_g, sun_k_b;
        ComputeSpectralRadianceToLuminanceFactors(rawData.wavelengths, rawData.solarIrradiance, 0 /* lambda_power */, &sun_k_r, &sun_k_g, &sun_k_b);

        rawData.skySpectralRadianceToLumiance = float3(sky_k_r, sky_k_g, sky_k_b); // 1e5f * (1.150, 0.7130, 0.653)
        rawData.sunSpectralRadianceToLumiance = float3(sun_k_r, sun_k_g, sun_k_b); // 1e5f * (0.983, 0.6995, 0.665)
    }

    AtmosphereParameters AtmosphereManager::update(const ApplicationTickData& tickData)
    {
        // Update once per frame.
        m_updatedFrame = tickData.tickCount;
        if (m_dirty)
        {
            updateCacheParameter();
        }

        //
        return m_cacheAtmosphereRawData.buildAtmosphereParameters({ kLambdaR, kLambdaG, kLambdaB }, m_config);
    }

    static inline uint toGPULumianceMode(Luminance type)
    {
        switch (type)
        {
            case chord::Luminance::NONE:        return LUMINANCE_MODE_NONE;
            case chord::Luminance::APPROXIMATE: return LUMINANCE_MODE_APPROX;
            case chord::Luminance::PRECOMPUTED: return LUMINANCE_MODE_PRECOMPUTE;
        }
        checkEntry();
        return ~0;
    }

    AtmosphereParameters AtmosphereRawData::buildAtmosphereParameters(const float3 lambdas, const AtmosphereConfig& config) const
    {
        AtmosphereParameters atmosphere{ };

        atmosphere.solar_irradiance = interpolateFloat3(wavelengths, solarIrradiance, lambdas, 1.0);
        atmosphere.sun_angular_radius = kSunAngularRadius;
        atmosphere.bottom_radius = kBottomRadius / kLengthUnitInMeters; // To kilometers.
        atmosphere.top_radius = kTopRadius / kLengthUnitInMeters;
        atmosphere.rayleigh_density.layers[0] = processDensityLayer(rayleighLayer);
        atmosphere.rayleigh_density.layers[1] = atmosphere.rayleigh_density.layers[0];

        atmosphere.rayleigh_scattering = interpolateFloat3(wavelengths, rayleighScattering, lambdas, kLengthUnitInMeters);
        atmosphere.mie_density.layers[0] = processDensityLayer(mieLayer);
        atmosphere.mie_density.layers[1] = atmosphere.mie_density.layers[0];

        atmosphere.mie_scattering = interpolateFloat3(wavelengths, mieScattering, lambdas, kLengthUnitInMeters);
        atmosphere.mie_extinction = interpolateFloat3(wavelengths, mieExtinction, lambdas, kLengthUnitInMeters);
        atmosphere.mie_phase_function_g = kMiePhaseFunctionG;
        atmosphere.absorption_density.layers[0] = processDensityLayer(ozoneLayer[0]);
        atmosphere.absorption_density.layers[1] = processDensityLayer(ozoneLayer[1]);
        atmosphere.absorption_extinction = interpolateFloat3(wavelengths, absorptionExtinction, lambdas, kLengthUnitInMeters);
        atmosphere.ground_albedo = interpolateFloat3(wavelengths, groundAlbedo, lambdas, 1.0);
        atmosphere.mu_s_min = cos(maxSunZenithAngle);

        atmosphere.bCombineScattering = config.bUseCombinedTexture ? 1 : 0;

        atmosphere.skySpectralRadianceToLumiance = skySpectralRadianceToLumiance;
        atmosphere.sunSpectralRadianceToLumiance = sunSpectralRadianceToLumiance;
        atmosphere.luminanceMode = toGPULumianceMode(config.luminance);

        atmosphere.earthCenterKm = getEarthCenterKm();
        return atmosphere;
    }

    const AtmosphereLut& AtmosphereManager::render(const ApplicationTickData& tickData, graphics::CommandList& cmd, graphics::GraphicsQueue& queue)
    {
        check(tickData.tickCount == m_updatedFrame);

        // Create texture if not valid.
        if (m_luts.transmittance == nullptr)
        {
            createTextures();
        }

        if (m_dirty)
        {
            computeLuts(cmd, queue, m_config.iterationTimes);
        }

        return getLuts();
    }

    void AtmosphereManager::createTextures()
    {
        using namespace graphics;

        // Now allocated new textures.
        auto& pool = getContext().getTexturePool();

        PoolTextureCreateInfo ci { };
        ci.format    = m_config.bUseHalfPrecision ? VK_FORMAT_R16G16B16A16_SFLOAT : VK_FORMAT_R32G32B32A32_SFLOAT;
        ci.usage     = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        ci.mipLevels = 1;

        {
            ci.extent = { TRANSMITTANCE_TEXTURE_WIDTH, TRANSMITTANCE_TEXTURE_HEIGHT, 1 };
            ci.imageType = VK_IMAGE_TYPE_2D;
            m_luts.transmittance = pool.create("Atmosphere-TransmittanceLut", ci);
        }

        {
            ci.extent = { SCATTERING_TEXTURE_WIDTH, SCATTERING_TEXTURE_HEIGHT, SCATTERING_TEXTURE_DEPTH };
            ci.imageType = VK_IMAGE_TYPE_3D;
            m_luts.scatteringTexture = pool.create("Atmosphere-ScatteringLut", ci);

            if (!m_config.bUseCombinedTexture)
            {
                m_luts.optionalSingleMieScatteringTexture = pool.create("Atmosphere-SingleMieScatteringLut", ci);
            }
        }

        {
            ci.extent = { IRRADIANCE_TEXTURE_WIDTH, IRRADIANCE_TEXTURE_HEIGHT, 1 };
            ci.imageType = VK_IMAGE_TYPE_2D;
            m_luts.irradianceTexture = pool.create("Atmosphere-IrradianceLut", ci);
        }

        // Always mark dirty when texture rebuild.
        m_dirty = true;
    }

    namespace graphics
    {
        PRIVATE_GLOBAL_SHADER(AtmosphereTransmittanceLutCS, "resource/shader/atmosphere.hlsl", "transmittanceLutCS", EShaderStage::Compute);
        PRIVATE_GLOBAL_SHADER(AtmosphereDirectIrradianceCS, "resource/shader/atmosphere.hlsl", "directIrradianceCS", EShaderStage::Compute);
        PRIVATE_GLOBAL_SHADER(AtmosphereSingleScatteringCS, "resource/shader/atmosphere.hlsl", "singleScatteringCS", EShaderStage::Compute);
        PRIVATE_GLOBAL_SHADER(AtmosphereScatteringDensityCS, "resource/shader/atmosphere.hlsl", "scatteringDensityCS", EShaderStage::Compute);
        PRIVATE_GLOBAL_SHADER(AtmosphereIndirectIrradianceCS, "resource/shader/atmosphere.hlsl", "indirectIrradianceCS", EShaderStage::Compute);
        PRIVATE_GLOBAL_SHADER(AtmosphereMultipleScatteringCS, "resource/shader/atmosphere.hlsl", "multipleScatteringCS", EShaderStage::Compute);
    }


    void AtmosphereManager::precompute(
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
        int32 numScatteringOrders)
    {
        using namespace graphics;

        AtmospherePrecomputeParameter constParams { };
        constParams.atmosphere = m_cacheAtmosphereRawData.buildAtmosphereParameters(lambdas, m_config);
        constParams.luminanceFromRadiance_0 = float4(luminanceFromRadiance[0], luminanceFromRadiance[1], luminanceFromRadiance[2], 0.0f);
        constParams.luminanceFromRadiance_1 = float4(luminanceFromRadiance[3], luminanceFromRadiance[4], luminanceFromRadiance[5], 0.0f);
        constParams.luminanceFromRadiance_2 = float4(luminanceFromRadiance[6], luminanceFromRadiance[7], luminanceFromRadiance[8], 0.0f);

        AtmospherePushConsts pushTemplate{ };
        pushTemplate.linearSampler = getContext().getSamplerManager().linearClampEdgeMipPoint().index.get();
        pushTemplate.constBufferId = uploadBufferToGPU(cmd, "AtmosphereParam", &constParams).second;

        // Transmittance.
        {
            auto pushConst = pushTemplate;
            pushConst.uav0 = asUAV(queue, m_luts.transmittance);

            auto computeShader = getContext().getShaderLibrary().getShader<AtmosphereTransmittanceLutCS>();
            addComputePass2(
                queue,
                "Atmosphere-TranmisttanceLut",
                getContext().computePipe(computeShader, "Atmosphere-TranmisttanceLutPipe"),
                pushConst,
                { TRANSMITTANCE_TEXTURE_WIDTH / 8, TRANSMITTANCE_TEXTURE_HEIGHT / 8, 1 });
        }

        // Direct irradiance.
        {
            auto pushConst = pushTemplate;
            pushConst.srv0 = asSRV(queue, m_luts.transmittance);
            pushConst.uav0 = asUAV(queue, deltaIrradianceTexture);
            pushConst.uav1 = asUAV(queue, m_luts.irradianceTexture);

            pushConst.flag0 = bBlend ? 0 : 1;
            auto computeShader = getContext().getShaderLibrary().getShader<AtmosphereDirectIrradianceCS>();
            addComputePass2(
                queue,
                "Atmosphere-DirectIrradiance",
                getContext().computePipe(computeShader, "Atmosphere-DirectIrradiancePipe"),
                pushConst,
                { IRRADIANCE_TEXTURE_WIDTH / 8, IRRADIANCE_TEXTURE_HEIGHT / 8, 1 });
        }

        // Single scattering.
        {
            auto pushConst = pushTemplate;
            pushConst.srv0 = asSRV(queue, m_luts.transmittance);
            pushConst.uav0 = asUAV3DTexture(queue, deltaRayleighScatteringTexture);
            pushConst.uav1 = asUAV3DTexture(queue, deltaMieScatteringTexture);
            pushConst.uav2 = asUAV3DTexture(queue, m_luts.scatteringTexture);
            pushConst.uav3 = m_config.bUseCombinedTexture ? ~0 : asUAV3DTexture(queue, m_luts.optionalSingleMieScatteringTexture);

            pushConst.flag0 = m_config.bUseCombinedTexture ? 0 : 1;
            pushConst.flag1 = bBlend ? 1 : 0;
            auto computeShader = getContext().getShaderLibrary().getShader<AtmosphereSingleScatteringCS>();
            addComputePass2(
                queue,
                "Atmosphere-SingleScattering",
                getContext().computePipe(computeShader, "Atmosphere-SingleScatteringPipe"),
                pushConst,
                { SCATTERING_TEXTURE_WIDTH / 8, SCATTERING_TEXTURE_HEIGHT / 8, SCATTERING_TEXTURE_DEPTH });
        }

        // 
        for (int32 scatteringOrder = 2; scatteringOrder <= numScatteringOrders; scatteringOrder ++)
        {
            {
                auto pushConst = pushTemplate;
                pushConst.srv0 = asSRV(queue, m_luts.transmittance);
                pushConst.srv1 = asSRV3DTexture(queue, deltaRayleighScatteringTexture);
                pushConst.srv2 = asSRV3DTexture(queue, deltaMieScatteringTexture);
                pushConst.srv3 = asSRV3DTexture(queue, deltaMultipleScatteringTexture);
                pushConst.srv4 = asSRV(queue, deltaIrradianceTexture);

                pushConst.flag0 = scatteringOrder;
                pushConst.uav0 = asUAV3DTexture(queue, deltaScatteringDensityTexture);

                auto computeShader = getContext().getShaderLibrary().getShader<AtmosphereScatteringDensityCS>();
                addComputePass2(
                    queue,
                    "Atmosphere-ScatteringDensity",
                    getContext().computePipe(computeShader, "Atmosphere-ScatteringDensityPipe"),
                    pushConst,
                    { SCATTERING_TEXTURE_WIDTH / 8, SCATTERING_TEXTURE_HEIGHT / 8, SCATTERING_TEXTURE_DEPTH });
            }

            {
                auto pushConst = pushTemplate;

                pushConst.srv0 = asSRV3DTexture(queue, deltaRayleighScatteringTexture);
                pushConst.srv1 = asSRV3DTexture(queue, deltaMieScatteringTexture);
                pushConst.srv2 = asSRV3DTexture(queue, deltaMultipleScatteringTexture);

                pushConst.flag0 = scatteringOrder - 1;
                pushConst.uav0 = asUAV(queue, m_luts.irradianceTexture);
                pushConst.uav1 = asUAV(queue, deltaIrradianceTexture);

                auto computeShader = getContext().getShaderLibrary().getShader<AtmosphereIndirectIrradianceCS>();
                addComputePass2(
                    queue,
                    "Atmosphere-IndirectIrradiance",
                    getContext().computePipe(computeShader, "Atmosphere-IndirectIrradiancePipe"),
                    pushConst,
                    { IRRADIANCE_TEXTURE_WIDTH / 8, IRRADIANCE_TEXTURE_HEIGHT / 8, 1 });
            }

            {
                auto pushConst = pushTemplate;
                pushConst.srv0 = asSRV(queue, m_luts.transmittance);
                pushConst.srv1 = asSRV3DTexture(queue, deltaScatteringDensityTexture);
                pushConst.uav0 = asUAV3DTexture(queue, deltaMultipleScatteringTexture);
                pushConst.uav1 = asUAV3DTexture(queue, m_luts.scatteringTexture);

                auto computeShader = getContext().getShaderLibrary().getShader<AtmosphereMultipleScatteringCS>();
                addComputePass2(
                    queue,
                    "Atmosphere-MultipleScattering",
                    getContext().computePipe(computeShader, "Atmosphere-MultipleScatteringPipe"),
                    pushConst,
                    { SCATTERING_TEXTURE_WIDTH / 8, SCATTERING_TEXTURE_HEIGHT / 8, SCATTERING_TEXTURE_DEPTH });
            }
        }
    }

    void AtmosphereManager::computeLuts(graphics::CommandList& cmd, graphics::GraphicsQueue& queue, int32 numScatteringOrders)
    {
        using namespace graphics;
        {
            static const auto clearValue = VkClearColorValue{ .uint32 = { 0, 0, 0, 0} };
            static const auto range = helper::buildBasicImageSubresource();

            queue.clearImage(m_luts.irradianceTexture, &clearValue, 1, &range);
            queue.clearImage(m_luts.scatteringTexture, &clearValue, 1, &range);
            if (m_luts.optionalSingleMieScatteringTexture != nullptr)
            {
                queue.clearImage(m_luts.optionalSingleMieScatteringTexture, &clearValue, 1, &range);
            }
        }


        PoolTextureRef deltaIrradianceTexture;
        PoolTextureRef deltaRayleighScatteringTexture;
        PoolTextureRef deltaMieScatteringTexture;
        PoolTextureRef deltaScatteringDensityTexture;
        {
            auto& pool = getContext().getTexturePool();

            PoolTextureCreateInfo ci{ };
            ci.format    = m_config.bUseHalfPrecision ? VK_FORMAT_R16G16B16A16_SFLOAT : VK_FORMAT_R32G32B32A32_SFLOAT;
            ci.usage     = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
            ci.mipLevels = 1;

            {
                ci.extent = { IRRADIANCE_TEXTURE_WIDTH, IRRADIANCE_TEXTURE_HEIGHT, 1 };
                ci.imageType = VK_IMAGE_TYPE_2D;

                deltaIrradianceTexture = pool.create("Atmosphere-DeltaIrradianceTexture", ci);
            }

            {
                ci.extent = { SCATTERING_TEXTURE_WIDTH, SCATTERING_TEXTURE_HEIGHT, SCATTERING_TEXTURE_DEPTH };
                ci.imageType = VK_IMAGE_TYPE_3D;

                deltaRayleighScatteringTexture = pool.create("Atmosphere-DeltaRayleighScatteringTexture", ci);
                deltaMieScatteringTexture = pool.create("Atmosphere-DeltaMieScatteringTexture", ci);
                deltaScatteringDensityTexture = pool.create("Atmosphere-DeltaScatteringDensityTexture", ci);
            }
        }
        // When compute multiple scattering, delta rayleigh scattering texture is useless, reuse it.
        PoolTextureRef deltaMultipleScatteringTexture = deltaRayleighScatteringTexture;

        if (m_cacheAtmosphereRawData.numPrecomputedWavelengths <= 3)
        {
            float3 lambdas { kLambdaR, kLambdaG, kLambdaB };
            float luminanceFromRadiance[9] = { 1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0 };

            precompute(cmd, queue, deltaIrradianceTexture, deltaRayleighScatteringTexture, deltaMieScatteringTexture, deltaScatteringDensityTexture, deltaMultipleScatteringTexture,
                lambdas, luminanceFromRadiance, false /* blend */, numScatteringOrders);
        }
        else
        {
            constexpr double lambdaMin = double(kLambdaMin);
            constexpr double lambdaMax = double(kLambdaMax);

            int numIterations = (m_cacheAtmosphereRawData.numPrecomputedWavelengths + 2) / 3;
            double dlambda = (lambdaMax - lambdaMin) / (3 * numIterations);
            for (int i = 0; i < numIterations; i++)
            {
                float3 lambdas
                {
                    lambdaMin + (3 * i + 0.5) * dlambda,
                    lambdaMin + (3 * i + 1.5) * dlambda,
                    lambdaMin + (3 * i + 2.5) * dlambda
                };

                auto coeff = [dlambda](double lambda, int component)
                {
                    // Note that we don't include MAX_LUMINOUS_EFFICACY here, to avoid
                    // artefacts due to too large values when using half precision on GPU.
                    // We add this term back in kAtmosphereShader, via
                    // SKY_SPECTRAL_RADIANCE_TO_LUMINANCE (see also the comments in the
                    // Model constructor).
                    double x = CieColorMatchingFunctionTableValue(lambda, 1);
                    double y = CieColorMatchingFunctionTableValue(lambda, 2);
                    double z = CieColorMatchingFunctionTableValue(lambda, 3);

                
                    return static_cast<float>((
                        XYZ_TO_SRGB[component * 3 + 0] * x +
                        XYZ_TO_SRGB[component * 3 + 1] * y +
                        XYZ_TO_SRGB[component * 3 + 2] * z) * dlambda);
                };

                float luminanceFromRadiance[9] = 
                {
                    coeff(lambdas[0], 0), coeff(lambdas[1], 0), coeff(lambdas[2], 0),
                    coeff(lambdas[0], 1), coeff(lambdas[1], 1), coeff(lambdas[2], 1),
                    coeff(lambdas[0], 2), coeff(lambdas[1], 2), coeff(lambdas[2], 2)
                };

                precompute(cmd, queue, deltaIrradianceTexture, deltaRayleighScatteringTexture, deltaMieScatteringTexture, deltaScatteringDensityTexture, deltaMultipleScatteringTexture,
                    lambdas, luminanceFromRadiance, i > 0 /* blend */, numScatteringOrders);
            }

            // After the above iterations, the transmittance texture contains the
            // transmittance for the 3 wavelengths used at the last iteration. But we
            // want the transmittance at kLambdaR, kLambdaG, kLambdaB instead, so we
            // must recompute it here for these 3 wavelengths:
            {
                AtmospherePrecomputeParameter constParams{ };
                constParams.atmosphere = m_cacheAtmosphereRawData.buildAtmosphereParameters({ kLambdaR, kLambdaG, kLambdaB }, m_config);

                AtmospherePushConsts pushConst{ };
                pushConst.linearSampler = getContext().getSamplerManager().linearClampEdgeMipPoint().index.get();
                pushConst.constBufferId = uploadBufferToGPU(cmd, "AtmosphereParam", &constParams).second;
                pushConst.uav0          = asUAV(queue, m_luts.transmittance);

                auto computeShader = getContext().getShaderLibrary().getShader<AtmosphereTransmittanceLutCS>();
                addComputePass2(
                    queue,
                    "Atmosphere-TranmisttanceLut",
                    getContext().computePipe(computeShader, "Atmosphere-TranmisttanceLutPipe"),
                    pushConst,
                    { TRANSMITTANCE_TEXTURE_WIDTH / 8, TRANSMITTANCE_TEXTURE_HEIGHT / 8, 1 });
            }
        }

        // Already compute, so reset state.
        m_dirty = false;
    }
} // namespace chord