#pragma once

#include "base.hlsli"

void sh3_coefficients(float3 direction, inout float coefficients[9])
{
    coefficients[0] = 0.2820947917738781;
    coefficients[1] = direction.y * 0.4886025119029199;
    coefficients[2] = direction.z * 0.4886025119029199;
    coefficients[3] = direction.x * 0.4886025119029199;
    coefficients[4] = direction.x * direction.y * 1.0925484305920792;
    coefficients[5] = direction.y * direction.z * 1.0925484305920792;
    coefficients[6] = (3.0 * direction.z * direction.z - 1.0) * 0.31539156525252005;
    coefficients[7] = direction.x * direction.z * 1.0925484305920792;
    coefficients[8] = (direction.x * direction.x - direction.y * direction.y) * 0.5462742152960396;
}

void sh3_coefficientsClampedCosine(float3 cosineLobeDir, inout float coefficients[9])
{
    sh3_coefficients(cosineLobeDir, coefficients);

    coefficients[0] = coefficients[0] * kPI;
    coefficients[1] = coefficients[1] * (2.0 * kPI / 3.0);
    coefficients[2] = coefficients[2] * (2.0 * kPI / 3.0);
    coefficients[3] = coefficients[3] * (2.0 * kPI / 3.0);
    coefficients[4] = coefficients[4] * (kPI / 4.0);
    coefficients[5] = coefficients[5] * (kPI / 4.0);
    coefficients[6] = coefficients[6] * (kPI / 4.0);
    coefficients[7] = coefficients[7] * (kPI / 4.0);
    coefficients[8] = coefficients[8] * (kPI / 4.0);
}
