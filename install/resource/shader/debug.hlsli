#pragma once 

namespace debug 
{
    void printFormat(float2 data)
    {
        printf("(%f, %f)", data.x, data.y);
    }

    void printFormat(float3 data)
    {
        printf("(%f, %f, %f)", data.x, data.y, data.z);
    }

    void printFormat(float4 data)
    {
        printf("(%f, %f, %f, %f)", data.x, data.y, data.z, data.w);
    }

    void printFormat(float4x4 data, bool bSeparate = true)
    {
        if (bSeparate) printf(">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>");
        {
            printf("[%f, %f, %f, %f]", data[0][0], data[0][1], data[0][2], data[0][3]);
            printf("[%f, %f, %f, %f]", data[1][0], data[1][1], data[1][2], data[1][3]);
            printf("[%f, %f, %f, %f]", data[2][0], data[2][1], data[2][2], data[2][3]);
            printf("[%f, %f, %f, %f]", data[3][0], data[3][1], data[3][2], data[3][3]);
        }
        if (bSeparate) printf("<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<");
    }
}