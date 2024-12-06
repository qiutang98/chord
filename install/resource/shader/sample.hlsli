#ifndef SAMPLE_HLSLI
#define SAMPLE_HLSLI

#include "base.hlsli"

// Angle: [0, 2PI]
// https://www.shadertoy.com/view/4l3yRM
float2 vogelDiskSample(uint sampleIndex, uint sampleCount, float angle)
{
	const float goldenAngle = 2.399963f;
	const float r = sqrt(sampleIndex + 0.5f) / sqrt(sampleCount);

	const float theta = sampleIndex * goldenAngle + angle;
	float sine, cosine;
    sincos(theta, sine, cosine);
    
    return float2(cosine, sine) * r;
}

int vogelDiskSampleMinIndex(int sampleCount, float r)
{
	return clamp(int(r * r * sampleCount - 0.5), 0, sampleCount - 1);
}


// square -> concentric disk
// https://www.shadertoy.com/view/MtySRw
// https://marc-b-reynolds.github.io/math/2017/01/08/SquareDisc.html
float2 fastUniformDiskSample(float2 rnd)
{
    float2 sf = rnd * sqrt(2.0) - sqrt(0.5);
	float2 sq = sf * sf;

	float root = sqrt(2.0 * max(sq.x, sq.y) - min(sq.x, sq.y));
	if (sq.x > sq.y)
	{
		sf.x = sf.x > 0 ? root : -root;
	}
	else
	{
		sf.y = sf.y > 0 ? root : -root;
	}
	return sf;
}

// 
// Computes a low discrepancy spherically distributed direction on the unit sphere.
float3 sphericalFibonacci(float sampleIndex, float numSamples, float noise = 0.0)
{
    const float b = (sqrt(5.f) * 0.5f + 0.5f) - 1.f;

	// Animate phi by noise
    float phi = kPI * 2.0 * frac(sampleIndex * b + noise);

	//
    float cosTheta = 1.f - (2.f * sampleIndex + 1.f) * (1.f / numSamples);
    float sinTheta = sqrt(saturate(1.f - (cosTheta * cosTheta)));

	//
    return float3((cos(phi) * sinTheta), (sin(phi) * sinTheta), cosTheta);
}

// 
float4 uniformSampleHemisphere(float2 E)
{
	float phi = 2 * kPI * E.x;
	float cosTheta = E.y;
	float sinTheta = sqrt(1 - cosTheta * cosTheta);

	float3 H;
	H.x = sinTheta * cos(phi);
	H.y = sinTheta * sin(phi);
	H.z = cosTheta;

	float pdf = 1.0 / (2 * kPI);
	return float4(H, pdf);
}

// https://www.mathematik.uni-marburg.de/~thormae/lectures/graphics1/code/ImportanceSampling/importance_sampling_notes.pdf
// Based on http://blog.selfshadow.com/publications/s2013-shading-course/karis/s2013_pbs_epic_slides.pdf
// https://bruop.github.io/ibl/
float3 importanceSampleGGX(float2 Xi, float roughness) 
{
	// Maps a 2D point to a hemisphere with spread based on roughness.
	float alpha = roughness * roughness;

    // Sample in spherical coordinates.
    float phi = 2.0 * kPI * Xi.x;
	float cosTheta = sqrt((1.0 - Xi.y) / (1.0 + (alpha * alpha - 1.0) * Xi.y));
	float sinTheta = sqrt(1.0 - cosTheta * cosTheta);

    // Construct tangent space sample vector.
	return float3(sinTheta * cos(phi), sinTheta * sin(phi), cosTheta);
}

// http://jcgt.org/published/0007/04/01/paper.pdf by Eric Heitz
// Input Ve: view direction
// Input alpha_x, alpha_y: roughness parameters
// Input U1, U2: uniform random numbers
// Output Ne: normal sampled with PDF D_Ve(Ne) = G1(Ve) * max(0, dot(Ve, Ne)) * D(Ne) / Ve.z
float3 importanceSampleGGXVNDF(float3 Ve, float alpha_x, float alpha_y, float U1, float U2) 
{
    // Section 3.2: transforming the view direction to the hemisphere configuration
    float3 Vh = normalize(float3(alpha_x * Ve.x, alpha_y * Ve.y, Ve.z));

    // Section 4.1: orthonormal basis (with special case if cross product is zero)
    float lensq = Vh.x * Vh.x + Vh.y * Vh.y;
    float3 T1 = lensq > 0 ? float3(-Vh.y, Vh.x, 0) * rsqrt(lensq) : float3(1, 0, 0);
    float3 T2 = cross(Vh, T1);

    // Section 4.2: parameterization of the projected area
    float r = sqrt(U1);

    float phi = 2.0 * kPI * U2;

	// 
    float t1 = r * cos(phi);
    float t2 = r * sin(phi);

	// 
    float s = 0.5 * (1.0 + Vh.z);
    t2 = (1.0 - s) * sqrt(1.0 - t1 * t1) + s * t2;

    // Section 4.3: reprojection onto hemisphere
    float3 Nh = t1 * T1 + t2 * T2 + sqrt(max(0.0, 1.0 - t1 * t1 - t2 * t2)) * Vh;

    // Section 3.4: transforming the normal back to the ellipsoid configuration
    float3 Ne = normalize(float3(alpha_x * Nh.x, alpha_y * Nh.y, max(0.0, Nh.z)));

	// 
    return Ne;
}


#endif 