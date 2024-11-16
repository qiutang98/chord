#pragma once

/**
 * Copyright (c) 2017 Eric Bruneton
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holders nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
**/

/**
 * NOTE: Atmosphere render in linear srgb color space(Rec709), the engine working color space is
 *       ACEScg, so when sample the render color (eg: sky color, sun color). Need convert to ACEScg color space manually.
**/

#include "base.h"
#include "debug.hlsli"
#include "colorspace.h"

#define TRANSMITTANCE_TEXTURE_WIDTH   256
#define TRANSMITTANCE_TEXTURE_HEIGHT  64
#define SCATTERING_TEXTURE_R_SIZE     32
#define SCATTERING_TEXTURE_MU_SIZE    128
#define SCATTERING_TEXTURE_MU_S_SIZE  32
#define SCATTERING_TEXTURE_NU_SIZE    8
#define SCATTERING_TEXTURE_WIDTH      (SCATTERING_TEXTURE_NU_SIZE * SCATTERING_TEXTURE_MU_S_SIZE)
#define SCATTERING_TEXTURE_HEIGHT     SCATTERING_TEXTURE_MU_SIZE
#define SCATTERING_TEXTURE_DEPTH      SCATTERING_TEXTURE_R_SIZE
#define IRRADIANCE_TEXTURE_WIDTH      64
#define IRRADIANCE_TEXTURE_HEIGHT     16
#define MAX_LUMINOUS_EFFICACY         683.0

#ifndef __cplusplus

// Require LINEAR_SAMPLER before include. 
#ifndef ATMOSPHERE_LINEAR_SAMPLER
    #error "Define a linear sampler for sky before include me!"
#endif 

#include "base.hlsli"
#include "bindless.hlsli"

#define IN(x) const in x
#define OUT(x) out x

#ifndef DISABLE_ATMOSPHERE_CHECK
    #define ATMOSPHERE_CHECK(x) check(x)
#else
    #define ATMOSPHERE_CHECK(x)
#endif

float3 finalRadianceExposureModify(in const GPUBasicData scene, float3 radiance)
{
    // This exposure scale should add for every radiance light.
    return 10.0 * radiance * ((scene.atmosphere.luminanceMode == 0) ? 1.0 : 1e-5f);
}

float ClampCosine(float mu)
{
    return clamp(mu, float(-1.0), float(1.0));
}

float ClampDistance(float d)
{
    return max(d, 0.0);
}

float ClampRadius(IN(AtmosphereParameters) atmosphere, float r) 
{
    return clamp(r, atmosphere.bottom_radius, atmosphere.top_radius);
}

float SafeSqrt(float a) 
{
    return sqrt(max(a, 0.0));
}

float DistanceToTopAtmosphereBoundary(IN(AtmosphereParameters) atmosphere, float r, float mu) 
{
    ATMOSPHERE_CHECK(r <= atmosphere.top_radius);
    ATMOSPHERE_CHECK(mu >= -1.0 && mu <= 1.0);

    float discriminant = r * r * (mu * mu - 1.0) + atmosphere.top_radius * atmosphere.top_radius;
    return ClampDistance(-r * mu + SafeSqrt(discriminant));
}

float DistanceToBottomAtmosphereBoundary(IN(AtmosphereParameters) atmosphere, float r, float mu) 
{
    ATMOSPHERE_CHECK(r >= atmosphere.bottom_radius);
    ATMOSPHERE_CHECK(mu >= -1.0 && mu <= 1.0);
    float discriminant = r * r * (mu * mu - 1.0) + atmosphere.bottom_radius * atmosphere.bottom_radius;
    return ClampDistance(-r * mu - SafeSqrt(discriminant));
}

bool RayIntersectsGround(IN(AtmosphereParameters) atmosphere, float r, float mu) 
{
    ATMOSPHERE_CHECK(r >= atmosphere.bottom_radius);
    ATMOSPHERE_CHECK(mu >= -1.0 && mu <= 1.0);
    return mu < 0.0 && r * r * (mu * mu - 1.0) + atmosphere.bottom_radius * atmosphere.bottom_radius >= 0.0;
}

float GetLayerDensity(IN(DensityProfileLayer) layer, float altitude) 
{
    float density = layer.exp_term * exp(layer.exp_scale * altitude) + layer.linear_term * altitude + layer.constant_term;
    return clamp(density, float(0.0), float(1.0));
}

float GetProfileDensity(IN(DensityProfile) profile, float altitude) 
{
    return altitude < profile.layers[0].width 
        ? GetLayerDensity(profile.layers[0], altitude) 
        : GetLayerDensity(profile.layers[1], altitude);
}

float ComputeOpticalLengthToTopAtmosphereBoundary(
    in const AtmosphereParameters atmosphere, 
    in const DensityProfile profile,
    float r, float mu) 
{
    ATMOSPHERE_CHECK(r >= atmosphere.bottom_radius && r <= atmosphere.top_radius);
    ATMOSPHERE_CHECK(mu >= -1.0 && mu <= 1.0);

    // float of intervals for the numerical integration.
    const int SAMPLE_COUNT = 500;

    // The integration step, i.e. the length of each integration interval.
    float dx = DistanceToTopAtmosphereBoundary(atmosphere, r, mu) / float(SAMPLE_COUNT);

    // Integration loop.
    float result = 0.0;
    for (int i = 0; i <= SAMPLE_COUNT; ++i) 
    {
        float d_i = float(i) * dx;

        // Distance between the current sample inPoint and the planet center.
        float r_i = sqrt(d_i * d_i + 2.0 * r * mu * d_i + r * r);

        // float density at the current sample inPoint (divided by the number density
        // at the bottom of the atmosphere, yielding a dimensionless number).
        float y_i = GetProfileDensity(profile, r_i - atmosphere.bottom_radius);

        // Sample weight (from the trapezoidal rule).
        float weight_i = i == 0 || i == SAMPLE_COUNT ? 0.5 : 1.0;

        result += y_i * weight_i * dx;
    }
    return result;
}

float3 ComputeTransmittanceToTopAtmosphereBoundary(in const AtmosphereParameters atmosphere, float r, float mu) 
{
    ATMOSPHERE_CHECK(r >= atmosphere.bottom_radius && r <= atmosphere.top_radius);
    ATMOSPHERE_CHECK(mu >= -1.0 && mu <= 1.0);

    return exp(-(
        atmosphere.rayleigh_scattering * ComputeOpticalLengthToTopAtmosphereBoundary(atmosphere, atmosphere.rayleigh_density, r, mu) +
        atmosphere.mie_extinction * ComputeOpticalLengthToTopAtmosphereBoundary(atmosphere, atmosphere.mie_density, r, mu) +
        atmosphere.absorption_extinction * ComputeOpticalLengthToTopAtmosphereBoundary(atmosphere, atmosphere.absorption_density, r, mu)));
}

// Some uv remap function for transmittance lut.
float GetTextureCoordFromUnitRange(float x, int textureSize) 
{
    return 0.5 / float(textureSize) + x * (1.0 - 1.0 / float(textureSize));
}

float GetUnitRangeFromTextureCoord(float u, int textureSize) 
{
    return (u - 0.5 / float(textureSize)) / (1.0 - 1.0 / float(textureSize));
}

float2 GetTransmittanceTextureUvFromRMu(
    in const AtmosphereParameters atmosphere,
    float r, float mu) 
{
    ATMOSPHERE_CHECK(r >= atmosphere.bottom_radius && r <= atmosphere.top_radius);
    ATMOSPHERE_CHECK(mu >= -1.0 && mu <= 1.0);

    // Distance to top atmosphere boundary for a horizontal ray at ground level.
    float H = sqrt(atmosphere.top_radius * atmosphere.top_radius - atmosphere.bottom_radius * atmosphere.bottom_radius);

    // Distance to the horizon.
    float rho = SafeSqrt(r * r - atmosphere.bottom_radius * atmosphere.bottom_radius);

    // Distance to the top atmosphere boundary for the ray (r,mu), and its minimum
    // and maximum values over all mu - obtained for (r,1) and (r,mu_horizon).
    float d = DistanceToTopAtmosphereBoundary(atmosphere, r, mu);
    float d_min = atmosphere.top_radius - r;
    float d_max = rho + H;
    float x_mu = (d - d_min) / (d_max - d_min);
    float x_r = rho / H;

    return float2(GetTextureCoordFromUnitRange(x_mu, TRANSMITTANCE_TEXTURE_WIDTH), GetTextureCoordFromUnitRange(x_r, TRANSMITTANCE_TEXTURE_HEIGHT));
}

void GetRMuFromTransmittanceTextureUv(    
    in const AtmosphereParameters atmosphere,
    in const float2 uv, 
    out float r, 
    out float mu) 
{
    ATMOSPHERE_CHECK(uv.x >= 0.0 && uv.x <= 1.0);
    ATMOSPHERE_CHECK(uv.y >= 0.0 && uv.y <= 1.0);

    float x_mu = GetUnitRangeFromTextureCoord(uv.x, TRANSMITTANCE_TEXTURE_WIDTH);
    float x_r  = GetUnitRangeFromTextureCoord(uv.y, TRANSMITTANCE_TEXTURE_HEIGHT);

    // Distance to top atmosphere boundary for a horizontal ray at ground level.
    float H = sqrt(atmosphere.top_radius * atmosphere.top_radius - atmosphere.bottom_radius * atmosphere.bottom_radius);

    // Distance to the horizon, from which we can compute r:
    float rho = H * x_r;
    r = sqrt(rho * rho + atmosphere.bottom_radius * atmosphere.bottom_radius);

    // Distance to the top atmosphere boundary for the ray (r,mu), and its minimum
    // and maximum values over all mu - obtained for (r,1) and (r,mu_horizon) -
    // from which we can recover mu:
    float d_min = atmosphere.top_radius - r;
    float d_max = rho + H;
    float d = d_min + x_mu * (d_max - d_min);
    mu = (d == 0.0) ? 1.0 : (H * H - rho * rho - d * d) / (2.0 * r * d);
    mu = ClampCosine(mu);
}

/*
<p>It is now easy to define a fragment shader function to precompute a texel of
the transmittance texture:
*/

float3 ComputeTransmittanceToTopAtmosphereBoundaryTexture(
    IN(AtmosphereParameters) atmosphere, IN(float2) frag_coord) 
{
    const float2 TRANSMITTANCE_TEXTURE_SIZE = float2(TRANSMITTANCE_TEXTURE_WIDTH, TRANSMITTANCE_TEXTURE_HEIGHT);
    float r;
    float mu;
    GetRMuFromTransmittanceTextureUv(atmosphere, frag_coord / TRANSMITTANCE_TEXTURE_SIZE, r, mu);
    return ComputeTransmittanceToTopAtmosphereBoundary(atmosphere, r, mu);
}

/*
<h4 id="transmittance_lookup">Lookup</h4>

<p>With the help of the above precomputed texture, we can now get the
transmittance between a inPoint and the top atmosphere boundary with a single
texture lookup (assuming there is no intersection with the ground):
*/

float3 GetTransmittanceToTopAtmosphereBoundary(
    IN(AtmosphereParameters) atmosphere,
    IN(Texture2D<float4>) transmittance_texture,
    float r, float mu) 
{
    ATMOSPHERE_CHECK(r >= atmosphere.bottom_radius && r <= atmosphere.top_radius);
    float2 uv = GetTransmittanceTextureUvFromRMu(atmosphere, r, mu);
    return transmittance_texture.SampleLevel(ATMOSPHERE_LINEAR_SAMPLER, uv, 0).xyz;
}

/*
<p>Also, with $r_d=\Vert\bo\bq\Vert=\sqrt{d^2+2r\mu d+r^2}$ and $\mu_d=
\bo\bq\cdot\bp\bi/\Vert\bo\bq\Vert\Vert\bp\bi\Vert=(r\mu+d)/r_d$ the values of
$r$ and $\mu$ at $\bq$, we can get the transmittance between two arbitrary
points $\bp$ and $\bq$ inside the atmosphere with only two texture lookups
(recall that the transmittance between $\bp$ and $\bq$ is the transmittance
between $\bp$ and the top atmosphere boundary, divided by the transmittance
between $\bq$ and the top atmosphere boundary, or the reverse - we continue to
assume that the segment between the two points does not intersect the ground):
*/

float3 GetTransmittance(
    IN(AtmosphereParameters) atmosphere,
    IN(Texture2D<float4>) transmittance_texture,
    float r, float mu, float d, bool ray_r_mu_intersects_ground) 
{
    ATMOSPHERE_CHECK(r >= atmosphere.bottom_radius && r <= atmosphere.top_radius);
    ATMOSPHERE_CHECK(mu >= -1.0 && mu <= 1.0);
    ATMOSPHERE_CHECK(d >= 0.0);

    float r_d = ClampRadius(atmosphere, sqrt(d * d + 2.0 * r * mu * d + r * r));
    float mu_d = ClampCosine((r * mu + d) / r_d);

    if (ray_r_mu_intersects_ground) 
    {
        return min(GetTransmittanceToTopAtmosphereBoundary(atmosphere, transmittance_texture, r_d, -mu_d) /
                   GetTransmittanceToTopAtmosphereBoundary(atmosphere, transmittance_texture, r, -mu), 1.0);
    } 
    else 
    {
        return min(GetTransmittanceToTopAtmosphereBoundary(atmosphere, transmittance_texture, r, mu) /
                   GetTransmittanceToTopAtmosphereBoundary(atmosphere, transmittance_texture, r_d, mu_d), 1.0);
    }
}

// 
float3 GetTransmittanceToSun(
    IN(AtmosphereParameters) atmosphere,
    IN(Texture2D<float4>) transmittance_texture,
    float r, float mu_s) 
{
    float sin_theta_h = atmosphere.bottom_radius / r;
    float cos_theta_h = -sqrt(max(1.0 - sin_theta_h * sin_theta_h, 0.0));
    return GetTransmittanceToTopAtmosphereBoundary(atmosphere, transmittance_texture, r, mu_s) *
        smoothstep(-sin_theta_h * atmosphere.sun_angular_radius, sin_theta_h * atmosphere.sun_angular_radius, mu_s - cos_theta_h);
}

void ComputeSingleScatteringIntegrand(
    IN(AtmosphereParameters) atmosphere,
    IN(Texture2D<float4>) transmittance_texture,
    float r, float mu, float mu_s, float nu, float d,
    bool ray_r_mu_intersects_ground,
    OUT(float3) rayleigh, OUT(float3) mie) 
{
    float r_d = ClampRadius(atmosphere, sqrt(d * d + 2.0 * r * mu * d + r * r));
    float mu_s_d = ClampCosine((r * mu_s + d * nu) / r_d);

    float3 transmittance =
        GetTransmittance(atmosphere, transmittance_texture, r, mu, d, ray_r_mu_intersects_ground) *
        GetTransmittanceToSun(atmosphere, transmittance_texture, r_d, mu_s_d);

    rayleigh = transmittance * GetProfileDensity(atmosphere.rayleigh_density, r_d - atmosphere.bottom_radius);
    mie = transmittance * GetProfileDensity(atmosphere.mie_density, r_d - atmosphere.bottom_radius);
}

float DistanceToNearestAtmosphereBoundary(
    IN(AtmosphereParameters) atmosphere,
    float r, float mu, bool ray_r_mu_intersects_ground) 
{
    if (ray_r_mu_intersects_ground) 
    {
        return DistanceToBottomAtmosphereBoundary(atmosphere, r, mu);
    } 
    else 
    {
        return DistanceToTopAtmosphereBoundary(atmosphere, r, mu);
    }
}

void ComputeSingleScattering(
    IN(AtmosphereParameters) atmosphere,
    IN(Texture2D<float4>) transmittance_texture,
    float r, float mu, float mu_s, float nu,
    bool ray_r_mu_intersects_ground,
    OUT(float3) rayleigh, OUT(float3) mie) 
{
    ATMOSPHERE_CHECK(r >= atmosphere.bottom_radius && r <= atmosphere.top_radius);
    ATMOSPHERE_CHECK(mu >= -1.0 && mu <= 1.0);
    ATMOSPHERE_CHECK(mu_s >= -1.0 && mu_s <= 1.0);
    ATMOSPHERE_CHECK(nu >= -1.0 && nu <= 1.0);

    // float of intervals for the numerical integration.
    const int SAMPLE_COUNT = 50;
    // The integration step, i.e. the length of each integration interval.
    float dx =
        DistanceToNearestAtmosphereBoundary(atmosphere, r, mu,
            ray_r_mu_intersects_ground) / float(SAMPLE_COUNT);

    // Integration loop.
    float3 rayleigh_sum = 0.0;
    float3 mie_sum = 0.0;
    for (int i = 0; i <= SAMPLE_COUNT; ++i) 
    {
        float d_i = float(i) * dx;

        // The Rayleigh and Mie single scattering at the current sample inPoint.
        float3 rayleigh_i;
        float3 mie_i;
        ComputeSingleScatteringIntegrand(atmosphere, transmittance_texture, r, mu, mu_s, nu, d_i, ray_r_mu_intersects_ground, rayleigh_i, mie_i);

        // Sample weight (from the trapezoidal rule).
        float weight_i = (i == 0 || i == SAMPLE_COUNT) ? 0.5 : 1.0;

        //
        rayleigh_sum += rayleigh_i * weight_i;
        mie_sum      +=      mie_i * weight_i;
    }

    // 
    rayleigh = rayleigh_sum * dx * atmosphere.solar_irradiance * atmosphere.rayleigh_scattering;

    // 
    mie = mie_sum * dx * atmosphere.solar_irradiance * atmosphere.mie_scattering;
}

// single scattering precomputation
float4 GetScatteringTextureUvwzFromRMuMuSNu(IN(AtmosphereParameters) atmosphere,
    float r, float mu, float mu_s, float nu,
    bool ray_r_mu_intersects_ground) 
{
    ATMOSPHERE_CHECK(r >= atmosphere.bottom_radius && r <= atmosphere.top_radius);
    ATMOSPHERE_CHECK(mu >= -1.0 && mu <= 1.0);
    ATMOSPHERE_CHECK(mu_s >= -1.0 && mu_s <= 1.0);
    ATMOSPHERE_CHECK(nu >= -1.0 && nu <= 1.0);

    // Distance to top atmosphere boundary for a horizontal ray at ground level.
    float H = sqrt(atmosphere.top_radius * atmosphere.top_radius - atmosphere.bottom_radius * atmosphere.bottom_radius);

    // Distance to the horizon.
    float rho = SafeSqrt(r * r - atmosphere.bottom_radius * atmosphere.bottom_radius);
    float u_r = GetTextureCoordFromUnitRange(rho / H, SCATTERING_TEXTURE_R_SIZE);

    // Discriminant of the quadratic equation for the intersections of the ray
    // (r,mu) with the ground (see RayIntersectsGround).
    float r_mu = r * mu;
    float discriminant = r_mu * r_mu - r * r + atmosphere.bottom_radius * atmosphere.bottom_radius;

    float u_mu;
    if (ray_r_mu_intersects_ground)
    {
        // Distance to the ground for the ray (r,mu), and its minimum and maximum
        // values over all mu - obtained for (r,-1) and (r,mu_horizon).
        float d = -r_mu - SafeSqrt(discriminant);
        float d_min = r - atmosphere.bottom_radius;
        float d_max = rho;
        u_mu = 0.5 - 0.5 * GetTextureCoordFromUnitRange(d_max == d_min ? 0.0 : (d - d_min) / (d_max - d_min), SCATTERING_TEXTURE_MU_SIZE / 2);
    } 
    else 
    {
        // Distance to the top atmosphere boundary for the ray (r,mu), and its
        // minimum and maximum values over all mu - obtained for (r,1) and
        // (r,mu_horizon).
        float d = -r_mu + SafeSqrt(discriminant + H * H);
        float d_min = atmosphere.top_radius - r;
        float d_max = rho + H;
        u_mu = 0.5 + 0.5 * GetTextureCoordFromUnitRange((d - d_min) / (d_max - d_min), SCATTERING_TEXTURE_MU_SIZE / 2);
    }

    float d = DistanceToTopAtmosphereBoundary(
        atmosphere, atmosphere.bottom_radius, mu_s);
    float d_min = atmosphere.top_radius - atmosphere.bottom_radius;
    float d_max = H;
    float a = (d - d_min) / (d_max - d_min);
    float D = DistanceToTopAtmosphereBoundary(atmosphere, atmosphere.bottom_radius, atmosphere.mu_s_min);

    float A = (D - d_min) / (d_max - d_min);
    // An ad-hoc function equal to 0 for mu_s = mu_s_min (because then d = D and
    // thus a = A), equal to 1 for mu_s = 1 (because then d = d_min and thus
    // a = 0), and with a large slope around mu_s = 0, to get more texture 
    // samples near the horizon.
    float u_mu_s = GetTextureCoordFromUnitRange(max(1.0 - a / A, 0.0) / (1.0 + a), SCATTERING_TEXTURE_MU_S_SIZE);

    float u_nu = (nu + 1.0) / 2.0;
    return float4(u_nu, u_mu_s, u_mu, u_r);
}

void GetRMuMuSNuFromScatteringTextureUvwz(IN(AtmosphereParameters) atmosphere,
    IN(float4) uvwz, OUT(float) r, OUT(float) mu, OUT(float) mu_s,
    OUT(float) nu, OUT(bool) ray_r_mu_intersects_ground) 
{
    ATMOSPHERE_CHECK(uvwz.x >= 0.0 && uvwz.x <= 1.0);
    ATMOSPHERE_CHECK(uvwz.y >= 0.0 && uvwz.y <= 1.0);
    ATMOSPHERE_CHECK(uvwz.z >= 0.0 && uvwz.z <= 1.0);
    ATMOSPHERE_CHECK(uvwz.w >= 0.0 && uvwz.w <= 1.0);

    // Distance to top atmosphere boundary for a horizontal ray at ground level.
    float H = sqrt(atmosphere.top_radius * atmosphere.top_radius -
        atmosphere.bottom_radius * atmosphere.bottom_radius);
    // Distance to the horizon.
    float rho = H * GetUnitRangeFromTextureCoord(uvwz.w, SCATTERING_TEXTURE_R_SIZE);
    r = sqrt(rho * rho + atmosphere.bottom_radius * atmosphere.bottom_radius);

    if (uvwz.z < 0.5) 
    {
        // Distance to the ground for the ray (r,mu), and its minimum and maximum
        // values over all mu - obtained for (r,-1) and (r,mu_horizon) - from which
        // we can recover mu:
        float d_min = r - atmosphere.bottom_radius;
        float d_max = rho;
        float d = d_min + (d_max - d_min) * GetUnitRangeFromTextureCoord(
            1.0 - 2.0 * uvwz.z, SCATTERING_TEXTURE_MU_SIZE / 2);
        mu = (d == 0.0) ? float(-1.0) : ClampCosine(-(rho * rho + d * d) / (2.0 * r * d));
        ray_r_mu_intersects_ground = true;
    } 
    else 
    {
        // Distance to the top atmosphere boundary for the ray (r,mu), and its
        // minimum and maximum values over all mu - obtained for (r,1) and
        // (r,mu_horizon) - from which we can recover mu:
        float d_min = atmosphere.top_radius - r;
        float d_max = rho + H;
        float d = d_min + (d_max - d_min) * GetUnitRangeFromTextureCoord(
            2.0 * uvwz.z - 1.0, SCATTERING_TEXTURE_MU_SIZE / 2);
        mu = (d == 0.0) ? float(1.0) : ClampCosine((H * H - rho * rho - d * d) / (2.0 * r * d));
        ray_r_mu_intersects_ground = false;
    }

    float x_mu_s = GetUnitRangeFromTextureCoord(uvwz.y, SCATTERING_TEXTURE_MU_S_SIZE);
    float d_min = atmosphere.top_radius - atmosphere.bottom_radius;
    float d_max = H;
    float D = DistanceToTopAtmosphereBoundary(
        atmosphere, atmosphere.bottom_radius, atmosphere.mu_s_min);
    float A = (D - d_min) / (d_max - d_min);
    float a = (A - x_mu_s * A) / (1.0 + x_mu_s * A);
    float d = d_min + min(a, A) * (d_max - d_min);
    mu_s = (d == 0.0) ? float(1.0) : ClampCosine((H * H - d * d) / (2.0 * atmosphere.bottom_radius * d));

    nu = ClampCosine(uvwz.x * 2.0 - 1.0);
}

void GetRMuMuSNuFromScatteringTextureFragCoord(
    IN(AtmosphereParameters) atmosphere, IN(float3) frag_coord,
    OUT(float) r, OUT(float) mu, OUT(float) mu_s, OUT(float) nu,
    OUT(bool) ray_r_mu_intersects_ground) 
{
    const float4 SCATTERING_TEXTURE_SIZE = float4(
        SCATTERING_TEXTURE_NU_SIZE - 1,
        SCATTERING_TEXTURE_MU_S_SIZE,
        SCATTERING_TEXTURE_MU_SIZE,
        SCATTERING_TEXTURE_R_SIZE);
        
    float frag_coord_nu = floor(frag_coord.x / float(SCATTERING_TEXTURE_MU_S_SIZE));
    float frag_coord_mu_s = mod(frag_coord.x, float(SCATTERING_TEXTURE_MU_S_SIZE));
    float4 uvwz = float4(frag_coord_nu, frag_coord_mu_s, frag_coord.y, frag_coord.z) / SCATTERING_TEXTURE_SIZE;

    GetRMuMuSNuFromScatteringTextureUvwz(atmosphere, uvwz, r, mu, mu_s, nu, ray_r_mu_intersects_ground);

    // Clamp nu to its valid range of values, given mu and mu_s.
    nu = clamp(nu, mu * mu_s - sqrt((1.0 - mu * mu) * (1.0 - mu_s * mu_s)), mu * mu_s + sqrt((1.0 - mu * mu) * (1.0 - mu_s * mu_s)));
}

/*
<p>With this mapping, we can finally write a function to precompute a texel of
the single scattering in a 3D texture:
*/

void ComputeSingleScatteringTexture(IN(AtmosphereParameters) atmosphere,
    IN(Texture2D<float4>) transmittance_texture, IN(float3) frag_coord,
    OUT(float3) rayleigh, OUT(float3) mie) 
{
    float r;
    float mu;
    float mu_s;
    float nu;
    bool ray_r_mu_intersects_ground;
    GetRMuMuSNuFromScatteringTextureFragCoord(atmosphere, frag_coord, r, mu, mu_s, nu, ray_r_mu_intersects_ground);
    ComputeSingleScattering(atmosphere, transmittance_texture, r, mu, mu_s, nu, ray_r_mu_intersects_ground, rayleigh, mie);
}

/*
<h4 id="single_scattering_lookup">Lookup</h4>

<p>With the help of the above precomputed texture, we can now get the scattering
between a inPoint and the nearest atmosphere boundary with two texture lookups (we
need two 3D texture lookups to emulate a single 4D texture lookup with
quadrilinear interpolation; the 3D texture coordinates are computed using the
inverse of the 3D-4D mapping defined in
<code>GetRMuMuSNuFromScatteringTextureFragCoord</code>):
*/
float3 GetScattering(
    IN(AtmosphereParameters) atmosphere,
    Texture3D<float4> scattering_texture,
    float r, float mu, float mu_s, float nu,
    bool ray_r_mu_intersects_ground) 
{
  float4 uvwz = GetScatteringTextureUvwzFromRMuMuSNu(
      atmosphere, r, mu, mu_s, nu, ray_r_mu_intersects_ground);
  float tex_coord_x = uvwz.x * float(SCATTERING_TEXTURE_NU_SIZE - 1);
  float tex_x = floor(tex_coord_x);
  float lerp = tex_coord_x - tex_x;
    
  float3 uvw0 = float3((tex_x + uvwz.y) / float(SCATTERING_TEXTURE_NU_SIZE), uvwz.z, uvwz.w);
  float3 uvw1 = float3((tex_x + 1.0 + uvwz.y) / float(SCATTERING_TEXTURE_NU_SIZE), uvwz.z, uvwz.w);

      

  return float3(scattering_texture.SampleLevel(ATMOSPHERE_LINEAR_SAMPLER, uvw0, 0).xyz * (1.0 - lerp) +
      scattering_texture.SampleLevel(ATMOSPHERE_LINEAR_SAMPLER, uvw1, 0).xyz * lerp);
}

/*
<p>Finally, we provide here a convenience lookup function which will be useful
in the next section. This function returns either the single scattering, with
the phase functions included, or the $n$-th order of scattering, with $n>1$. It
assumes that, if <code>scattering_order</code> is strictly greater than 1, then
<code>multiple_scattering_texture</code> corresponds to this scattering order,
with both Rayleigh and Mie included, as well as all the phase function terms.
*/

float3 GetScattering(
    IN(AtmosphereParameters) atmosphere,
    IN(Texture3D<float4>) single_rayleigh_scattering_texture,
    IN(Texture3D<float4>) single_mie_scattering_texture,
    IN(Texture3D<float4>) multiple_scattering_texture,
    float r, float mu, float mu_s, float nu,
    bool ray_r_mu_intersects_ground,
    int scattering_order) 
{
    if (scattering_order == 1) 
    {
        float3 rayleigh = GetScattering(atmosphere, single_rayleigh_scattering_texture, r, mu, mu_s, nu, ray_r_mu_intersects_ground);
        float3 mie = GetScattering(atmosphere, single_mie_scattering_texture, r, mu, mu_s, nu, ray_r_mu_intersects_ground);

        return rayleigh * rayleighPhase(nu) + mie * cornetteShanksMiePhase(atmosphere.mie_phase_function_g, nu);
    } 
    else 
    {
        return GetScattering(atmosphere, multiple_scattering_texture, r, mu, mu_s, nu, ray_r_mu_intersects_ground);
    }
}

float3 GetIrradiance(
    IN(AtmosphereParameters) atmosphere,
    IN(Texture2D<float4>) irradiance_texture,
    float r, float mu_s);

float3 ComputeScatteringDensity(
    IN(AtmosphereParameters) atmosphere,
    IN(Texture2D<float4>) transmittance_texture,
    IN(Texture3D<float4>) single_rayleigh_scattering_texture,
    IN(Texture3D<float4>) single_mie_scattering_texture,
    IN(Texture3D<float4>) multiple_scattering_texture,
    IN(Texture2D<float4>) irradiance_texture,
    float r, float mu, float mu_s, float nu, int scattering_order) 
{
    ATMOSPHERE_CHECK(r >= atmosphere.bottom_radius && r <= atmosphere.top_radius);
    ATMOSPHERE_CHECK(mu >= -1.0 && mu <= 1.0);
    ATMOSPHERE_CHECK(mu_s >= -1.0 && mu_s <= 1.0);
    ATMOSPHERE_CHECK(nu >= -1.0 && nu <= 1.0);
    ATMOSPHERE_CHECK(scattering_order >= 2);

    // Compute unit direction vectors for the zenith, the view direction omega and
    // and the sun direction omega_s, such that the cosine of the view-zenith
    // angle is mu, the cosine of the sun-zenith angle is mu_s, and the cosine of
    // the view-sun angle is nu. The goal is to simplify computations below.
    float3 zenith_direction = float3(0.0, 0.0, 1.0);
    float3 omega = float3(sqrt(1.0 - mu * mu), 0.0, mu);
    float sun_dir_x = omega.x == 0.0 ? 0.0 : (nu - mu * mu_s) / omega.x;
    float sun_dir_y = sqrt(max(1.0 - sun_dir_x * sun_dir_x - mu_s * mu_s, 0.0));
    float3 omega_s = float3(sun_dir_x, sun_dir_y, mu_s);

    const int SAMPLE_COUNT = 16;
    const float dphi = kPI / float(SAMPLE_COUNT);
    const float dtheta = kPI / float(SAMPLE_COUNT);
    float3 rayleigh_mie = 0.0;

    // Nested loops for the integral over all the incident directions omega_i.
    for (int l = 0; l < SAMPLE_COUNT; ++l) 
    {
        float theta = (float(l) + 0.5) * dtheta;
        float cos_theta = cos(theta);
        float sin_theta = sin(theta);
        bool ray_r_theta_intersects_ground = RayIntersectsGround(atmosphere, r, cos_theta);

        // The distance and transmittance to the ground only depend on theta, so we
        // can compute them in the outer loop for efficiency.
        float distance_to_ground = 0.0;
        float3 transmittance_to_ground = (0.0);
        float3 ground_albedo = (0.0);
        if (ray_r_theta_intersects_ground)
        {
            distance_to_ground = DistanceToBottomAtmosphereBoundary(atmosphere, r, cos_theta);
            transmittance_to_ground = GetTransmittance(atmosphere, transmittance_texture, r, cos_theta, distance_to_ground, true /* ray_intersects_ground */);
            ground_albedo = atmosphere.ground_albedo;
        }

        for (int m = 0; m < 2 * SAMPLE_COUNT; ++m) 
        {
            float phi = (float(m) + 0.5) * dphi;
            float3 omega_i = float3(cos(phi) * sin_theta, sin(phi) * sin_theta, cos_theta);
            float domega_i = dtheta * dphi * sin(theta);

            // The radiance L_i arriving from direction omega_i after n-1 bounces is
            // the sum of a term given by the precomputed scattering texture for the
            // (n-1)-th order:
            float nu1 = dot(omega_s, omega_i);
            float3 incident_radiance = GetScattering(atmosphere,
                single_rayleigh_scattering_texture, single_mie_scattering_texture,
                multiple_scattering_texture, r, omega_i.z, mu_s, nu1,
                ray_r_theta_intersects_ground, scattering_order - 1);

            // and of the contribution from the light paths with n-1 bounces and whose
            // last bounce is on the ground. This contribution is the product of the
            // transmittance to the ground, the ground albedo, the ground BRDF, and
            // the irradiance received on the ground after n-2 bounces.
            float3 ground_normal = normalize(zenith_direction * r + omega_i * distance_to_ground);

            float3 ground_irradiance = GetIrradiance(atmosphere, irradiance_texture, atmosphere.bottom_radius, dot(ground_normal, omega_s));
            incident_radiance += transmittance_to_ground * ground_albedo * kInvertPI * ground_irradiance;

            // The radiance finally scattered from direction omega_i towards direction
            // -omega is the product of the incident radiance, the scattering
            // coefficient, and the phase function for directions omega and omega_i
            // (all this summed over all particle types, i.e. Rayleigh and Mie).
            float nu2 = dot(omega, omega_i);
            float rayleigh_density = GetProfileDensity(atmosphere.rayleigh_density, r - atmosphere.bottom_radius);

            float mie_density = GetProfileDensity(atmosphere.mie_density, r - atmosphere.bottom_radius);
            rayleigh_mie += incident_radiance * (atmosphere.rayleigh_scattering * rayleigh_density * rayleighPhase(nu2) + atmosphere.mie_scattering * mie_density * cornetteShanksMiePhase(atmosphere.mie_phase_function_g, nu2)) * domega_i;
        }
    }
    
    return rayleigh_mie;
}

float3 ComputeMultipleScattering(
    IN(AtmosphereParameters) atmosphere,
    IN(Texture2D<float4>) transmittance_texture,
    IN(Texture3D<float4>) scattering_density_texture,
    float r, float mu, float mu_s, float nu,
    bool ray_r_mu_intersects_ground) 
{
    ATMOSPHERE_CHECK(r >= atmosphere.bottom_radius && r <= atmosphere.top_radius);
    ATMOSPHERE_CHECK(mu >= -1.0 && mu <= 1.0);
    ATMOSPHERE_CHECK(mu_s >= -1.0 && mu_s <= 1.0);
    ATMOSPHERE_CHECK(nu >= -1.0 && nu <= 1.0);

    // float of intervals for the numerical integration.
    const int SAMPLE_COUNT = 50;
    // The integration step, i.e. the length of each integration interval.
    float dx = DistanceToNearestAtmosphereBoundary(atmosphere, r, mu, ray_r_mu_intersects_ground) / float(SAMPLE_COUNT);

    // Integration loop.
    float3 rayleigh_mie_sum = 0.0;
    for (int i = 0; i <= SAMPLE_COUNT; ++i) 
    {
        float d_i = float(i) * dx;

        // The r, mu and mu_s parameters at the current integration inPoint (see the
        // single scattering section for a detailed explanation).
        float r_i = ClampRadius(atmosphere, sqrt(d_i * d_i + 2.0 * r * mu * d_i + r * r));
        float mu_i = ClampCosine((r * mu + d_i) / r_i);
        float mu_s_i = ClampCosine((r * mu_s + d_i * nu) / r_i);

        // The Rayleigh and Mie multiple scattering at the current sample inPoint.
        float3 rayleigh_mie_i =
            GetScattering(atmosphere, scattering_density_texture, r_i, mu_i, mu_s_i, nu, ray_r_mu_intersects_ground) *
            GetTransmittance(atmosphere, transmittance_texture, r, mu, d_i, ray_r_mu_intersects_ground) * dx;

        // Sample weight (from the trapezoidal rule).
        float weight_i = (i == 0 || i == SAMPLE_COUNT) ? 0.5 : 1.0;
        rayleigh_mie_sum += rayleigh_mie_i * weight_i;
    }
    return rayleigh_mie_sum;
}

float3 ComputeScatteringDensityTexture(
    IN(AtmosphereParameters) atmosphere,
    IN(Texture2D<float4>) transmittance_texture,
    IN(Texture3D<float4>) single_rayleigh_scattering_texture,
    IN(Texture3D<float4>) single_mie_scattering_texture,
    IN(Texture3D<float4>) multiple_scattering_texture,
    IN(Texture2D<float4>) irradiance_texture,
    IN(float3) frag_coord, int scattering_order) 
{
    float r;
    float mu;
    float mu_s;
    float nu;
    bool ray_r_mu_intersects_ground;
    GetRMuMuSNuFromScatteringTextureFragCoord(atmosphere, frag_coord, r, mu, mu_s, nu, ray_r_mu_intersects_ground);

    return ComputeScatteringDensity(atmosphere, transmittance_texture,
        single_rayleigh_scattering_texture, single_mie_scattering_texture,
        multiple_scattering_texture, irradiance_texture, r, mu, mu_s, nu,
        scattering_order);
}

float3 ComputeMultipleScatteringTexture(
    IN(AtmosphereParameters) atmosphere,
    IN(Texture2D<float4>) transmittance_texture,
    IN(Texture3D<float4>) scattering_density_texture,
    IN(float3) frag_coord, OUT(float) nu)
{
    float r;
    float mu;
    float mu_s;
    bool ray_r_mu_intersects_ground;
    GetRMuMuSNuFromScatteringTextureFragCoord(atmosphere, frag_coord, r, mu, mu_s, nu, ray_r_mu_intersects_ground);

    return ComputeMultipleScattering(atmosphere, transmittance_texture, scattering_density_texture, r, mu, mu_s, nu, ray_r_mu_intersects_ground);
}

float3 ComputeDirectIrradiance(
    in const AtmosphereParameters atmosphere,
    in const Texture2D<float4> transmittance_texture,
    float r, 
    float mu_s) 
{
    ATMOSPHERE_CHECK(r >= atmosphere.bottom_radius && r <= atmosphere.top_radius);
    ATMOSPHERE_CHECK(mu_s >= -1.0 && mu_s <= 1.0);

    float alpha_s = atmosphere.sun_angular_radius;

    // Approximate average of the cosine factor mu_s over the visible fraction of
    // the Sun disc.
    float average_cosine_factor = mu_s < -alpha_s ? 0.0 : (mu_s > alpha_s ? mu_s : (mu_s + alpha_s) * (mu_s + alpha_s) / (4.0 * alpha_s));

    return atmosphere.solar_irradiance * GetTransmittanceToTopAtmosphereBoundary(atmosphere, transmittance_texture, r, mu_s) * average_cosine_factor;
}

float3 ComputeIndirectIrradiance(
    IN(AtmosphereParameters) atmosphere,
    IN(Texture3D<float4>) single_rayleigh_scattering_texture,
    IN(Texture3D<float4>) single_mie_scattering_texture,
    IN(Texture3D<float4>) multiple_scattering_texture,
    float r, 
    float mu_s, 
    int scattering_order) 
{
    ATMOSPHERE_CHECK(r >= atmosphere.bottom_radius && r <= atmosphere.top_radius);
    ATMOSPHERE_CHECK(mu_s >= -1.0 && mu_s <= 1.0);
    ATMOSPHERE_CHECK(scattering_order >= 1);

    const int SAMPLE_COUNT = 32;
    const float dphi = kPI / float(SAMPLE_COUNT);
    const float dtheta = kPI / float(SAMPLE_COUNT);

    float3 result = 0.0;
    float3 omega_s = float3(sqrt(1.0 - mu_s * mu_s), 0.0, mu_s);

    for (int j = 0; j < SAMPLE_COUNT / 2; ++j) 
    {
        float theta = (float(j) + 0.5) * dtheta;
        for (int i = 0; i < 2 * SAMPLE_COUNT; ++i) 
        {
            float phi = (float(i) + 0.5) * dphi;
            float3 omega = float3(cos(phi) * sin(theta), sin(phi) * sin(theta), cos(theta));
            float domega = dtheta * dphi * sin(theta);

            float nu = dot(omega, omega_s);
            result += GetScattering(atmosphere, single_rayleigh_scattering_texture, single_mie_scattering_texture, multiple_scattering_texture, r, omega.z, mu_s, nu, false /* ray_r_theta_intersects_ground */, scattering_order) * omega.z * domega;
        }
    }
    return result;
}

// Irradiance precomputation
float2 GetIrradianceTextureUvFromRMuS(IN(AtmosphereParameters) atmosphere, float r, float mu_s) 
{
    ATMOSPHERE_CHECK(r >= atmosphere.bottom_radius && r <= atmosphere.top_radius);
    ATMOSPHERE_CHECK(mu_s >= -1.0 && mu_s <= 1.0);

    float x_r = (r - atmosphere.bottom_radius) / (atmosphere.top_radius - atmosphere.bottom_radius);
    float x_mu_s = mu_s * 0.5 + 0.5;

    return float2(GetTextureCoordFromUnitRange(x_mu_s, IRRADIANCE_TEXTURE_WIDTH), GetTextureCoordFromUnitRange(x_r, IRRADIANCE_TEXTURE_HEIGHT));
}

void GetRMuSFromIrradianceTextureUv(IN(AtmosphereParameters) atmosphere, IN(float2) uv, OUT(float) r, OUT(float) mu_s) 
{
    ATMOSPHERE_CHECK(uv.x >= 0.0 && uv.x <= 1.0);
    ATMOSPHERE_CHECK(uv.y >= 0.0 && uv.y <= 1.0);
    float x_mu_s = GetUnitRangeFromTextureCoord(uv.x, IRRADIANCE_TEXTURE_WIDTH);
    float x_r = GetUnitRangeFromTextureCoord(uv.y, IRRADIANCE_TEXTURE_HEIGHT);
    r = atmosphere.bottom_radius + x_r * (atmosphere.top_radius - atmosphere.bottom_radius);
    mu_s = ClampCosine(2.0 * x_mu_s - 1.0);
}

static const float2 IRRADIANCE_TEXTURE_SIZE = float2(IRRADIANCE_TEXTURE_WIDTH, IRRADIANCE_TEXTURE_HEIGHT);

float3 ComputeDirectIrradianceTexture(
    in const AtmosphereParameters atmosphere,
    in const Texture2D<float4> transmittance_texture,
    in const float2 frag_coord) 
{
    float r;
    float mu_s;
    GetRMuSFromIrradianceTextureUv(atmosphere, frag_coord / IRRADIANCE_TEXTURE_SIZE, r, mu_s);
    return ComputeDirectIrradiance(atmosphere, transmittance_texture, r, mu_s);
}

float3 ComputeIndirectIrradianceTexture(
    IN(AtmosphereParameters) atmosphere,
    IN(Texture3D<float4>) single_rayleigh_scattering_texture,
    IN(Texture3D<float4>) single_mie_scattering_texture,
    IN(Texture3D<float4>) multiple_scattering_texture,
    IN(float2) frag_coord, int scattering_order) 
{
    float r;
    float mu_s;
    GetRMuSFromIrradianceTextureUv(atmosphere, frag_coord / IRRADIANCE_TEXTURE_SIZE, r, mu_s);
    return ComputeIndirectIrradiance(atmosphere, single_rayleigh_scattering_texture, single_mie_scattering_texture, multiple_scattering_texture, r, mu_s, scattering_order);
}

float3 GetIrradiance(
    in const AtmosphereParameters atmosphere,
    Texture2D<float4> irradiance_texture,
    float r, 
    float mu_s) 
{
    float2 uv = GetIrradianceTextureUvFromRMuS(atmosphere, r, mu_s);
    return float3(irradiance_texture.SampleLevel(ATMOSPHERE_LINEAR_SAMPLER, uv, 0).xyz);
}

float3 GetExtrapolatedSingleMieScattering(IN(AtmosphereParameters) atmosphere, IN(float4) scattering) 
{
    // Algebraically this can never be negative, but rounding errors can produce
    // that effect for sufficiently short view rays.
    if (scattering.r <= 0.0) 
    {
        return 0.0;
    }

    return scattering.rgb * scattering.a / scattering.r * (atmosphere.rayleigh_scattering.r / atmosphere.mie_scattering.r) * (atmosphere.mie_scattering / atmosphere.rayleigh_scattering);
}

float3 GetCombinedScattering(
    IN(AtmosphereParameters) atmosphere,
    IN(Texture3D<float4>) scattering_texture,
    IN(Texture3D<float4>) single_mie_scattering_texture,
    float r, float mu, float mu_s, float nu,
    bool ray_r_mu_intersects_ground,
    OUT(float3) single_mie_scattering) 
{
    float4 uvwz = GetScatteringTextureUvwzFromRMuMuSNu(atmosphere, r, mu, mu_s, nu, ray_r_mu_intersects_ground);

    float tex_coord_x = uvwz.x * float(SCATTERING_TEXTURE_NU_SIZE - 1);
    float tex_x = floor(tex_coord_x);
    float lerp = tex_coord_x - tex_x;
    float3 uvw0 = float3((tex_x + uvwz.y) / float(SCATTERING_TEXTURE_NU_SIZE), uvwz.z, uvwz.w);
    float3 uvw1 = float3((tex_x + 1.0 + uvwz.y) / float(SCATTERING_TEXTURE_NU_SIZE), uvwz.z, uvwz.w);

    float3 scattering;
    if(atmosphere.bCombineScattering != 0)
    {
        float4 combined_scattering = scattering_texture.SampleLevel(ATMOSPHERE_LINEAR_SAMPLER, uvw0, 0) * (1.0 - lerp) + scattering_texture.SampleLevel(ATMOSPHERE_LINEAR_SAMPLER, uvw1, 0) * lerp;
        scattering = combined_scattering.xyz;
        single_mie_scattering = GetExtrapolatedSingleMieScattering(atmosphere, combined_scattering);
    }
    else
    {
        scattering = float3(scattering_texture.SampleLevel(ATMOSPHERE_LINEAR_SAMPLER, uvw0, 0).xyz * (1.0 - lerp) + scattering_texture.SampleLevel(ATMOSPHERE_LINEAR_SAMPLER, uvw1, 0).xyz * lerp);
        single_mie_scattering = float3(single_mie_scattering_texture.SampleLevel(ATMOSPHERE_LINEAR_SAMPLER, uvw0, 0).xyz * (1.0 - lerp) + single_mie_scattering_texture.SampleLevel(ATMOSPHERE_LINEAR_SAMPLER, uvw1, 0).xyz * lerp);
    }

    return scattering;
}

float3 GetSkyRadiance(
    IN(AtmosphereParameters) atmosphere,
    IN(Texture2D<float4>) transmittance_texture,
    IN(Texture3D<float4>) scattering_texture,
    IN(Texture3D<float4>) single_mie_scattering_texture,
    float3 camera, 
    IN(float3) view_ray, 
    IN(float3) sun_direction, 
    OUT(float3) transmittance) 
{
    // Compute the distance to the top atmosphere boundary along the view ray,
    // assuming the viewer is in space (or NaN if the view ray does not intersect
    // the atmosphere).
    float r = length(camera);
    float rmu = dot(camera, view_ray);

    float distance_to_top_atmosphere_boundary = -rmu - sqrt(rmu * rmu - r * r + atmosphere.top_radius * atmosphere.top_radius);
    // If the viewer is in space and the view ray intersects the atmosphere, move
    // the viewer to the top atmosphere boundary (along the view ray):
    if (distance_to_top_atmosphere_boundary > 0.0) 
    {
        camera = camera + view_ray * distance_to_top_atmosphere_boundary;
        r = atmosphere.top_radius;
        rmu += distance_to_top_atmosphere_boundary;
    } 
    else if (r > atmosphere.top_radius) 
    {
        // If the view ray does not intersect the atmosphere, simply return 0.
        transmittance = 1.0;
        return 0.0;
    }

    // Compute the r, mu, mu_s and nu parameters needed for the texture lookups.
    float mu = rmu / r;

    float mu_s = dot(camera, sun_direction) / r;
    float nu = dot(view_ray, sun_direction);
    bool ray_r_mu_intersects_ground = RayIntersectsGround(atmosphere, r, mu);

    transmittance = ray_r_mu_intersects_ground ? (0.0) : GetTransmittanceToTopAtmosphereBoundary(atmosphere, transmittance_texture, r, mu);

    float3 single_mie_scattering;
    float3 scattering = GetCombinedScattering(atmosphere, scattering_texture, single_mie_scattering_texture, r, mu, mu_s, nu, ray_r_mu_intersects_ground, single_mie_scattering);

    const float3 skySpectralRadianceToLumiance = (atmosphere.luminanceMode != LUMINANCE_MODE_NONE) ? atmosphere.skySpectralRadianceToLumiance : 1.0;
    const float3 skyScattering = scattering * rayleighPhase(nu) + single_mie_scattering * cornetteShanksMiePhase(atmosphere.mie_phase_function_g, nu);

    const float3 skyLumianceSRGB = skyScattering * skySpectralRadianceToLumiance;
    return mul(sRGB_2_AP1, skyLumianceSRGB); // SRGB -> ACEScg
}

float3 GetSkyRadianceToPoint(
    in const AtmosphereParameters atmosphere,
    in const Texture2D<float4> transmittance_texture,
    in const Texture3D<float4> scattering_texture,
    in const Texture3D<float4> single_mie_scattering_texture,
    float3 camera,
    float3 inPoint, 
    float3 sun_direction, 
    out float3 transmittance) 
{
    // Compute the distance to the top atmosphere boundary along the view ray,
    // assuming the viewer is in space (or NaN if the view ray does not intersect
    // the atmosphere).
    float3 view_ray = normalize(inPoint - camera);
    float r = length(camera);
    float rmu = dot(camera, view_ray);
    float distance_to_top_atmosphere_boundary = -rmu - sqrt(rmu * rmu - r * r + atmosphere.top_radius * atmosphere.top_radius);
    
    // If the viewer is in space and the view ray intersects the atmosphere, move
    // the viewer to the top atmosphere boundary (along the view ray):
    if (distance_to_top_atmosphere_boundary > 0.0) 
    {
        camera = camera + view_ray * distance_to_top_atmosphere_boundary;
        r = atmosphere.top_radius;
        rmu += distance_to_top_atmosphere_boundary;
    }

    // Compute the r, mu, mu_s and nu parameters for the first texture lookup.
    float mu = rmu / r;
    float mu_s = dot(camera, sun_direction) / r;
    float nu = dot(view_ray, sun_direction);
    float d = length(inPoint - camera);
    bool ray_r_mu_intersects_ground = RayIntersectsGround(atmosphere, r, mu);

    transmittance = GetTransmittance(atmosphere, transmittance_texture, r, mu, d, ray_r_mu_intersects_ground);

    float3 single_mie_scattering;
    float3 scattering = GetCombinedScattering(atmosphere, scattering_texture, single_mie_scattering_texture, r, mu, mu_s, nu, ray_r_mu_intersects_ground, single_mie_scattering);

    float r_p = ClampRadius(atmosphere, sqrt(d * d + 2.0 * r * mu * d + r * r));
    float mu_p = (r * mu + d) / r_p;
    float mu_s_p = (r * mu_s + d * nu) / r_p;

    float3 single_mie_scattering_p;
    float3 scattering_p = GetCombinedScattering(atmosphere, scattering_texture, single_mie_scattering_texture, r_p, mu_p, mu_s_p, nu, ray_r_mu_intersects_ground, single_mie_scattering_p);

    scattering = scattering - transmittance * scattering_p;
    single_mie_scattering = single_mie_scattering - transmittance * single_mie_scattering_p;

    if (atmosphere.bCombineScattering != 0)
    {
        single_mie_scattering = GetExtrapolatedSingleMieScattering(atmosphere, float4(scattering, single_mie_scattering.r));
    }

    // Hack to avoid rendering artifacts when the sun is below the horizon.
    single_mie_scattering = single_mie_scattering * smoothstep(float(0.0), float(0.01), mu_s);

    const float3 skySpectralRadianceToLumiance = atmosphere.luminanceMode != LUMINANCE_MODE_NONE ? atmosphere.skySpectralRadianceToLumiance : 1.0;
    const float3 skyScattering = scattering * rayleighPhase(nu) + single_mie_scattering * cornetteShanksMiePhase(atmosphere.mie_phase_function_g, nu);

    const float3 skyLumianceSRGB = skyScattering * skySpectralRadianceToLumiance; 
    return mul(sRGB_2_AP1, skyLumianceSRGB); // SRGB -> ACEScg
}

float3 GetSunAndSkyIrradiance(
    in const AtmosphereParameters atmosphere,
    in const Texture2D<float4> transmittance_texture,
    in const Texture2D<float4> irradiance_texture,
    float3 targetPoint, 
    float3 normal, 
    float3 sunDirection,
    out float3 skyIrradiance) 
{
    float r = length(targetPoint);
    float mu_s = dot(targetPoint, sunDirection) / r;

    const float3 skySpectralRadianceToLumiance = atmosphere.luminanceMode != LUMINANCE_MODE_NONE ? atmosphere.skySpectralRadianceToLumiance : 1.0;

    // Indirect irradiance (approximated if the surface is not horizontal).
    skyIrradiance  = GetIrradiance(atmosphere, irradiance_texture, r, mu_s) * (1.0 + dot(normal, targetPoint) / r) * 0.5;
    skyIrradiance *= skySpectralRadianceToLumiance;
    skyIrradiance  = mul(sRGB_2_AP1, skyIrradiance); // SRGB -> ACEScg

    const float3 sunSpectralRadianceToLumiance = atmosphere.luminanceMode != LUMINANCE_MODE_NONE ? atmosphere.sunSpectralRadianceToLumiance : 1.0;
    const float3 sunRadiance = atmosphere.solar_irradiance * GetTransmittanceToSun(atmosphere, transmittance_texture, r, mu_s) * max(dot(normal, sunDirection), 0.0);
    
    // Direct irradiance.
    float3 sunLumianceSRGB = sunSpectralRadianceToLumiance * sunRadiance;
    return mul(sRGB_2_AP1, sunLumianceSRGB); // SRGB -> ACEScg
}

#endif