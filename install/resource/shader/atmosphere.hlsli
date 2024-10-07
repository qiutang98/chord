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

/*<h2>atmosphere/definitions.glsl</h2>

<p>This GLSL file defines the physical types and constants which are used in the
main <a href="functions.glsl.html">functions</a> of our atmosphere model, in
such a way that they can be compiled by a GLSL compiler (a
<a href="reference/definitions.h.html">C++ equivalent</a> of this file
provides the same types and constants in C++, to allow the same functions to be
compiled by a C++ compiler - see the <a href="../index.html">Introduction</a>).

<h3>Physical quantities</h3>

<p>The physical quantities we need for our atmosphere model are
<a href="https://en.wikipedia.org/wiki/Radiometry">radiometric</a> and
<a href="https://en.wikipedia.org/wiki/Photometry_(optics)">photometric</a>
quantities. In GLSL we can't define custom numeric types to enforce the
homogeneity of expressions at compile time, so we define all the physical
quantities as <code>float</code>, with preprocessor macros (there is no
<code>typedef</code> in GLSL).

<p>We start with six base quantities: length, wavelength, angle, solid angle,
power and luminous power (wavelength is also a length, but we distinguish the
two for increased clarity).
*/

#define Length float
#define Wavelength float
#define Angle float
#define SolidAngle float
#define Power float
#define LuminousPower float

/*
<p>From this we "derive" the irradiance, radiance, spectral irradiance,
spectral radiance, luminance, etc, as well pure numbers, area, volume, etc (the
actual derivation is done in the <a href="reference/definitions.h.html">C++
equivalent</a> of this file).
*/

#define Number float
#define InverseLength float

#define Luminance float
#define Illuminance float
#define Area float
#define Volume float
#define NumberDensity float
#define Irradiance float
#define Radiance float
#define SpectralPower float
#define SpectralIrradiance float
#define SpectralRadiance float
#define SpectralRadianceDensity float
#define ScatteringCoefficient float
#define InverseSolidAngle float
#define LuminousIntensity float

// A generic function from Wavelength to some other type.
#define AbstractSpectrum float3
// A function from Wavelength to Number.
#define DimensionlessSpectrum float3
// A function from Wavelength to SpectralPower.
#define PowerSpectrum float3
// A function from Wavelength to SpectralIrradiance.
#define IrradianceSpectrum float3
// A function from Wavelength to SpectralRadiance.
#define RadianceSpectrum float3
// A function from Wavelength to SpectralRadianceDensity.
#define RadianceDensitySpectrum float3
// A function from Wavelength to ScaterringCoefficient.
#define ScatteringSpectrum float3

// A vector of 3 luminance values.
#define Luminance3 float3
// A vector of 3 illuminance values.
#define Illuminance3 float3

/*
<p>Finally, we also need precomputed textures containing physical quantities in
each texel. Since we can't define custom sampler types to enforce the
homogeneity of expressions at compile time in GLSL, we define these texture
types as <code>sampler2D</code> and <code>sampler3D</code>, with preprocessor
macros. The full definitions are given in the
<a href="reference/definitions.h.html">C++ equivalent</a> of this file).
*/

#define TransmittanceTexture Texture2D<float4>
#define AbstractScatteringTexture Texture3D<float4>
#define ReducedScatteringTexture Texture3D<float4>
#define ScatteringTexture Texture3D<float4>
#define ScatteringDensityTexture Texture3D<float4>
#define IrradianceTexture Texture2D<float4>

/*
<h3>Physical units</h3>

<p>We can then define the units for our six base physical quantities:
meter (m), nanometer (nm), radian (rad), steradian (sr), watt (watt) and lumen
(lm):
*/

static const Length m = 1.0;
static const Wavelength nm = 1.0;
static const Angle rad = 1.0;
static const SolidAngle sr = 1.0;
static const Power watt = 1.0;
static const LuminousPower lm = 1.0;

/*
<p>From which we can derive the units for some derived physical quantities,
as well as some derived units (kilometer km, kilocandela kcd, degree deg):
*/

static const float PI = 3.14159265358979323846;

static const Length km = 1000.0 * m;
static const Area m2 = m * m;
static const Volume m3 = m * m * m;
static const Angle pi = PI * rad;
static const Angle deg = pi / 180.0;
static const Irradiance watt_per_square_meter = watt / m2;
static const Radiance watt_per_square_meter_per_sr = watt / (m2 * sr);
static const SpectralIrradiance watt_per_square_meter_per_nm = watt / (m2 * nm);
static const SpectralRadiance watt_per_square_meter_per_sr_per_nm = watt / (m2 * sr * nm);
static const SpectralRadianceDensity watt_per_cubic_meter_per_sr_per_nm = watt / (m3 * sr * nm);
static const LuminousIntensity cd = lm / sr;
static const LuminousIntensity kcd = 1000.0 * cd;
static const Luminance cd_per_square_meter = cd / m2;
static const Luminance kcd_per_square_meter = kcd / m2;

Number ClampCosine(Number mu)
{
    return clamp(mu, Number(-1.0), Number(1.0));
}

Length ClampDistance(Length d)
{
    return max(d, 0.0 * m);
}

Length ClampRadius(IN(AtmosphereParameters) atmosphere, Length r) 
{
    return clamp(r, atmosphere.bottom_radius, atmosphere.top_radius);
}

Length SafeSqrt(Area a) 
{
    return sqrt(max(a, 0.0 * m2));
}

Length DistanceToTopAtmosphereBoundary(IN(AtmosphereParameters) atmosphere, Length r, Number mu) 
{
    ATMOSPHERE_CHECK(r <= atmosphere.top_radius);
    ATMOSPHERE_CHECK(mu >= -1.0 && mu <= 1.0);

    Area discriminant = r * r * (mu * mu - 1.0) + atmosphere.top_radius * atmosphere.top_radius;
    return ClampDistance(-r * mu + SafeSqrt(discriminant));
}

Length DistanceToBottomAtmosphereBoundary(IN(AtmosphereParameters) atmosphere, Length r, Number mu) 
{
    ATMOSPHERE_CHECK(r >= atmosphere.bottom_radius);
    ATMOSPHERE_CHECK(mu >= -1.0 && mu <= 1.0);
    Area discriminant = r * r * (mu * mu - 1.0) + atmosphere.bottom_radius * atmosphere.bottom_radius;
    return ClampDistance(-r * mu - SafeSqrt(discriminant));
}

/*
<h5>Intersections with the ground</h5>

<p>The segment $[\bp,\bi]$ intersects the ground when
$d^2+2r\mu d+r^2=r_{\mathrm{bottom}}^2$ has a solution with $d \ge 0$. This
requires the discriminant $r^2(\mu^2-1)+r_{\mathrm{bottom}}^2$ to be positive,
from which we deduce the following function:
*/

bool RayIntersectsGround(IN(AtmosphereParameters) atmosphere, Length r, Number mu) 
{
    ATMOSPHERE_CHECK(r >= atmosphere.bottom_radius);
    ATMOSPHERE_CHECK(mu >= -1.0 && mu <= 1.0);
    return mu < 0.0 && r * r * (mu * mu - 1.0) + atmosphere.bottom_radius * atmosphere.bottom_radius >= 0.0 * m2;
}

/*
<h5>Transmittance to the top atmosphere boundary</h5>

<p>We can now compute the transmittance between $\bp$ and $\bi$. From its
definition and the
<a href="https://en.wikipedia.org/wiki/Beer-Lambert_law">Beer-Lambert law</a>,
this involves the integral of the number density of air molecules along the
segment $[\bp,\bi]$, as well as the integral of the number density of aerosols
and the integral of the number density of air molecules that absorb light
(e.g. ozone) - along the same segment. These 3 integrals have the same form and,
when the segment $[\bp,\bi]$ does not intersect the ground, they can be computed
numerically with the help of the following auxilliary function (using the <a
href="https://en.wikipedia.org/wiki/Trapezoidal_rule">trapezoidal rule</a>):
*/

Number GetLayerDensity(IN(DensityProfileLayer) layer, Length altitude) 
{
    Number density = layer.exp_term * exp(layer.exp_scale * altitude) + layer.linear_term * altitude + layer.constant_term;
    return clamp(density, Number(0.0), Number(1.0));
}

Number GetProfileDensity(IN(DensityProfile) profile, Length altitude) 
{
    return altitude < profile.layers[0].width 
        ? GetLayerDensity(profile.layers[0], altitude) 
        : GetLayerDensity(profile.layers[1], altitude);
}

Length ComputeOpticalLengthToTopAtmosphereBoundary(
    IN(AtmosphereParameters) atmosphere, IN(DensityProfile) profile,
    Length r, Number mu) 
{
    ATMOSPHERE_CHECK(r >= atmosphere.bottom_radius && r <= atmosphere.top_radius);
    ATMOSPHERE_CHECK(mu >= -1.0 && mu <= 1.0);
    // Number of intervals for the numerical integration.
    const int SAMPLE_COUNT = 500;
    // The integration step, i.e. the length of each integration interval.
    Length dx = DistanceToTopAtmosphereBoundary(atmosphere, r, mu) / Number(SAMPLE_COUNT);
    // Integration loop.
    Length result = 0.0 * m;
    for (int i = 0; i <= SAMPLE_COUNT; ++i) 
    {
        Length d_i = Number(i) * dx;
        // Distance between the current sample point_ and the planet center.
        Length r_i = sqrt(d_i * d_i + 2.0 * r * mu * d_i + r * r);
        // Number density at the current sample point_ (divided by the number density
        // at the bottom of the atmosphere, yielding a dimensionless number).
        Number y_i = GetProfileDensity(profile, r_i - atmosphere.bottom_radius);
        // Sample weight (from the trapezoidal rule).
        Number weight_i = i == 0 || i == SAMPLE_COUNT ? 0.5 : 1.0;
        result += y_i * weight_i * dx;
    }
    return result;
}

/*
<p>With this function the transmittance between $\bp$ and $\bi$ is now easy to
compute (we continue to assume that the segment does not intersect the ground):
*/

DimensionlessSpectrum ComputeTransmittanceToTopAtmosphereBoundary(IN(AtmosphereParameters) atmosphere, Length r, Number mu) 
{
    ATMOSPHERE_CHECK(r >= atmosphere.bottom_radius && r <= atmosphere.top_radius);
    ATMOSPHERE_CHECK(mu >= -1.0 && mu <= 1.0);

    return exp(-(
        atmosphere.rayleigh_scattering * ComputeOpticalLengthToTopAtmosphereBoundary(atmosphere, atmosphere.rayleigh_density, r, mu) +
        atmosphere.mie_extinction * ComputeOpticalLengthToTopAtmosphereBoundary(atmosphere, atmosphere.mie_density, r, mu) +
        atmosphere.absorption_extinction * ComputeOpticalLengthToTopAtmosphereBoundary(atmosphere, atmosphere.absorption_density, r, mu)));
}

/*
<h4 id="transmittance_precomputation">Precomputation</h4>

<p>The above function is quite costly to evaluate, and a lot of evaluations are
needed to compute single and multiple scattering. Fortunately this function
depends on only two parameters and is quite smooth, so we can precompute it in a
small 2D texture to optimize its evaluation.

<p>For this we need a mapping between the function parameters $(r,\mu)$ and the
texture coordinates $(u,v)$, and vice-versa, because these parameters do not
have the same units and range of values. And even if it was the case, storing a
function $f$ from the $[0,1]$ interval in a texture of size $n$ would sample the
function at $0.5/n$, $1.5/n$, ... $(n-0.5)/n$, because texture samples are at
the center of texels. Therefore, this texture would only give us extrapolated
function values at the domain boundaries ($0$ and $1$). To avoid this we need
to store $f(0)$ at the center of texel 0 and $f(1)$ at the center of texel
$n-1$. This can be done with the following mapping from values $x$ in $[0,1]$ to
texture coordinates $u$ in $[0.5/n,1-0.5/n]$ - and its inverse:
*/

Number GetTextureCoordFromUnitRange(Number x, int texture_size) 
{
    return 0.5 / Number(texture_size) + x * (1.0 - 1.0 / Number(texture_size));
}

Number GetUnitRangeFromTextureCoord(Number u, int texture_size) 
{
    return (u - 0.5 / Number(texture_size)) / (1.0 - 1.0 / Number(texture_size));
}

float2 GetTransmittanceTextureUvFromRMu(IN(AtmosphereParameters) atmosphere,
    Length r, Number mu) 
{
    ATMOSPHERE_CHECK(r >= atmosphere.bottom_radius && r <= atmosphere.top_radius);
    ATMOSPHERE_CHECK(mu >= -1.0 && mu <= 1.0);
    // Distance to top atmosphere boundary for a horizontal ray at ground level.
    Length H = sqrt(atmosphere.top_radius * atmosphere.top_radius -
        atmosphere.bottom_radius * atmosphere.bottom_radius);
    // Distance to the horizon.
    Length rho =
        SafeSqrt(r * r - atmosphere.bottom_radius * atmosphere.bottom_radius);
    // Distance to the top atmosphere boundary for the ray (r,mu), and its minimum
    // and maximum values over all mu - obtained for (r,1) and (r,mu_horizon).
    Length d = DistanceToTopAtmosphereBoundary(atmosphere, r, mu);
    Length d_min = atmosphere.top_radius - r;
    Length d_max = rho + H;
    Number x_mu = (d - d_min) / (d_max - d_min);
    Number x_r = rho / H;
    return float2(GetTextureCoordFromUnitRange(x_mu, TRANSMITTANCE_TEXTURE_WIDTH),
                GetTextureCoordFromUnitRange(x_r, TRANSMITTANCE_TEXTURE_HEIGHT));
}

/*
<p>and the inverse mapping follows immediately:
*/

void GetRMuFromTransmittanceTextureUv(IN(AtmosphereParameters) atmosphere,
    IN(float2) uv, OUT(Length) r, OUT(Number) mu) 
{
    ATMOSPHERE_CHECK(uv.x >= 0.0 && uv.x <= 1.0);
    ATMOSPHERE_CHECK(uv.y >= 0.0 && uv.y <= 1.0);
    Number x_mu = GetUnitRangeFromTextureCoord(uv.x, TRANSMITTANCE_TEXTURE_WIDTH);
    Number x_r = GetUnitRangeFromTextureCoord(uv.y, TRANSMITTANCE_TEXTURE_HEIGHT);
    // Distance to top atmosphere boundary for a horizontal ray at ground level.
    Length H = sqrt(atmosphere.top_radius * atmosphere.top_radius -
        atmosphere.bottom_radius * atmosphere.bottom_radius);
    // Distance to the horizon, from which we can compute r:
    Length rho = H * x_r;
    r = sqrt(rho * rho + atmosphere.bottom_radius * atmosphere.bottom_radius);
    // Distance to the top atmosphere boundary for the ray (r,mu), and its minimum
    // and maximum values over all mu - obtained for (r,1) and (r,mu_horizon) -
    // from which we can recover mu:
    Length d_min = atmosphere.top_radius - r;
    Length d_max = rho + H;
    Length d = d_min + x_mu * (d_max - d_min);
    mu = d == 0.0 * m ? Number(1.0) : (H * H - rho * rho - d * d) / (2.0 * r * d);
    mu = ClampCosine(mu);
}

/*
<p>It is now easy to define a fragment shader function to precompute a texel of
the transmittance texture:
*/

DimensionlessSpectrum ComputeTransmittanceToTopAtmosphereBoundaryTexture(
    IN(AtmosphereParameters) atmosphere, IN(float2) frag_coord) 
{
    const float2 TRANSMITTANCE_TEXTURE_SIZE = float2(TRANSMITTANCE_TEXTURE_WIDTH, TRANSMITTANCE_TEXTURE_HEIGHT);
    Length r;
    Number mu;
    GetRMuFromTransmittanceTextureUv(atmosphere, frag_coord / TRANSMITTANCE_TEXTURE_SIZE, r, mu);
    return ComputeTransmittanceToTopAtmosphereBoundary(atmosphere, r, mu);
}

/*
<h4 id="transmittance_lookup">Lookup</h4>

<p>With the help of the above precomputed texture, we can now get the
transmittance between a point_ and the top atmosphere boundary with a single
texture lookup (assuming there is no intersection with the ground):
*/

DimensionlessSpectrum GetTransmittanceToTopAtmosphereBoundary(
    IN(AtmosphereParameters) atmosphere,
    IN(TransmittanceTexture) transmittance_texture,
    Length r, Number mu) 
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

DimensionlessSpectrum GetTransmittance(
    IN(AtmosphereParameters) atmosphere,
    IN(TransmittanceTexture) transmittance_texture,
    Length r, Number mu, Length d, bool ray_r_mu_intersects_ground) 
{
    ATMOSPHERE_CHECK(r >= atmosphere.bottom_radius && r <= atmosphere.top_radius);
    ATMOSPHERE_CHECK(mu >= -1.0 && mu <= 1.0);
    ATMOSPHERE_CHECK(d >= 0.0 * m);

    Length r_d = ClampRadius(atmosphere, sqrt(d * d + 2.0 * r * mu * d + r * r));
    Number mu_d = ClampCosine((r * mu + d) / r_d);

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

/*
<p>where <code>ray_r_mu_intersects_ground</code> should be true iif the ray
defined by $r$ and $\mu$ intersects the ground. We don't compute it here with
<code>RayIntersectsGround</code> because the result could be wrong for rays
very close to the horizon, due to the finite precision and rounding errors of
floating point_ operations. And also because the caller generally has more robust
ways to know whether a ray intersects the ground or not (see below).

<p>Finally, we will also need the transmittance between a point_ in the
atmosphere and the Sun. The Sun is not a point_ light source, so this is an
integral of the transmittance over the Sun disc. Here we consider that the
transmittance is constant over this disc, except below the horizon, where the
transmittance is 0. As a consequence, the transmittance to the Sun can be
computed with <code>GetTransmittanceToTopAtmosphereBoundary</code>, times the
fraction of the Sun disc which is above the horizon.

<p>This fraction varies from 0 when the Sun zenith angle $\theta_s$ is larger
than the horizon zenith angle $\theta_h$ plus the Sun angular radius $\alpha_s$,
to 1 when $\theta_s$ is smaller than $\theta_h-\alpha_s$. Equivalently, it
varies from 0 when $\mu_s=\cos\theta_s$ is smaller than
$\cos(\theta_h+\alpha_s)\approx\cos\theta_h-\alpha_s\sin\theta_h$ to 1 when
$\mu_s$ is larger than
$\cos(\theta_h-\alpha_s)\approx\cos\theta_h+\alpha_s\sin\theta_h$. In between,
the visible Sun disc fraction varies approximately like a smoothstep (this can
be verified by plotting the area of <a
href="https://en.wikipedia.org/wiki/Circular_segment">circular segment</a> as a
function of its <a href="https://en.wikipedia.org/wiki/Sagitta_(geometry)"
>sagitta</a>). Therefore, since $\sin\theta_h=r_{\mathrm{bottom}}/r$, we can
approximate the transmittance to the Sun with the following function:
*/

DimensionlessSpectrum GetTransmittanceToSun(
    IN(AtmosphereParameters) atmosphere,
    IN(TransmittanceTexture) transmittance_texture,
    Length r, Number mu_s) 
{
    Number sin_theta_h = atmosphere.bottom_radius / r;
    Number cos_theta_h = -sqrt(max(1.0 - sin_theta_h * sin_theta_h, 0.0));
    return GetTransmittanceToTopAtmosphereBoundary(atmosphere, transmittance_texture, r, mu_s) *
        smoothstep(-sin_theta_h * atmosphere.sun_angular_radius / rad,
                    sin_theta_h * atmosphere.sun_angular_radius / rad,
                    mu_s - cos_theta_h);
}

void ComputeSingleScatteringIntegrand(
    IN(AtmosphereParameters) atmosphere,
    IN(TransmittanceTexture) transmittance_texture,
    Length r, Number mu, Number mu_s, Number nu, Length d,
    bool ray_r_mu_intersects_ground,
    OUT(DimensionlessSpectrum) rayleigh, OUT(DimensionlessSpectrum) mie) {
  Length r_d = ClampRadius(atmosphere, sqrt(d * d + 2.0 * r * mu * d + r * r));
  Number mu_s_d = ClampCosine((r * mu_s + d * nu) / r_d);
  DimensionlessSpectrum transmittance =
      GetTransmittance(
          atmosphere, transmittance_texture, r, mu, d,
          ray_r_mu_intersects_ground) *
      GetTransmittanceToSun(
          atmosphere, transmittance_texture, r_d, mu_s_d);
  rayleigh = transmittance * GetProfileDensity(
      atmosphere.rayleigh_density, r_d - atmosphere.bottom_radius);
  mie = transmittance * GetProfileDensity(
      atmosphere.mie_density, r_d - atmosphere.bottom_radius);
}

/*
<p>Consider now the Sun light arriving at $\bp$ from a given direction $\bw$,
after exactly one scattering event. The scattering event can occur at any point_
$\bq$ between $\bp$ and the intersection $\bi$ of the half-line $[\bp,\bw)$ with
the nearest atmosphere boundary. Thus, the single scattered radiance at $\bp$
from direction $\bw$ is the integral of the single scattered radiance from $\bq$
to $\bp$ for all points $\bq$ between $\bp$ and $\bi$. To compute it, we first
need the length $\Vert\bp\bi\Vert$:
*/

Length DistanceToNearestAtmosphereBoundary(IN(AtmosphereParameters) atmosphere,
    Length r, Number mu, bool ray_r_mu_intersects_ground) {
  if (ray_r_mu_intersects_ground) {
    return DistanceToBottomAtmosphereBoundary(atmosphere, r, mu);
  } else {
    return DistanceToTopAtmosphereBoundary(atmosphere, r, mu);
  }
}

/*
<p>The single scattering integral can then be computed as follows (using
the <a href="https://en.wikipedia.org/wiki/Trapezoidal_rule">trapezoidal
rule</a>):
*/

void ComputeSingleScattering(
    IN(AtmosphereParameters) atmosphere,
    IN(TransmittanceTexture) transmittance_texture,
    Length r, Number mu, Number mu_s, Number nu,
    bool ray_r_mu_intersects_ground,
    OUT(IrradianceSpectrum) rayleigh, OUT(IrradianceSpectrum) mie) {
  ATMOSPHERE_CHECK(r >= atmosphere.bottom_radius && r <= atmosphere.top_radius);
  ATMOSPHERE_CHECK(mu >= -1.0 && mu <= 1.0);
  ATMOSPHERE_CHECK(mu_s >= -1.0 && mu_s <= 1.0);
  ATMOSPHERE_CHECK(nu >= -1.0 && nu <= 1.0);

  // Number of intervals for the numerical integration.
  const int SAMPLE_COUNT = 50;
  // The integration step, i.e. the length of each integration interval.
  Length dx =
      DistanceToNearestAtmosphereBoundary(atmosphere, r, mu,
          ray_r_mu_intersects_ground) / Number(SAMPLE_COUNT);
  // Integration loop.
  DimensionlessSpectrum rayleigh_sum = (0.0);
  DimensionlessSpectrum mie_sum = (0.0);
  for (int i = 0; i <= SAMPLE_COUNT; ++i) {
    Length d_i = Number(i) * dx;
    // The Rayleigh and Mie single scattering at the current sample point_.
    DimensionlessSpectrum rayleigh_i;
    DimensionlessSpectrum mie_i;
    ComputeSingleScatteringIntegrand(atmosphere, transmittance_texture,
        r, mu, mu_s, nu, d_i, ray_r_mu_intersects_ground, rayleigh_i, mie_i);
    // Sample weight (from the trapezoidal rule).
    Number weight_i = (i == 0 || i == SAMPLE_COUNT) ? 0.5 : 1.0;
    rayleigh_sum += rayleigh_i * weight_i;
    mie_sum += mie_i * weight_i;
  }
  rayleigh = rayleigh_sum * dx * atmosphere.solar_irradiance *
      atmosphere.rayleigh_scattering;
  mie = mie_sum * dx * atmosphere.solar_irradiance * atmosphere.mie_scattering;
}

/*
<h4 id="single_scattering_precomputation">Precomputation</h4>

<p>The <code>ComputeSingleScattering</code> function is quite costly to
evaluate, and a lot of evaluations are needed to compute multiple scattering.
We therefore want to precompute it in a texture, which requires a mapping from
the 4 function parameters to texture coordinates. Assuming for now that we have
4D textures, we need to define a mapping from $(r,\mu,\mu_s,\nu)$ to texture
coordinates $(u,v,w,z)$. The function below implements the mapping defined in
our <a href="https://hal.inria.fr/inria-00288758/en">paper</a>, with some small
improvements (refer to the paper and to the above figures for the notations):
<ul>
<li>the mapping for $\mu$ takes the minimal distance to the nearest atmosphere
boundary into account, to map $\mu$ to the full $[0,1]$ interval (the original
mapping was not covering the full $[0,1]$ interval).</li>
<li>the mapping for $\mu_s$ is more generic than in the paper (the original
mapping was using ad-hoc constants chosen for the Earth atmosphere case). It is
based on the distance to the top atmosphere boundary (for the sun rays), as for
the $\mu$ mapping, and uses only one ad-hoc (but configurable) parameter. Yet,
as the original definition, it provides an increased sampling rate near the
horizon.</li>
</ul>
*/

float4 GetScatteringTextureUvwzFromRMuMuSNu(IN(AtmosphereParameters) atmosphere,
    Length r, Number mu, Number mu_s, Number nu,
    bool ray_r_mu_intersects_ground) 
{
    ATMOSPHERE_CHECK(r >= atmosphere.bottom_radius && r <= atmosphere.top_radius);
    ATMOSPHERE_CHECK(mu >= -1.0 && mu <= 1.0);
    ATMOSPHERE_CHECK(mu_s >= -1.0 && mu_s <= 1.0);
    ATMOSPHERE_CHECK(nu >= -1.0 && nu <= 1.0);

    // Distance to top atmosphere boundary for a horizontal ray at ground level.
    Length H = sqrt(atmosphere.top_radius * atmosphere.top_radius -
        atmosphere.bottom_radius * atmosphere.bottom_radius);
    // Distance to the horizon.
    Length rho =
        SafeSqrt(r * r - atmosphere.bottom_radius * atmosphere.bottom_radius);
    Number u_r = GetTextureCoordFromUnitRange(rho / H, SCATTERING_TEXTURE_R_SIZE);

    // Discriminant of the quadratic equation for the intersections of the ray
    // (r,mu) with the ground (see RayIntersectsGround).
    Length r_mu = r * mu;
    Area discriminant = r_mu * r_mu - r * r + atmosphere.bottom_radius * atmosphere.bottom_radius;

    Number u_mu;
    if (ray_r_mu_intersects_ground)
    {
        // Distance to the ground for the ray (r,mu), and its minimum and maximum
        // values over all mu - obtained for (r,-1) and (r,mu_horizon).
        Length d = -r_mu - SafeSqrt(discriminant);
        Length d_min = r - atmosphere.bottom_radius;
        Length d_max = rho;
        u_mu = 0.5 - 0.5 * GetTextureCoordFromUnitRange(d_max == d_min ? 0.0 :
            (d - d_min) / (d_max - d_min), SCATTERING_TEXTURE_MU_SIZE / 2);
    } 
    else 
    {
        // Distance to the top atmosphere boundary for the ray (r,mu), and its
        // minimum and maximum values over all mu - obtained for (r,1) and
        // (r,mu_horizon).
        Length d = -r_mu + SafeSqrt(discriminant + H * H);
        Length d_min = atmosphere.top_radius - r;
        Length d_max = rho + H;
        u_mu = 0.5 + 0.5 * GetTextureCoordFromUnitRange(
            (d - d_min) / (d_max - d_min), SCATTERING_TEXTURE_MU_SIZE / 2);
    }

    Length d = DistanceToTopAtmosphereBoundary(
        atmosphere, atmosphere.bottom_radius, mu_s);
    Length d_min = atmosphere.top_radius - atmosphere.bottom_radius;
    Length d_max = H;
    Number a = (d - d_min) / (d_max - d_min);
    Length D = DistanceToTopAtmosphereBoundary(
        atmosphere, atmosphere.bottom_radius, atmosphere.mu_s_min);
    Number A = (D - d_min) / (d_max - d_min);
    // An ad-hoc function equal to 0 for mu_s = mu_s_min (because then d = D and
    // thus a = A), equal to 1 for mu_s = 1 (because then d = d_min and thus
    // a = 0), and with a large slope around mu_s = 0, to get more texture 
    // samples near the horizon.
    Number u_mu_s = GetTextureCoordFromUnitRange(
        max(1.0 - a / A, 0.0) / (1.0 + a), SCATTERING_TEXTURE_MU_S_SIZE);

    Number u_nu = (nu + 1.0) / 2.0;
    return float4(u_nu, u_mu_s, u_mu, u_r);
}

/*
<p>The inverse mapping follows immediately:
*/

void GetRMuMuSNuFromScatteringTextureUvwz(IN(AtmosphereParameters) atmosphere,
    IN(float4) uvwz, OUT(Length) r, OUT(Number) mu, OUT(Number) mu_s,
    OUT(Number) nu, OUT(bool) ray_r_mu_intersects_ground) 
{
    ATMOSPHERE_CHECK(uvwz.x >= 0.0 && uvwz.x <= 1.0);
    ATMOSPHERE_CHECK(uvwz.y >= 0.0 && uvwz.y <= 1.0);
    ATMOSPHERE_CHECK(uvwz.z >= 0.0 && uvwz.z <= 1.0);
    ATMOSPHERE_CHECK(uvwz.w >= 0.0 && uvwz.w <= 1.0);

    // Distance to top atmosphere boundary for a horizontal ray at ground level.
    Length H = sqrt(atmosphere.top_radius * atmosphere.top_radius -
        atmosphere.bottom_radius * atmosphere.bottom_radius);
    // Distance to the horizon.
    Length rho =
        H * GetUnitRangeFromTextureCoord(uvwz.w, SCATTERING_TEXTURE_R_SIZE);
    r = sqrt(rho * rho + atmosphere.bottom_radius * atmosphere.bottom_radius);

    if (uvwz.z < 0.5) 
    {
        // Distance to the ground for the ray (r,mu), and its minimum and maximum
        // values over all mu - obtained for (r,-1) and (r,mu_horizon) - from which
        // we can recover mu:
        Length d_min = r - atmosphere.bottom_radius;
        Length d_max = rho;
        Length d = d_min + (d_max - d_min) * GetUnitRangeFromTextureCoord(
            1.0 - 2.0 * uvwz.z, SCATTERING_TEXTURE_MU_SIZE / 2);
        mu = d == 0.0 * m ? Number(-1.0) :
            ClampCosine(-(rho * rho + d * d) / (2.0 * r * d));
        ray_r_mu_intersects_ground = true;
    } 
    else 
    {
        // Distance to the top atmosphere boundary for the ray (r,mu), and its
        // minimum and maximum values over all mu - obtained for (r,1) and
        // (r,mu_horizon) - from which we can recover mu:
        Length d_min = atmosphere.top_radius - r;
        Length d_max = rho + H;
        Length d = d_min + (d_max - d_min) * GetUnitRangeFromTextureCoord(
            2.0 * uvwz.z - 1.0, SCATTERING_TEXTURE_MU_SIZE / 2);
        mu = d == 0.0 * m ? Number(1.0) :
            ClampCosine((H * H - rho * rho - d * d) / (2.0 * r * d));
        ray_r_mu_intersects_ground = false;
    }

    Number x_mu_s =
        GetUnitRangeFromTextureCoord(uvwz.y, SCATTERING_TEXTURE_MU_S_SIZE);
    Length d_min = atmosphere.top_radius - atmosphere.bottom_radius;
    Length d_max = H;
    Length D = DistanceToTopAtmosphereBoundary(
        atmosphere, atmosphere.bottom_radius, atmosphere.mu_s_min);
    Number A = (D - d_min) / (d_max - d_min);
    Number a = (A - x_mu_s * A) / (1.0 + x_mu_s * A);
    Length d = d_min + min(a, A) * (d_max - d_min);
    mu_s = d == 0.0 * m ? Number(1.0) :
        ClampCosine((H * H - d * d) / (2.0 * atmosphere.bottom_radius * d));

    nu = ClampCosine(uvwz.x * 2.0 - 1.0);
}

/*
<p>We assumed above that we have 4D textures, which is not the case in practice.
We therefore need a further mapping, between 3D and 4D texture coordinates. The
function below expands a 3D texel coordinate into a 4D texture coordinate, and
then to $(r,\mu,\mu_s,\nu)$ parameters. It does so by "unpacking" two texel
coordinates from the $x$ texel coordinate. Note also how we clamp the $\nu$
parameter at the end. This is because $\nu$ is not a fully independent variable:
its range of values depends on $\mu$ and $\mu_s$ (this can be seen by computing
$\mu$, $\mu_s$ and $\nu$ from the cartesian coordinates of the zenith, view and
sun unit direction vectors), and the previous functions implicitely assume this
(their assertions can break if this constraint is not respected).
*/

void GetRMuMuSNuFromScatteringTextureFragCoord(
    IN(AtmosphereParameters) atmosphere, IN(float3) frag_coord,
    OUT(Length) r, OUT(Number) mu, OUT(Number) mu_s, OUT(Number) nu,
    OUT(bool) ray_r_mu_intersects_ground) 
{
    const float4 SCATTERING_TEXTURE_SIZE = float4(
        SCATTERING_TEXTURE_NU_SIZE - 1,
        SCATTERING_TEXTURE_MU_S_SIZE,
        SCATTERING_TEXTURE_MU_SIZE,
        SCATTERING_TEXTURE_R_SIZE);
        
    Number frag_coord_nu = floor(frag_coord.x / Number(SCATTERING_TEXTURE_MU_S_SIZE));
    Number frag_coord_mu_s = mod(frag_coord.x, Number(SCATTERING_TEXTURE_MU_S_SIZE));
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
    IN(TransmittanceTexture) transmittance_texture, IN(float3) frag_coord,
    OUT(IrradianceSpectrum) rayleigh, OUT(IrradianceSpectrum) mie) 
{
    Length r;
    Number mu;
    Number mu_s;
    Number nu;
    bool ray_r_mu_intersects_ground;
    GetRMuMuSNuFromScatteringTextureFragCoord(atmosphere, frag_coord, r, mu, mu_s, nu, ray_r_mu_intersects_ground);
    ComputeSingleScattering(atmosphere, transmittance_texture, r, mu, mu_s, nu, ray_r_mu_intersects_ground, rayleigh, mie);
}

/*
<h4 id="single_scattering_lookup">Lookup</h4>

<p>With the help of the above precomputed texture, we can now get the scattering
between a point_ and the nearest atmosphere boundary with two texture lookups (we
need two 3D texture lookups to emulate a single 4D texture lookup with
quadrilinear interpolation; the 3D texture coordinates are computed using the
inverse of the 3D-4D mapping defined in
<code>GetRMuMuSNuFromScatteringTextureFragCoord</code>):
*/
AbstractSpectrum GetScattering(
    IN(AtmosphereParameters) atmosphere,
    AbstractScatteringTexture scattering_texture,
    Length r, Number mu, Number mu_s, Number nu,
    bool ray_r_mu_intersects_ground) 
{
  float4 uvwz = GetScatteringTextureUvwzFromRMuMuSNu(
      atmosphere, r, mu, mu_s, nu, ray_r_mu_intersects_ground);
  Number tex_coord_x = uvwz.x * Number(SCATTERING_TEXTURE_NU_SIZE - 1);
  Number tex_x = floor(tex_coord_x);
  Number lerp = tex_coord_x - tex_x;
    
  float3 uvw0 = float3((tex_x + uvwz.y) / Number(SCATTERING_TEXTURE_NU_SIZE), uvwz.z, uvwz.w);
  float3 uvw1 = float3((tex_x + 1.0 + uvwz.y) / Number(SCATTERING_TEXTURE_NU_SIZE), uvwz.z, uvwz.w);

      

  return AbstractSpectrum(scattering_texture.SampleLevel(ATMOSPHERE_LINEAR_SAMPLER, uvw0, 0).xyz * (1.0 - lerp) +
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

RadianceSpectrum GetScattering(
    IN(AtmosphereParameters) atmosphere,
    IN(ReducedScatteringTexture) single_rayleigh_scattering_texture,
    IN(ReducedScatteringTexture) single_mie_scattering_texture,
    IN(ScatteringTexture) multiple_scattering_texture,
    Length r, Number mu, Number mu_s, Number nu,
    bool ray_r_mu_intersects_ground,
    int scattering_order) 
{
    if (scattering_order == 1) 
    {
        IrradianceSpectrum rayleigh = GetScattering(atmosphere, single_rayleigh_scattering_texture, r, mu, mu_s, nu, ray_r_mu_intersects_ground);
        IrradianceSpectrum mie = GetScattering(atmosphere, single_mie_scattering_texture, r, mu, mu_s, nu, ray_r_mu_intersects_ground);

        return rayleigh * rayleighPhase(nu) + mie * cornetteShanksMiePhase(atmosphere.mie_phase_function_g, nu);
    } 
    else 
    {
        return GetScattering(atmosphere, multiple_scattering_texture, r, mu, mu_s, nu, ray_r_mu_intersects_ground);
    }
}

IrradianceSpectrum GetIrradiance(
    IN(AtmosphereParameters) atmosphere,
    IN(IrradianceTexture) irradiance_texture,
    Length r, Number mu_s);

RadianceDensitySpectrum ComputeScatteringDensity(
    IN(AtmosphereParameters) atmosphere,
    IN(TransmittanceTexture) transmittance_texture,
    IN(ReducedScatteringTexture) single_rayleigh_scattering_texture,
    IN(ReducedScatteringTexture) single_mie_scattering_texture,
    IN(ScatteringTexture) multiple_scattering_texture,
    IN(IrradianceTexture) irradiance_texture,
    Length r, Number mu, Number mu_s, Number nu, int scattering_order) 
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
    Number sun_dir_x = omega.x == 0.0 ? 0.0 : (nu - mu * mu_s) / omega.x;
    Number sun_dir_y = sqrt(max(1.0 - sun_dir_x * sun_dir_x - mu_s * mu_s, 0.0));
    float3 omega_s = float3(sun_dir_x, sun_dir_y, mu_s);

    const int SAMPLE_COUNT = 16;
    const Angle dphi = pi / Number(SAMPLE_COUNT);
    const Angle dtheta = pi / Number(SAMPLE_COUNT);
    RadianceDensitySpectrum rayleigh_mie = (0.0 * watt_per_cubic_meter_per_sr_per_nm);

    // Nested loops for the integral over all the incident directions omega_i.
    for (int l = 0; l < SAMPLE_COUNT; ++l) 
    {
        Angle theta = (Number(l) + 0.5) * dtheta;
        Number cos_theta = cos(theta);
        Number sin_theta = sin(theta);
        bool ray_r_theta_intersects_ground = RayIntersectsGround(atmosphere, r, cos_theta);

        // The distance and transmittance to the ground only depend on theta, so we
        // can compute them in the outer loop for efficiency.
        Length distance_to_ground = 0.0 * m;
        DimensionlessSpectrum transmittance_to_ground = (0.0);
        DimensionlessSpectrum ground_albedo = (0.0);
        if (ray_r_theta_intersects_ground)
        {
            distance_to_ground = DistanceToBottomAtmosphereBoundary(atmosphere, r, cos_theta);
            transmittance_to_ground = GetTransmittance(atmosphere, transmittance_texture, r, cos_theta, distance_to_ground, true /* ray_intersects_ground */);
            ground_albedo = atmosphere.ground_albedo;
        }

        for (int m = 0; m < 2 * SAMPLE_COUNT; ++m) 
        {
            Angle phi = (Number(m) + 0.5) * dphi;
            float3 omega_i = float3(cos(phi) * sin_theta, sin(phi) * sin_theta, cos_theta);
            SolidAngle domega_i = (dtheta / rad) * (dphi / rad) * sin(theta) * sr;

            // The radiance L_i arriving from direction omega_i after n-1 bounces is
            // the sum of a term given by the precomputed scattering texture for the
            // (n-1)-th order:
            Number nu1 = dot(omega_s, omega_i);
            RadianceSpectrum incident_radiance = GetScattering(atmosphere,
                single_rayleigh_scattering_texture, single_mie_scattering_texture,
                multiple_scattering_texture, r, omega_i.z, mu_s, nu1,
                ray_r_theta_intersects_ground, scattering_order - 1);

            // and of the contribution from the light paths with n-1 bounces and whose
            // last bounce is on the ground. This contribution is the product of the
            // transmittance to the ground, the ground albedo, the ground BRDF, and
            // the irradiance received on the ground after n-2 bounces.
            float3 ground_normal = normalize(zenith_direction * r + omega_i * distance_to_ground);

            IrradianceSpectrum ground_irradiance = GetIrradiance(atmosphere, irradiance_texture, atmosphere.bottom_radius, dot(ground_normal, omega_s));
            incident_radiance += transmittance_to_ground * ground_albedo * (1.0 / (PI * sr)) * ground_irradiance;

            // The radiance finally scattered from direction omega_i towards direction
            // -omega is the product of the incident radiance, the scattering
            // coefficient, and the phase function for directions omega and omega_i
            // (all this summed over all particle types, i.e. Rayleigh and Mie).
            Number nu2 = dot(omega, omega_i);
            Number rayleigh_density = GetProfileDensity(atmosphere.rayleigh_density, r - atmosphere.bottom_radius);

            Number mie_density = GetProfileDensity(atmosphere.mie_density, r - atmosphere.bottom_radius);
            rayleigh_mie += incident_radiance * (atmosphere.rayleigh_scattering * rayleigh_density * rayleighPhase(nu2) + atmosphere.mie_scattering * mie_density * cornetteShanksMiePhase(atmosphere.mie_phase_function_g, nu2)) * domega_i;
        }
    }
    
    return rayleigh_mie;
}

/*
<h5 id="multiple_scattering_second_step">Second step</h5>

<p>The second step to compute the $n$-th order of scattering is to compute for
each point_ $\bp$ and direction $\bw$, the radiance coming from direction $\bw$
after $n$ bounces, using a texture precomputed with the previous function.

<p>This radiance is the integral over all points $\bq$ between $\bp$ and the
nearest atmosphere boundary in direction $\bw$ of the product of:
<ul>
<li>a term given by a texture precomputed with the previous function, namely
the radiance scattered at $\bq$ towards $\bp$, coming from any direction after
$n-1$ bounces,</li>
<li>the transmittance betweeen $\bp$ and $\bq$</li>
</ul>
Note that this excludes the light paths with $n$ bounces and whose last
bounce is on the ground, on purpose. Indeed, we chose to exclude these paths
from our precomputed textures so that we can compute them at render time
instead, using the actual ground albedo.

<p>The implementation for this second step is straightforward:
*/

RadianceSpectrum ComputeMultipleScattering(
    IN(AtmosphereParameters) atmosphere,
    IN(TransmittanceTexture) transmittance_texture,
    IN(ScatteringDensityTexture) scattering_density_texture,
    Length r, Number mu, Number mu_s, Number nu,
    bool ray_r_mu_intersects_ground) 
{
    ATMOSPHERE_CHECK(r >= atmosphere.bottom_radius && r <= atmosphere.top_radius);
    ATMOSPHERE_CHECK(mu >= -1.0 && mu <= 1.0);
    ATMOSPHERE_CHECK(mu_s >= -1.0 && mu_s <= 1.0);
    ATMOSPHERE_CHECK(nu >= -1.0 && nu <= 1.0);

    // Number of intervals for the numerical integration.
    const int SAMPLE_COUNT = 50;
    // The integration step, i.e. the length of each integration interval.
    Length dx = DistanceToNearestAtmosphereBoundary(atmosphere, r, mu, ray_r_mu_intersects_ground) / Number(SAMPLE_COUNT);

    // Integration loop.
    RadianceSpectrum rayleigh_mie_sum = (0.0 * watt_per_square_meter_per_sr_per_nm);
    for (int i = 0; i <= SAMPLE_COUNT; ++i) 
    {
        Length d_i = Number(i) * dx;

        // The r, mu and mu_s parameters at the current integration point_ (see the
        // single scattering section for a detailed explanation).
        Length r_i = ClampRadius(atmosphere, sqrt(d_i * d_i + 2.0 * r * mu * d_i + r * r));
        Number mu_i = ClampCosine((r * mu + d_i) / r_i);
        Number mu_s_i = ClampCosine((r * mu_s + d_i * nu) / r_i);

        // The Rayleigh and Mie multiple scattering at the current sample point_.
        RadianceSpectrum rayleigh_mie_i =
            GetScattering(atmosphere, scattering_density_texture, r_i, mu_i, mu_s_i, nu, ray_r_mu_intersects_ground) *
            GetTransmittance(atmosphere, transmittance_texture, r, mu, d_i, ray_r_mu_intersects_ground) * dx;

        // Sample weight (from the trapezoidal rule).
        Number weight_i = (i == 0 || i == SAMPLE_COUNT) ? 0.5 : 1.0;
        rayleigh_mie_sum += rayleigh_mie_i * weight_i;
    }
    return rayleigh_mie_sum;
}

/*
<h4 id="multiple_scattering_precomputation">Precomputation</h4>

<p>As explained in the <a href="#multiple_scattering">overall algorithm</a> to
compute multiple scattering, we need to precompute each order of scattering in a
texture to save computations while computing the next order. And, in order to
store a function in a texture, we need a mapping from the function parameters to
texture coordinates. Fortunately, all the orders of scattering depend on the
same $(r,\mu,\mu_s,\nu)$ parameters as single scattering, so we can simple reuse
the mappings defined for single scattering. This immediately leads to the
following simple functions to precompute a texel of the textures for the
<a href="#multiple_scattering_first_step">first</a> and
<a href="#multiple_scattering_second_step">second</a> steps of each iteration
over the number of bounces:
*/

RadianceDensitySpectrum ComputeScatteringDensityTexture(
    IN(AtmosphereParameters) atmosphere,
    IN(TransmittanceTexture) transmittance_texture,
    IN(ReducedScatteringTexture) single_rayleigh_scattering_texture,
    IN(ReducedScatteringTexture) single_mie_scattering_texture,
    IN(ScatteringTexture) multiple_scattering_texture,
    IN(IrradianceTexture) irradiance_texture,
    IN(float3) frag_coord, int scattering_order) 
{
    Length r;
    Number mu;
    Number mu_s;
    Number nu;
    bool ray_r_mu_intersects_ground;
    GetRMuMuSNuFromScatteringTextureFragCoord(atmosphere, frag_coord, r, mu, mu_s, nu, ray_r_mu_intersects_ground);

    return ComputeScatteringDensity(atmosphere, transmittance_texture,
        single_rayleigh_scattering_texture, single_mie_scattering_texture,
        multiple_scattering_texture, irradiance_texture, r, mu, mu_s, nu,
        scattering_order);
}

RadianceSpectrum ComputeMultipleScatteringTexture(
    IN(AtmosphereParameters) atmosphere,
    IN(TransmittanceTexture) transmittance_texture,
    IN(ScatteringDensityTexture) scattering_density_texture,
    IN(float3) frag_coord, OUT(Number) nu)
{
    Length r;
    Number mu;
    Number mu_s;
    bool ray_r_mu_intersects_ground;
    GetRMuMuSNuFromScatteringTextureFragCoord(atmosphere, frag_coord, r, mu, mu_s, nu, ray_r_mu_intersects_ground);

    return ComputeMultipleScattering(atmosphere, transmittance_texture, scattering_density_texture, r, mu, mu_s, nu, ray_r_mu_intersects_ground);
}

IrradianceSpectrum ComputeDirectIrradiance(
    in const AtmosphereParameters atmosphere,
    in const TransmittanceTexture transmittance_texture,
    Length r, 
    Number mu_s) 
{
    ATMOSPHERE_CHECK(r >= atmosphere.bottom_radius && r <= atmosphere.top_radius);
    ATMOSPHERE_CHECK(mu_s >= -1.0 && mu_s <= 1.0);

    Number alpha_s = atmosphere.sun_angular_radius / rad;

    // Approximate average of the cosine factor mu_s over the visible fraction of
    // the Sun disc.
    Number average_cosine_factor = mu_s < -alpha_s ? 0.0 : (mu_s > alpha_s ? mu_s : (mu_s + alpha_s) * (mu_s + alpha_s) / (4.0 * alpha_s));

    return atmosphere.solar_irradiance * GetTransmittanceToTopAtmosphereBoundary(atmosphere, transmittance_texture, r, mu_s) * average_cosine_factor;
}

IrradianceSpectrum ComputeIndirectIrradiance(
    IN(AtmosphereParameters) atmosphere,
    IN(ReducedScatteringTexture) single_rayleigh_scattering_texture,
    IN(ReducedScatteringTexture) single_mie_scattering_texture,
    IN(ScatteringTexture) multiple_scattering_texture,
    Length r, 
    Number mu_s, 
    int scattering_order) 
{
    ATMOSPHERE_CHECK(r >= atmosphere.bottom_radius && r <= atmosphere.top_radius);
    ATMOSPHERE_CHECK(mu_s >= -1.0 && mu_s <= 1.0);
    ATMOSPHERE_CHECK(scattering_order >= 1);

    const int SAMPLE_COUNT = 32;
    const Angle dphi = pi / Number(SAMPLE_COUNT);
    const Angle dtheta = pi / Number(SAMPLE_COUNT);

    IrradianceSpectrum result = (0.0 * watt_per_square_meter_per_nm);
    float3 omega_s = float3(sqrt(1.0 - mu_s * mu_s), 0.0, mu_s);

    for (int j = 0; j < SAMPLE_COUNT / 2; ++j) 
    {
        Angle theta = (Number(j) + 0.5) * dtheta;
        for (int i = 0; i < 2 * SAMPLE_COUNT; ++i) 
        {
            Angle phi = (Number(i) + 0.5) * dphi;
            float3 omega = float3(cos(phi) * sin(theta), sin(phi) * sin(theta), cos(theta));
            SolidAngle domega = (dtheta / rad) * (dphi / rad) * sin(theta) * sr;

            Number nu = dot(omega, omega_s);
            result += GetScattering(atmosphere, single_rayleigh_scattering_texture, single_mie_scattering_texture, multiple_scattering_texture, r, omega.z, mu_s, nu, false /* ray_r_theta_intersects_ground */, scattering_order) * omega.z * domega;
        }
    }
    return result;
}

/*
<h4 id="irradiance_precomputation">Precomputation</h4>

<p>In order to precompute the ground irradiance in a texture we need a mapping
from the ground irradiance parameters to texture coordinates. Since we
precompute the ground irradiance only for horizontal surfaces, this irradiance
depends only on $r$ and $\mu_s$, so we need a mapping from $(r,\mu_s)$ to
$(u,v)$ texture coordinates. The simplest, affine mapping is sufficient here,
because the ground irradiance function is very smooth:
*/

float2 GetIrradianceTextureUvFromRMuS(IN(AtmosphereParameters) atmosphere, Length r, Number mu_s) 
{
    ATMOSPHERE_CHECK(r >= atmosphere.bottom_radius && r <= atmosphere.top_radius);
    ATMOSPHERE_CHECK(mu_s >= -1.0 && mu_s <= 1.0);

    Number x_r = (r - atmosphere.bottom_radius) / (atmosphere.top_radius - atmosphere.bottom_radius);
    Number x_mu_s = mu_s * 0.5 + 0.5;

    return float2(GetTextureCoordFromUnitRange(x_mu_s, IRRADIANCE_TEXTURE_WIDTH), GetTextureCoordFromUnitRange(x_r, IRRADIANCE_TEXTURE_HEIGHT));
}

/*
<p>The inverse mapping follows immediately:
*/

void GetRMuSFromIrradianceTextureUv(IN(AtmosphereParameters) atmosphere, IN(float2) uv, OUT(Length) r, OUT(Number) mu_s) 
{
    ATMOSPHERE_CHECK(uv.x >= 0.0 && uv.x <= 1.0);
    ATMOSPHERE_CHECK(uv.y >= 0.0 && uv.y <= 1.0);
    Number x_mu_s = GetUnitRangeFromTextureCoord(uv.x, IRRADIANCE_TEXTURE_WIDTH);
    Number x_r = GetUnitRangeFromTextureCoord(uv.y, IRRADIANCE_TEXTURE_HEIGHT);
    r = atmosphere.bottom_radius + x_r * (atmosphere.top_radius - atmosphere.bottom_radius);
    mu_s = ClampCosine(2.0 * x_mu_s - 1.0);
}

/*
<p>It is now easy to define a fragment shader function to precompute a texel of
the ground irradiance texture, for the direct irradiance:
*/

static const float2 IRRADIANCE_TEXTURE_SIZE = float2(IRRADIANCE_TEXTURE_WIDTH, IRRADIANCE_TEXTURE_HEIGHT);

IrradianceSpectrum ComputeDirectIrradianceTexture(
    IN(AtmosphereParameters) atmosphere,
    IN(TransmittanceTexture) transmittance_texture,
    IN(float2) frag_coord) 
{
    Length r;
    Number mu_s;
    GetRMuSFromIrradianceTextureUv(atmosphere, frag_coord / IRRADIANCE_TEXTURE_SIZE, r, mu_s);
    return ComputeDirectIrradiance(atmosphere, transmittance_texture, r, mu_s);
}

/*
<p>and the indirect one:
*/

IrradianceSpectrum ComputeIndirectIrradianceTexture(
    IN(AtmosphereParameters) atmosphere,
    IN(ReducedScatteringTexture) single_rayleigh_scattering_texture,
    IN(ReducedScatteringTexture) single_mie_scattering_texture,
    IN(ScatteringTexture) multiple_scattering_texture,
    IN(float2) frag_coord, int scattering_order) 
{
    Length r;
    Number mu_s;
    GetRMuSFromIrradianceTextureUv(atmosphere, frag_coord / IRRADIANCE_TEXTURE_SIZE, r, mu_s);
    return ComputeIndirectIrradiance(atmosphere, single_rayleigh_scattering_texture, single_mie_scattering_texture, multiple_scattering_texture, r, mu_s, scattering_order);
}

/*
<h4 id="irradiance_lookup">Lookup</h4>

<p>Thanks to these precomputed textures, we can now get the ground irradiance
with a single texture lookup:
*/

IrradianceSpectrum GetIrradiance(
    in const AtmosphereParameters atmosphere,
    IrradianceTexture irradiance_texture,
    Length r, Number mu_s) 
{
    float2 uv = GetIrradianceTextureUvFromRMuS(atmosphere, r, mu_s);
    return IrradianceSpectrum(irradiance_texture.SampleLevel(ATMOSPHERE_LINEAR_SAMPLER, uv, 0).xyz);
}

/*
<h3 id="rendering">Rendering</h3>

<p>Here we assume that the transmittance, scattering and irradiance textures
have been precomputed, and we provide functions using them to compute the sky
color, the aerial perspective, and the ground radiance.

<p>More precisely, we assume that the single Rayleigh scattering, without its
phase function term, plus the multiple scattering terms (divided by the Rayleigh
phase function for dimensional homogeneity) are stored in a
<code>scattering_texture</code>. We also assume that the single Mie scattering
is stored, without its phase function term:
<ul>
<li>either separately, in a <code>single_mie_scattering_texture</code> (this
option was not provided our <a href=
"http://evasion.inrialpes.fr/~Eric.Bruneton/PrecomputedAtmosphericScattering2.zip"
>original implementation</a>),</li>
<li>or, if the <code>COMBINED_SCATTERING_TEXTURES</code> preprocessor
macro is defined, in the <code>scattering_texture</code>. In this case, which is
only available with a GLSL compiler, Rayleigh and multiple scattering are stored
in the RGB channels, and the red component of the single Mie scattering is
stored in the alpha channel).</li>
</ul>

<p>In the second case, the green and blue components of the single Mie
scattering are extrapolated as described in our
<a href="https://hal.inria.fr/inria-00288758/en">paper</a>, with the following
function:
*/

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

/*
<p>We can then retrieve all the scattering components (Rayleigh + multiple
scattering on one side, and single Mie scattering on the other side) with the
following function, based on
<a href="#single_scattering_lookup"><code>GetScattering</code></a> (we duplicate
some code here, instead of using two calls to <code>GetScattering</code>, to
make sure that the texture coordinates computation is shared between the lookups
in <code>scattering_texture</code> and
<code>single_mie_scattering_texture</code>):
*/

IrradianceSpectrum GetCombinedScattering(
    IN(AtmosphereParameters) atmosphere,
    IN(ReducedScatteringTexture) scattering_texture,
    IN(ReducedScatteringTexture) single_mie_scattering_texture,
    Length r, Number mu, Number mu_s, Number nu,
    bool ray_r_mu_intersects_ground,
    OUT(IrradianceSpectrum) single_mie_scattering) 
{
    float4 uvwz = GetScatteringTextureUvwzFromRMuMuSNu(atmosphere, r, mu, mu_s, nu, ray_r_mu_intersects_ground);

    Number tex_coord_x = uvwz.x * Number(SCATTERING_TEXTURE_NU_SIZE - 1);
    Number tex_x = floor(tex_coord_x);
    Number lerp = tex_coord_x - tex_x;
    float3 uvw0 = float3((tex_x + uvwz.y) / Number(SCATTERING_TEXTURE_NU_SIZE), uvwz.z, uvwz.w);
    float3 uvw1 = float3((tex_x + 1.0 + uvwz.y) / Number(SCATTERING_TEXTURE_NU_SIZE), uvwz.z, uvwz.w);

    IrradianceSpectrum scattering;
    if(atmosphere.bCombineScattering != 0)
    {
        float4 combined_scattering = scattering_texture.SampleLevel(ATMOSPHERE_LINEAR_SAMPLER, uvw0, 0) * (1.0 - lerp) + scattering_texture.SampleLevel(ATMOSPHERE_LINEAR_SAMPLER, uvw1, 0) * lerp;
        scattering = combined_scattering.xyz;
        single_mie_scattering = GetExtrapolatedSingleMieScattering(atmosphere, combined_scattering);
    }
    else
    {
        scattering = IrradianceSpectrum(scattering_texture.SampleLevel(ATMOSPHERE_LINEAR_SAMPLER, uvw0, 0).xyz * (1.0 - lerp) + scattering_texture.SampleLevel(ATMOSPHERE_LINEAR_SAMPLER, uvw1, 0).xyz * lerp);
        single_mie_scattering = IrradianceSpectrum(single_mie_scattering_texture.SampleLevel(ATMOSPHERE_LINEAR_SAMPLER, uvw0, 0).xyz * (1.0 - lerp) + single_mie_scattering_texture.SampleLevel(ATMOSPHERE_LINEAR_SAMPLER, uvw1, 0).xyz * lerp);
    }

    return scattering;
}

/*
<h4 id="rendering_sky">Sky</h4>

<p>To render the sky we simply need to display the sky radiance, which we can
get with a lookup in the precomputed scattering texture(s), multiplied by the
phase function terms that were omitted during precomputation. We can also return
the transmittance of the atmosphere (which we can get with a single lookup in
the precomputed transmittance texture), which is needed to correctly render the
objects in space (such as the Sun and the Moon). This leads to the following
function, where most of the computations are used to correctly handle the case
of viewers outside the atmosphere, and the case of light shafts:
*/

RadianceSpectrum GetSkyRadiance(
    IN(AtmosphereParameters) atmosphere,
    IN(TransmittanceTexture) transmittance_texture,
    IN(ReducedScatteringTexture) scattering_texture,
    IN(ReducedScatteringTexture) single_mie_scattering_texture,
    float3 camera, IN(float3) view_ray, Length shadow_length,
    IN(float3) sun_direction, OUT(DimensionlessSpectrum) transmittance) 
{
    // Compute the distance to the top atmosphere boundary along the view ray,
    // assuming the viewer is in space (or NaN if the view ray does not intersect
    // the atmosphere).
    Length r = length(camera);
    Length rmu = dot(camera, view_ray);

    Length distance_to_top_atmosphere_boundary = -rmu - sqrt(rmu * rmu - r * r + atmosphere.top_radius * atmosphere.top_radius);
    // If the viewer is in space and the view ray intersects the atmosphere, move
    // the viewer to the top atmosphere boundary (along the view ray):
    if (distance_to_top_atmosphere_boundary > 0.0 * m) 
    {
        camera = camera + view_ray * distance_to_top_atmosphere_boundary;
        r = atmosphere.top_radius;
        rmu += distance_to_top_atmosphere_boundary;
    } 
    else if (r > atmosphere.top_radius) 
    {
        // If the view ray does not intersect the atmosphere, simply return 0.
        transmittance = (1.0);
        return (0.0 * watt_per_square_meter_per_sr_per_nm);
    }

    // Compute the r, mu, mu_s and nu parameters needed for the texture lookups.
    Number mu = rmu / r;

    Number mu_s = dot(camera, sun_direction) / r;
    Number nu = dot(view_ray, sun_direction);
    bool ray_r_mu_intersects_ground = RayIntersectsGround(atmosphere, r, mu);

    transmittance = ray_r_mu_intersects_ground ? (0.0) : GetTransmittanceToTopAtmosphereBoundary(atmosphere, transmittance_texture, r, mu);

    IrradianceSpectrum single_mie_scattering;
    IrradianceSpectrum scattering;
    if (shadow_length == 0.0 * m) 
    {
        scattering = GetCombinedScattering(atmosphere, scattering_texture, single_mie_scattering_texture, r, mu, mu_s, nu, ray_r_mu_intersects_ground, single_mie_scattering);
    } 
    else 
    {
        // Case of light shafts (shadow_length is the total length noted l in our
        // paper): we omit the scattering between the camera and the point_ at
        // distance l, by implementing Eq. (18) of the paper (shadow_transmittance
        // is the T(x,x_s) term, scattering is the S|x_s=x+lv term).
        Length d = shadow_length;
        Length r_p = ClampRadius(atmosphere, sqrt(d * d + 2.0 * r * mu * d + r * r));
        Number mu_p = (r * mu + d) / r_p;
        Number mu_s_p = (r * mu_s + d * nu) / r_p;

        scattering = GetCombinedScattering(atmosphere, scattering_texture, single_mie_scattering_texture, r_p, mu_p, mu_s_p, nu, ray_r_mu_intersects_ground, single_mie_scattering);
        DimensionlessSpectrum shadow_transmittance = GetTransmittance(atmosphere, transmittance_texture, r, mu, shadow_length, ray_r_mu_intersects_ground);
        scattering = scattering * shadow_transmittance;
        single_mie_scattering = single_mie_scattering * shadow_transmittance;
    }

    const float3 skySpectralRadianceToLumiance = (atmosphere.luminanceMode != LUMINANCE_MODE_NONE) ? atmosphere.skySpectralRadianceToLumiance : 1.0;
    const float3 skyScattering = scattering * rayleighPhase(nu) + single_mie_scattering * cornetteShanksMiePhase(atmosphere.mie_phase_function_g, nu);

    const float3 skyLumianceSRGB = skyScattering * skySpectralRadianceToLumiance;
    return mul(sRGB_2_AP1, skyLumianceSRGB); // SRGB -> ACEScg
}

/*
<h4 id="rendering_aerial_perspective">Aerial perspective</h4>

<p>To render the aerial perspective we need the transmittance and the scattering
between two points (i.e. between the viewer and a point_ on the ground, which can
at an arbibrary altitude). We already have a function to compute the
transmittance between two points (using 2 lookups in a texture which only
contains the transmittance to the top of the atmosphere), but we don't have one
for the scattering between 2 points. Hopefully, the scattering between 2 points
can be computed from two lookups in a texture which contains the scattering to
the nearest atmosphere boundary, as for the transmittance (except that here the
two lookup results must be subtracted, instead of divided). This is what we
implement in the following function (the initial computations are used to
correctly handle the case of viewers outside the atmosphere):
*/

RadianceSpectrum GetSkyRadianceToPoint(
    IN(AtmosphereParameters) atmosphere,
    IN(TransmittanceTexture) transmittance_texture,
    IN(ReducedScatteringTexture) scattering_texture,
    IN(ReducedScatteringTexture) single_mie_scattering_texture,
    float3 camera,
    float3 point_, 
    Length shadow_length,
    float3 sun_direction, 
    OUT(DimensionlessSpectrum) transmittance) 
{
    // Compute the distance to the top atmosphere boundary along the view ray,
    // assuming the viewer is in space (or NaN if the view ray does not intersect
    // the atmosphere).
    float3 view_ray = normalize(point_ - camera);
    Length r = length(camera);
    Length rmu = dot(camera, view_ray);
    Length distance_to_top_atmosphere_boundary = -rmu - sqrt(rmu * rmu - r * r + atmosphere.top_radius * atmosphere.top_radius);
    
    // If the viewer is in space and the view ray intersects the atmosphere, move
    // the viewer to the top atmosphere boundary (along the view ray):
    if (distance_to_top_atmosphere_boundary > 0.0 * m) 
    {
        camera = camera + view_ray * distance_to_top_atmosphere_boundary;
        r = atmosphere.top_radius;
        rmu += distance_to_top_atmosphere_boundary;
    }

    // Compute the r, mu, mu_s and nu parameters for the first texture lookup.
    Number mu = rmu / r;
    Number mu_s = dot(camera, sun_direction) / r;
    Number nu = dot(view_ray, sun_direction);
    Length d = length(point_ - camera);
    bool ray_r_mu_intersects_ground = RayIntersectsGround(atmosphere, r, mu);

    transmittance = GetTransmittance(atmosphere, transmittance_texture, r, mu, d, ray_r_mu_intersects_ground);

    IrradianceSpectrum single_mie_scattering;
    IrradianceSpectrum scattering = GetCombinedScattering(atmosphere, scattering_texture, single_mie_scattering_texture, r, mu, mu_s, nu, ray_r_mu_intersects_ground, single_mie_scattering);

    // Compute the r, mu, mu_s and nu parameters for the second texture lookup.
    // If shadow_length is not 0 (case of light shafts), we want to ignore the
    // scattering along the last shadow_length meters of the view ray, which we
    // do by subtracting shadow_length from d (this way scattering_p is equal to
    // the S|x_s=x_0-lv term in Eq. (17) of our paper).
    d = max(d - shadow_length, 0.0 * m);
    Length r_p = ClampRadius(atmosphere, sqrt(d * d + 2.0 * r * mu * d + r * r));
    Number mu_p = (r * mu + d) / r_p;
    Number mu_s_p = (r * mu_s + d * nu) / r_p;

    IrradianceSpectrum single_mie_scattering_p;
    IrradianceSpectrum scattering_p = GetCombinedScattering(atmosphere, scattering_texture, single_mie_scattering_texture, r_p, mu_p, mu_s_p, nu, ray_r_mu_intersects_ground, single_mie_scattering_p);

    // Combine the lookup results to get the scattering between camera and point_.
    DimensionlessSpectrum shadow_transmittance = transmittance;
    if (shadow_length > 0.0 * m) 
    {
        // This is the T(x,x_s) term in Eq. (17) of our paper, for light shafts.
        shadow_transmittance = GetTransmittance(atmosphere, transmittance_texture, r, mu, d, ray_r_mu_intersects_ground);
    }

    scattering = scattering - shadow_transmittance * scattering_p;
    single_mie_scattering = single_mie_scattering - shadow_transmittance * single_mie_scattering_p;

    if (atmosphere.bCombineScattering != 0)
    {
        single_mie_scattering = GetExtrapolatedSingleMieScattering(atmosphere, float4(scattering, single_mie_scattering.r));
    }

    // Hack to avoid rendering artifacts when the sun is below the horizon.
    single_mie_scattering = single_mie_scattering * smoothstep(Number(0.0), Number(0.01), mu_s);

    const float3 skySpectralRadianceToLumiance = atmosphere.luminanceMode != LUMINANCE_MODE_NONE ? atmosphere.skySpectralRadianceToLumiance : 1.0;
    const float3 skyScattering = scattering * rayleighPhase(nu) + single_mie_scattering * cornetteShanksMiePhase(atmosphere.mie_phase_function_g, nu);

    const float3 skyLumianceSRGB = skyScattering * skySpectralRadianceToLumiance; 
    return mul(sRGB_2_AP1, skyLumianceSRGB); // SRGB -> ACEScg
}

/*
<h4 id="rendering_ground">Ground</h4>

<p>To render the ground we need the irradiance received on the ground after 0 or
more bounce(s) in the atmosphere or on the ground. The direct irradiance can be
computed with a lookup in the transmittance texture,
via <code>GetTransmittanceToSun</code>, while the indirect irradiance is given
by a lookup in the precomputed irradiance texture (this texture only contains
the irradiance for horizontal surfaces; we use the approximation defined in our
<a href="https://hal.inria.fr/inria-00288758/en">paper</a> for the other cases).
The function below returns the direct and indirect irradiances separately:
*/

IrradianceSpectrum GetSunAndSkyIrradiance(
    IN(AtmosphereParameters) atmosphere,
    IN(TransmittanceTexture) transmittance_texture,
    IN(IrradianceTexture) irradiance_texture,
    float3 targetPoint, float3 normal, float3 sun_direction,
    OUT(IrradianceSpectrum) skyIrradiance) 
{
    Length r = length(targetPoint);
    Number mu_s = dot(targetPoint, sun_direction) / r;

    const float3 skySpectralRadianceToLumiance = atmosphere.luminanceMode != LUMINANCE_MODE_NONE ? atmosphere.skySpectralRadianceToLumiance : 1.0;

    // Indirect irradiance (approximated if the surface is not horizontal).
    skyIrradiance  = GetIrradiance(atmosphere, irradiance_texture, r, mu_s) * (1.0 + dot(normal, targetPoint) / r) * 0.5;
    skyIrradiance *= skySpectralRadianceToLumiance;
    skyIrradiance  = mul(sRGB_2_AP1, skyIrradiance); // SRGB -> ACEScg

    const float3 sunSpectralRadianceToLumiance = atmosphere.luminanceMode != LUMINANCE_MODE_NONE ? atmosphere.sunSpectralRadianceToLumiance : 1.0;
    const float3 sunRadiance = atmosphere.solar_irradiance * GetTransmittanceToSun(atmosphere, transmittance_texture, r, mu_s) * max(dot(normal, sun_direction), 0.0);
    
    // Direct irradiance.
    float3 sunLumianceSRGB = sunSpectralRadianceToLumiance * sunRadiance;
    return mul(sRGB_2_AP1, sunLumianceSRGB); // SRGB -> ACEScg
}

#endif