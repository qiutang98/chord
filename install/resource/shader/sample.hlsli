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


// concentric: disc->square
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



#endif 