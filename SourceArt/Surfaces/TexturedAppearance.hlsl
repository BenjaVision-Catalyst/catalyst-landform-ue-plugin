// Profile 2: planet-local triplanar PBR, shared by VPP and flattened Designer meshes.
struct CLTexturedAppearanceShader
{
    float3 Perturb(float3 N,float3 G,float2 UV,float3 Map,float Strength)
    {
        float3 dx=ddx(G),dy=ddy(G),r1=cross(dy,N),r2=cross(N,dx);
        float2 ux=ddx(UV),uy=ddy(UV);
        float3 T=ux.x*r1+uy.x*r2,B=ux.y*r1+uy.y*r2;
        float scale=rsqrt(max(max(dot(T,T),dot(B,B)),1.e-16));
        return normalize(N*max(Map.z,.25)+(T*Map.x+B*Map.y)*scale*Strength);
    }
    void Shade(float3 P,float3 N,float3 G,float4 Tint,float4 Detail,float4 TextureDetail,
        Texture2D Albedo,Texture2D NormalMap,Texture2D RoughMap,
        out float3 Color,out float Roughness,out float3 Normal)
    {
        CLSurfaceAppearanceShader Legacy;
        if(TextureDetail.y<.5){Legacy.Shade(P,N,G,Tint,Detail,Color,Roughness,Normal);return;}
        float3 SN=normalize(cross(ddx(P),ddy(P))+1.e-12);
        float3 W=pow(abs(SN),4);W/=max(dot(W,1),1.e-5);
        float3 UV=P/max(TextureDetail.x,1);
        float3 C=0,NN=0;float R=0;
        // One shared wrap sampler avoids multiplying sampler usage across surface blends.
        float2 U[3]={UV.yz,UV.xz,UV.xy};
        [unroll]for(int I=0;I<3;++I)
        {
            C+=Texture2DSample(Albedo,View.MaterialTextureBilinearWrapedSampler,U[I]).rgb*W[I];
            R+=Texture2DSample(RoughMap,View.MaterialTextureBilinearWrapedSampler,U[I]).r*W[I];
            float3 M=UnpackNormalMap(Texture2DSample(NormalMap,View.MaterialTextureBilinearWrapedSampler,U[I])).xyz;
            NN+=Perturb(N,G,U[I],M,Detail.z)*W[I];
        }
        float macro=Legacy.Noise(P/(TextureDetail.x*12)+7.2);
        Color=C*Tint.rgb*(.86+.28*macro);Roughness=saturate(R*Tint.a);Normal=normalize(NN);
    }
    float Line(float Angle,float Step,float Pixel)
    {
        float D=abs(frac(Angle/Step+.5)-.5)*Step;
        float A=1-smoothstep(.35*Pixel,.95*Pixel,D);
        return A*saturate(Step/max(Pixel,1.e-5)-2);
    }
    void Grid(float3 P,float3 Eye,float4 Settings,inout float3 Color,out float3 Emissive)
    {
        Emissive=0;if(Settings.x<.5)return;
        float Radius=max(length(P),1),Step=Settings.y;
        // Globe cameras sit outside the body; nearby surface cameras retain 1-degree lines.
        if(Step<.5)Step=length(Eye)>Radius*2?10:1;
        float3 D=P/Radius;float Lat=asin(clamp(D.z,-1,1))*57.2957795;
        float Lon=atan2(D.y,D.x)*57.2957795;
        float Foot=max(max(length(ddx(D)),length(ddy(D))),1.e-6)*57.2957795;
        float Latitude=Line(Lat,Step,Foot);
        float Longitude=Line(Lon,Step,Foot/max(length(D.xy),.03))*smoothstep(.01,.06,length(D.xy));
        float Grid=max(Latitude,Longitude)*.42;
        float Equator=1-smoothstep(Foot,Foot*2.2,abs(Lat));
        float Pole=1-smoothstep(Step*.12+Foot,Step*.12+Foot*2.5,90-abs(Lat));
        float3 Ink=float3(.14,.32,.4);float Alpha=Grid;
        Ink=lerp(Ink,float3(.8,.38,.04),Equator);Alpha=max(Alpha,Equator*.86);
        Ink=lerp(Ink,D.z>0?float3(.8,.12,.08):float3(.06,.22,.65),Pole);Alpha=max(Alpha,Pole*.9);
        Color=lerp(Color,Ink,Alpha);Emissive=Ink*Alpha*.1;
    }
};
