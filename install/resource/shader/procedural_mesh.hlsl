// Some procedural mesh for debug use.

#include "base.hlsli"

// https://discussions.unity.com/t/full-procedural-sphere-with-vertex-shader-source-code/752570/3
void proceduralSphere(   // Triangle List
    uint  id,            // input SV_VertexID
    float radius,        // Sphere radius.
    uint  tess,          // Vertices Count = tess * tess * 6
    out float3 position, // 
    out float3 normal, 
    out float2 texcoord)
{
    int instance = int(floor(id / 6.0));

    float x = sign(mod(20.0, mod(float(id), 6.0) + 2.0));
    float y = sign(mod(18.0, mod(float(id), 6.0) + 2.0));

    float u = (float(instance / tess) + x) / float(tess);
    float v = (mod(float(instance), float(tess)) + y) / float(tess);

    float a = sin(kPI * u) * cos(2.0 * kPI * v);
    float b = sin(kPI * u) * sin(2.0 * kPI * v);
    float c = cos(kPI * u);

    position = float3(a, b, c) * radius;
    normal   = normalize(position);
    texcoord = float2(u, v);
}