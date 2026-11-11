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
    uniform const double contacts[],uniform int numContacts,uniform double air[],uniform int num)
{
    foreach(i=0 ... num)
    {
        double x=qx[i],y=qy[i],z=qz[i],b=(double)noise[i],bed=(double)noise[num+i],detail=(double)noise[2*num+i];
        double wx=x+2.8d*b+.65d*sn(y*.15d + z*.20d),wy=y+4.0d*b+1.8d*bed,plan=-1.e300d;
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
