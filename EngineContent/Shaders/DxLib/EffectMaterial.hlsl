// SPDX-License-Identifier: MIT

cbuffer VertexConstants : register(b0) { row_major float4x4 Wvp; };
struct VertexInput { float3 Position : POSITION; float2 Uv : TEXCOORD0; float4 Color : COLOR0; float Dissolve : TEXCOORD1; float Emission : TEXCOORD2; float2 UVOffset : TEXCOORD3; float2 DistortionOffset : TEXCOORD4; float DistortionStrength : TEXCOORD5; };
struct VertexOutput { float4 Position : SV_POSITION; float2 Uv : TEXCOORD0; float4 Color : COLOR0; float Dissolve : TEXCOORD1; float Emission : TEXCOORD2; float2 UVOffset : TEXCOORD3; float2 DistortionOffset : TEXCOORD4; float DistortionStrength : TEXCOORD5; };
VertexOutput VSMain(VertexInput Input) { VertexOutput Output; Output.Position = mul(float4(Input.Position, 1.0), Wvp); Output.Uv = Input.Uv; Output.Color = Input.Color; Output.Dissolve = Input.Dissolve; Output.Emission = Input.Emission; Output.UVOffset = Input.UVOffset; Output.DistortionOffset = Input.DistortionOffset; Output.DistortionStrength = Input.DistortionStrength; return Output; }
Texture2D SourceTexture : register(t0);
Texture2D MaskTexture : register(t1);
Texture2D DistortionTexture : register(t2);
SamplerState WrapSampler : register(s0);
cbuffer PixelConstants : register(b0)
{
    float4 UvScaleOffset;
    float4 Values;
    float4 Tint;
    float4 Edge;
    float4 BrightBlend;
    float4 Options;
    float4 SourceRect;
    float4 MaskRect;
    float4 SourceInset;
    float4 MaskInset;
    float4 DistortionScaleOffset;
    float4 DistortionValues;
    float4 DistortionRect;
    float4 DistortionInset;
};
float Hash(float2 P) { return frac(sin(dot(P, float2(127.1, 311.7))) * 43758.5453); }
float Noise(float2 P) { float2 I = floor(P); float2 F = frac(P); F = F * F * (3.0 - 2.0 * F); return lerp(lerp(Hash(I), Hash(I + float2(1, 0)), F.x), lerp(Hash(I + float2(0, 1)), Hash(I + 1.0), F.x), F.y); }
float4 PSMain(VertexOutput Input) : SV_TARGET
{
    float IsRibbon = Options.w;
    float2 SourceOffset = IsRibbon > 0.5 ? Input.UVOffset : UvScaleOffset.zw;
    float2 SourceLogicalUv = Input.Uv * UvScaleOffset.xy + SourceOffset;
    if (DistortionValues.y > 0.5)
    {
        float2 PointDistortionOffset = IsRibbon > 0.5 ? Input.DistortionOffset : DistortionScaleOffset.zw;
        float PointDistortionStrength = IsRibbon > 0.5 ? Input.DistortionStrength : DistortionValues.x;
        float2 DistortionLogicalUv = frac(Input.Uv * DistortionScaleOffset.xy + PointDistortionOffset);
        DistortionLogicalUv = clamp(DistortionLogicalUv, DistortionInset.xy, 1.0 - DistortionInset.xy);
        float2 DistortionUv = DistortionRect.zw + DistortionLogicalUv * DistortionRect.xy;
        float2 Distortion = DistortionTexture.Sample(WrapSampler, DistortionUv).rg * 2.0 - 1.0;
        SourceLogicalUv += Distortion * PointDistortionStrength;
    }
    SourceLogicalUv = frac(SourceLogicalUv);
    SourceLogicalUv = clamp(SourceLogicalUv, SourceInset.xy, 1.0 - SourceInset.xy);
    float2 BaseUv = SourceRect.zw + SourceLogicalUv * SourceRect.xy;
    float2 MaskLogicalUv = frac(Input.Uv * Values.z);
    MaskLogicalUv = clamp(MaskLogicalUv, MaskInset.xy, 1.0 - MaskInset.xy);
    float2 MaskUv = MaskRect.zw + MaskLogicalUv * MaskRect.xy;
    float Mask = Options.x > 0.5 ? MaskTexture.Sample(WrapSampler, MaskUv).r : saturate(Noise(MaskUv * 4.0) * 0.5 + Noise(MaskUv * 16.0) * 0.35 + Noise(MaskUv * 64.0) * 0.15);
    float Dissolve = IsRibbon > 0.5 ? Input.Dissolve : Values.x;
    if (Values.y > 0.5) { if (Dissolve >= 1.0) discard; clip(Mask - Dissolve); }
    float EdgeAmount = 0.0;
    if (Values.y > 0.5 && Dissolve > 0.0 && Dissolve < 1.0 && Edge.a > 0.0) EdgeAmount = 1.0 - smoothstep(0.0, Edge.a, Mask - Dissolve);
    float4 Source = SourceTexture.Sample(WrapSampler, BaseUv);
    float4 PixelTint = IsRibbon > 0.5 ? Input.Color : Tint;
    float Emission = IsRibbon > 0.5 ? Input.Emission : Values.w;
    if (Source.a * PixelTint.a * Options.y <= 0.0) discard;
    float3 Emissive = Source.rgb * PixelTint.rgb * Emission + Edge.rgb * EdgeAmount;
    float3 Rgb = min(Emissive * BrightBlend.rgb, 1.0);
    float Alpha = Source.a * PixelTint.a * Options.y;
    return float4(Rgb, Alpha);
}
