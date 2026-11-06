cbuffer TriangleParameters : register(b0)
{
    float4 Tint : packoffset(c0);
    float Scale : packoffset(c1.x);
    float3 Reserved : packoffset(c1.y);
};

cbuffer CameraTransform : register(b1)
{
    row_major float4x4 WorldToClip : packoffset(c0);
};

struct VertexInput
{
    float3 position : POSITION0;
    float3 color : COLOR0;
};

struct VertexOutput
{
    float4 position : SV_Position;
    float3 color : COLOR0;
};

VertexOutput VSMain(VertexInput input)
{
    VertexOutput output;
    output.position = mul(float4(input.position, 1.0), WorldToClip);
    output.position.xy *= Scale;
    output.color = input.color;
    return output;
}

float4 PSMain(VertexOutput input) : SV_Target0
{
    return float4(input.color, 1.0) * Tint;
}
