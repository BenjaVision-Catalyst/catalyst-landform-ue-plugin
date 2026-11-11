#include "Nodes/VoxelNoiseNodesImpl.isph"
// Landform's spatial dissolution field. VPP owns every mesh and collider.
static inline double ss(double lo,double hi,double v) {double t=clamp((v-lo)/(hi-lo),0.0d,1.0d);return t*t*(3.0d - 2.0d*t);}
static inline double sm(double a,double b,uniform double k) {double h=clamp(.5d + .5d*(b-a)/k,0.0d,1.0d);return b+(a-b)*h-k*h*(1.0d - h);}
// Native noise is already float. SIMD transcendental evaluation is qualified
// against the original double scalar field, including floor/contact probes.
static inline double sn(double x) {return (double)sin((float)x);}
static inline double cs(double x) {return (double)cos((float)x);}
static inline double ex(double x) {return (double)exp((float)x);}
static inline double sq(double x) {return x*x;}
static inline double hill(double x,double y,uniform double cx,uniform double cy,uniform double rx,uniform double ry) {return ex(-sq((x-cx)/rx)-sq((y-cy)/ry));}
export void CLSpatialMorph(
    uniform const double qx[],uniform const double qy[],uniform const double qz[],
    uniform const float noise[],uniform const double vx[],uniform const double vy[],uniform const int starts[],uniform const int counts[],uniform int regions,
    uniform const double contacts[],uniform int numContacts,uniform const double morphology[],uniform const double contactz[],uniform const int groups[],uniform const int sources[],uniform int generated,uniform double air[],uniform int num)
{
    foreach(i=0 ... num)
    {
        double x=qx[i],y=qy[i],z=qz[i],b=(double)noise[i],bed=(double)noise[num+i],detail=(double)noise[2*num+i];
        double wx=x+2.8d*b+.65d*sn(y*.15d + z*.20d),wy=y+4.0d*b+1.8d*bed,plan=-1.e300d;
        if(generated)
        {
            double cd=1.e300d;int low=0,high=numContacts;
            while(low<high){int m=(low+high)/2;if(contacts[m]<contactz[i])low=m+1;else high=m;}
            if(low<numContacts)cd=abs(contacts[low]-contactz[i]);if(low>0)cd=min(cd,abs(contacts[low-1]-contactz[i]));
            double relief=(.25d + .30d*clamp(b*2.0d+bed,0.0d,1.0d))*ex(-sq(cd/.40d))+.35d*bed;
            uniform double phase=morphology[0];
            double rolling=-.004d*x+1.0d*sn(x*.035d+y*.021d+phase)+.60d*cs(y*.07d-x*.018d+phase*.7d)+.10d*sn(x*.21d+y*.19d)
                +3.0d*hill(x,y,morphology[1],morphology[2],35.0d,22.0d)+2.1d*hill(x,y,morphology[1]-65.0d,morphology[2]-31.0d,30.0d,17.0d)+2.7d*hill(x,y,morphology[1]+75.0d,morphology[2]+36.0d,32.0d,20.0d)-.8d*ex(-sq((y-8.0d*sn(x*.012d+phase))/8.0d));
            double field=-1.e300d,bodies[9],datums[9];
            for(uniform int g=0;g<generated;++g)
            {
                uniform const double* uniform f=morphology+4+28*sources[g];
                double t=clamp(((x-f[2])*f[4]+(y-f[3])*f[5])/f[6],0.0d,1.0d);
                if(f[1]!=0.0d)t+=.6d*sn(6.283185307179586d*t)/6.283185307179586d;
                datums[g]=f[0]+f[1]*ss(0.0d,1.0d,t);bodies[g]=-1.e300d;
            }
            for(uniform int r=0;r<regions;++r)
            {
                uniform const double* uniform m=morphology+4+28*r;uniform int g=groups[r];
                double datum=datums[g];double h=z-datum;
                double bx=max(max(m[24]-wx,0.0d),wx-m[26]),by=max(max(m[25]-wy,0.0d),wy-m[27]);
                double box=-sqrt(bx*bx+by*by);if(bx==0.0d&&by==0.0d)box=min(min(wx-m[24],m[26]-wx),min(wy-m[25],m[27]-wy));
                double upper=min(box+relief+1.6d,datum+m[7]+m[14]+2.3d*b+bed-z);
                if(upper>bodies[g]-4.0d)
                {
                    double ds=1.e300d;bool inside=false;
                    for(uniform int j=0;j<counts[r];++j)
                    {
                        uniform int a=starts[r]+j,c=starts[r]+(j+1==counts[r]?0:j+1);
                        double dx=vx[c]-vx[a],dy=vy[c]-vy[a],px=wx-vx[a],py=wy-vy[a];
                        double t=clamp((px*dx+py*dy)/(dx*dx+dy*dy),0.0d,1.0d);
                        ds=min(ds,sq(px-t*dx)+sq(py-t*dy));
                        if(((vy[a]>wy)!=(vy[c]>wy))&&(wx<(vx[c]-vx[a])*(wy-vy[a])/(vy[c]-vy[a])+vx[a]))inside=!inside;
                    }
                    double plan=sqrt(ds);if(!inside)plan=-plan;
                    double roof=datum+m[7]+m[14]*hill(x,y,m[8]+m[12],m[9]+m[13],m[10]*.65d,m[11]*.65d)
                        -m[17]*hill(x,y,m[8]+m[15],m[9]+m[16],m[10]*.4d,m[11]*.4d)+2.3d*b+bed;
                    double under=1.6d*ex(-sq((h-5.8d)/2.7d));
                    double v=sm(plan+relief+under,roof-z,5.5d);
                    if(m[18]>0.0d)
                    {
                        double flare=1.0d+sq((h-16.0d)/19.0d);
                        double px=(x-m[19]-1.4d*sn(h*.14d))/(m[21]*flare),py=(y-m[20]+1.6d*cs(h*.11d))/(m[22]*flare);
                        double side=(sqrt(px*px+py*py)-1.0d)*m[21]+1.8d*b+.65d*bed,base=m[23]+1.6d*b+.7d*bed-h;
                        v=sm(v,-sm(-side,-base,5.0d),1.3d);
                    }
                    bodies[g]=-sm(-bodies[g],-v,4.0d);
                }
            }
            for(uniform int g=0;g<generated;++g)
            {
                double floor=datums[g]+rolling;
                field=max(field,sm(bodies[g],z-floor,1.2d)+.14d*detail*ss(.3d,2.0d,z-floor));
            }
            air[i]=field*.6d;
        }
        else
        {
        for(uniform int r=0;r<regions;++r)
        {
            double ds=1.e300d;bool inside=false;
            for(uniform int j=0;j<counts[r];++j)
            {
                uniform int a=starts[r]+j,c=starts[r]+(j+1==counts[r]?0:j+1);
                double dx=vx[c]-vx[a],dy=vy[c]-vy[a],px=wx-vx[a],py=wy-vy[a];
                double t=clamp((px*dx+py*dy)/(dx*dx+dy*dy),0.0d,1.0d);
                ds=min(ds,sq(px-t*dx)+sq(py-t*dy));
                if(((vy[a]>wy)!=(vy[c]>wy))&&(wx<(vx[c]-vx[a])*(wy-vy[a])/(vy[c]-vy[a])+vx[a]))inside=!inside;
            }
            double d=sqrt(ds);if(!inside)d=-d;plan=-sm(-plan,-d,4.0d);
        }
        double room=ss(-24.0d,18.0d,x);
        double roof=10.5d + room*15.5d + 8.0d*hill(x,y,34.0d,-10.0d,24.0d,20.0d)+5.0d*hill(x,y,8.0d,20.0d,21.0d,16.0d)-4.5d*hill(x,y,48.0d,13.0d,15.0d,12.0d)+2.3d*b+bed;
        int low=0,high=numContacts;
        while(low<high){int m=(low+high)/2;if(contacts[m]<z)low=m+1;else high=m;}
        double cd=1.e300d;if(low<numContacts)cd=abs(contacts[low]-z);if(low>0)cd=min(cd,abs(contacts[low-1]-z));
        double relief=(.25d + .30d*clamp(b*2.0d + bed,0.0d,1.0d))*ex(-sq(cd/.40d))+.35d*bed;
        double under=1.6d*ex(-sq((z-5.8d)/2.7d));
        double bank=2.1d*hill(x,y,-53.0d,-9.0d,24.0d,6.0d)+3.3d*hill(x,y,35.0d,24.0d,25.0d,13.0d)+2.7d*hill(x,y,55.0d,-21.0d,18.0d,11.0d);
        double channel=-.9d*ex(-sq((y-1.8d*sn(x*.035d))/4.8d));
        double rolling=.55d*sn(x*.052d + y*.029d)+.32d*cs(y*.12d - x*.018d)+.10d*sn(x*.31d + y*.23d);
        double floor=-.012d*x-1.4d*room+bank+channel+rolling;
        double v=sm(plan+relief+under,roof-z,5.5d);v=sm(v,z-floor,1.2d);
        double flare=1.0d + sq((z-16.0d)/19.0d);double px=(x-39.0d - 1.4d*sn(z*.14d))/(5.0d*flare),py=(y-12.0d + 1.6d*cs(z*.11d))/(7.0d*flare);
        double side=(sqrt(px*px+py*py)-1.0d)*5.0d + 1.8d*b+.65d*bed,base=8.5d + 1.6d*b+.7d*bed - z;
        v=sm(v,-sm(-side,-base,5.0d),1.3d);
        air[i]=(v+.14d*detail*ss(.3d,2.0d,z-floor))*.6d;
        }
    }
}
