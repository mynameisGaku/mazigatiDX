// SPDX-License-Identifier: MIT
#if defined(INVERT)
#define FIN(c) float4(1.0-(c).rgb,(c).a)
#elif defined(MULA)
#define FIN(c) float4(lerp(float3(1.0,1.0,1.0),(c).rgb,(c).a),(c).a)
#else
#define FIN(c) (c)
#endif
struct V { float4 p:POSITION; float4 c:COLOR; float2 uv:TEXCOORD; }; struct P { float4 p:SV_POSITION; float4 c:COLOR; float2 uv:TEXCOORD; }; P VS(V v) { P p; p.p=v.p; p.c=v.c; p.uv=v.uv; return p; } float4 PS(P p):SV_TARGET { return FIN(p.c); } Texture2D image:register(t0); SamplerState sampling:register(s0); float4 TexturePS(P p):SV_TARGET { float4 color=image.Sample(sampling,p.uv)*p.c; if(color.a<=0) discard; return FIN(color); }Texture2D maskImage:register(t1); cbuffer cbMask:register(b0) { uint4 maskInfo; }; float MaskValue(float4 position) { uint w,h; maskImage.GetDimensions(w,h); uint2 q=uint2(position.xy); if(q.x>=w||q.y>=h) { return 1.0; } float4 t=maskImage.Load(int3(q,0)); float m=maskInfo.x==0?t.a:(maskInfo.x==1?t.r:(maskInfo.x==2?t.g:t.b)); return maskInfo.y!=0?1.0-m:m; } float4 MaskedPS(P p):SV_TARGET { float4 color=p.c; color.a*=MaskValue(p.p); return FIN(color); } float4 MaskedTexturePS(P p):SV_TARGET { float4 color=image.Sample(sampling,p.uv)*p.c; color.a*=MaskValue(p.p); if(color.a<=0) discard; return FIN(color); }