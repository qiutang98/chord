// Full screen triangle vertex shader.  
// 
// NOTE: Full screen triangle can reduce num of pixel shader invoked helper lane.
//       Can save some performance when pixel shader is heavy.

// http://www.altdev.co/2011/08/08/interesting-vertex-shader-trick/

struct FullScreenVS2PS
{
                        float4 pos : SV_POSITION;
    [[vk::location(0)]] float2 uv  : TEXCOORD0;
};

FullScreenVS2PS fullScreenVS(uint vertexId : SV_VertexID)
{
    FullScreenVS2PS output;

    // vertex uv #0: (0.0, 0.0)
    //           #1: (2.0, 0.0)
    //           #2: (0.0, 2.0)
	output.uv  = float2((vertexId << 1) & 2, vertexId & 2);
    output.pos = float4(output.uv * float2(2, -2) + float2(-1, 1), 0, 1);

    return output;
}