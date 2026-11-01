// Catalyst surface appearance profile 1. Embedded into both native and preview materials.
// Coordinates are centimetres; 3D noise avoids UV seams on curved worlds.
struct CLSurfaceAppearanceShader
{
    float Hash(float3 p)
    {
        p=frac(p*0.1031);p+=dot(p,p.yzx+33.33);return frac((p.x+p.y)*p.z);
    }
    float Noise(float3 p)
    {
        float3 i=floor(p),f=frac(p);f=f*f*(3-2*f);
        return lerp(lerp(lerp(Hash(i),Hash(i+float3(1,0,0)),f.x),lerp(Hash(i+float3(0,1,0)),Hash(i+float3(1,1,0)),f.x),f.y),lerp(lerp(Hash(i+float3(0,0,1)),Hash(i+float3(1,0,1)),f.x),lerp(Hash(i+float3(0,1,1)),Hash(i+float3(1,1,1)),f.x),f.y),f.z);
    }
    void Shade(float3 P,float3 N,float3 Geometry,float4 Tint,float4 Detail,out float3 Color,out float Roughness,out float3 Normal)
    {
        float grain=max(Detail.x,1),strength=saturate(Detail.y);
        float a=Noise(P/grain),b=Noise(P/(grain*4.7)+13.1),macro=Noise(P/420+7.2);
        float flecks=smoothstep(.25,.75,a);float variation=(flecks-.5)*strength+(b-.5)*.18+(macro-.5)*.22;
        Color=saturate(Tint.rgb*(1+variation));Roughness=saturate(Tint.a+(a-.5)*.12);
        float H=(a*.7+b*.3)*grain*.06;
        float3 dx=ddx(Geometry),dy=ddy(Geometry),r1=cross(dy,N),r2=cross(N,dx);float det=dot(dx,r1);
        float3 grad=(ddx(H)*r1+ddy(H)*r2)*sign(det)/max(abs(det),.00001);
        Normal=normalize(N-grad*saturate(Detail.z));
    }
};
