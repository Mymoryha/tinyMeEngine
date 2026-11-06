Texture2D<float4> SceneColor : register(t0);
cbuffer PostParameters : register(b0)
{
    uint Mode;
};

float4 VSMain(uint vertex_id : SV_VertexID) : SV_Position
{
    const float2 positions[3] = {float2(-1, -1), float2(-1, 3), float2(3, -1)};
    return float4(positions[vertex_id], 0, 1);
}

float4 PSMain(float4 position : SV_Position) : SV_Target0
{
    const float4 source = SceneColor.Load(int3(int2(position.xy), 0));
    const float gray = dot(source.rgb, float3(0.2126, 0.7152, 0.0722));
    return Mode == 2 ? float4(gray.xxx, source.a) : source;
}
