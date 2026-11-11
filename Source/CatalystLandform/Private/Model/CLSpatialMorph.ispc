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

// Procedural family: independently authored joint footprint and roof field;
// floors are the shared level field or a C1 inter-level ramp, supplied once.
// Profile 9 uses native float arithmetic in its bounded local coordinates.
// Canonical body/bedding coordinates and the profile-8 kernel stay double.
static inline float fss(float lo,float hi,float v){float t=clamp((v-lo)/(hi-lo),0.f,1.f);return t*t*(3.f-2.f*t);}
static inline float fsm(float a,float b,uniform float k){float h=clamp(.5f+.5f*(b-a)/k,0.f,1.f);return b+(a-b)*h-k*h*(1.f-h);}
static inline float fsn(float x){return sin(x);}
static inline float fcs(float x){return cos(x);}
static inline float fex(float x){return exp(x);}
static inline float fsq(float x){return x*x;}
static inline float fhill(float x,float y,uniform float cx,uniform float cy,uniform float rx,uniform float ry){return fex(-fsq((x-cx)/rx)-fsq((y-cy)/ry));}
struct FamilyInputs
{
 float broad,bed,detail;
 float floor1,floor2,floor3,floor4,floor5;
 float warpX,warpY,relief;
};
static inline FamilyInputs familyInputs(float x,float y,float z,float b,float bed,float detail,
 uniform const double contacts[],uniform int numContacts,uniform float phase,uniform float floorScale)
{
 FamilyInputs q;q.broad=b;q.bed=bed;q.detail=detail;
 float fx=x/floorScale,fy=y/floorScale;
 q.floor1=floorScale*.40f*fsn(fx*.025f+phase);q.floor2=floorScale*.35f*fcs(fy*.033f-phase);q.floor3=floorScale*.12f*fsn((fx+fy)*.09f);
 q.floor4=floorScale*1.2f*fhill(fx,fy,48.0f,-32.0f,38.0f,22.0f);q.floor5=floorScale*.35f*fex(-fsq((fy - 3.0f*fsn(fx*.025f))/6.0f));
 q.warpX=2.8f*b+.65f*fsn(y*.15f+z*.20f);q.warpY=4.0f*b+1.8f*bed;
 int low=0,high=numContacts;while(low<high){int m=(low+high)/2;if(contacts[m]<z)low=m+1;else high=m;}
 float cd=1.e30f;if(low<numContacts)cd=abs((float)contacts[low]-z);if(low>0)cd=min(cd,abs((float)contacts[low - 1]-z));
 q.relief=(.25f+.30f*clamp(b*2.0f+bed,0.0f,1.0f))*fex(-fsq(cd/.40f));return q;
}
static inline float familyPoint(float x,float y,float z,FamilyInputs q,
 uniform const double vx[],uniform const double vy[],uniform int vertices,uniform const float edges[],uniform bool compiledEdges,uniform const uint32 masks[],uniform int tileOffset,uniform int width,uniform int heightTiles,uniform const float grid[],uniform const double contacts[],uniform int numContacts,
 uniform float cx,uniform float cy,uniform float height,uniform float scale,uniform float phase,uniform int chamber,
 uniform float ax,uniform float ay,uniform float bx,uniform float by,uniform float fa,uniform float fb)
{
  float b=q.broad,bed=q.bed,detail=q.detail;
  uniform float rampX=bx-ax,rampY=by-ay;
  uniform float len=rampX*rampX+rampY*rampY;
  float rampT=len>0.0f?clamp(((x-ax)*rampX+(y-ay)*rampY)/len,0.0f,1.0f):0.0f;
  rampT=clamp((rampT - .20f)/.60f,0.0f,1.0f);
  float f=fa+(fb-fa)*rampT*rampT*(3.0f - 2.0f*rampT)+q.floor1+q.floor2+q.floor3+q.floor4-q.floor5;
  float wx=x+scale*q.warpX,wy=y+scale*q.warpY;
  float ds=1.e30f;bool inside=false;
  if(compiledEdges)
  {
   int tx=clamp((int)floor((wx-grid[0])*grid[4]),0,width-1),ty=clamp((int)floor((wy-grid[1])*grid[5]),0,heightTiles-1);
   uint32 packed=masks[tileOffset+tx+ty*width];
   // Native bounded queries can extend beyond the indexed planar domain.
   // Such points retain every edge and the full winding test.
   bool outside=(wx<grid[0])|(wy<grid[1])|(wx>grid[0]+(float)width*grid[2])|(wy>grid[1]+(float)heightTiles*grid[3]);
   packed=outside?(0x80000000u|((1u<<vertices)-1u)):packed;
   inside=(packed&0x40000000u)!=0;bool full=(packed&0x80000000u)!=0;
   if(full)inside=false;
   float tileCX=grid[0]+((float)tx+.5f)*grid[2],tileCY=grid[1]+((float)ty+.5f)*grid[3];
   float rx=wx-tileCX,ry=wy-tileCY;
   uniform bool needFull=any(full);
   uniform uint32 selected=0;foreach_active(lane){selected=selected|(extract(packed,lane)&0xffffu);}
   while(selected!=0)
   {
    uniform int j=count_trailing_zeros(selected);selected=selected&(selected - 1);
    uniform int e=j*8;
    uniform float x0=edges[e],y0=edges[e+1],dx=edges[e+2],dy=edges[e+3],inverseLength=edges[e+4],slope=edges[e+5],y1=edges[e+6];
    float px=wx-x0,py=wy-y0;float t=clamp((px*dx+py*dy)*inverseLength,0.0f,1.0f);
    ds=min(ds,fsq(px-t*dx)+fsq(py-t*dy));
    float edgeCX=x0-tileCX,edgeCY=y0-tileCY;
    float crossA=rx*edgeCY-ry*edgeCX,crossB=rx*(edgeCY+dy)-ry*(edgeCX+dx);
    float numerator=edgeCX*dy-edgeCY*dx,denominator=rx*dy-ry*dx;
    bool between=denominator>0.f?((numerator>0.f)&(numerator<=denominator)):((numerator<0.f)&(numerator>=denominator));
    bool crossing=((crossA>0.f)!=(crossB>0.f))&between;
    if(needFull){bool traditional=((y0>wy)!=(y1>wy))&(wx<slope*(wy-y0)+x0);crossing=full?traditional:crossing;}
    inside=inside^crossing;
   }
  }
  else
  {
   for(uniform int j=0;j<vertices;++j)
   {
   uniform int c=j+1==vertices?0:j+1;
   uniform float x0=(float)vx[j],x1=(float)vx[c],y0=(float)vy[j],y1=(float)vy[c];
   uniform float dx=x1-x0,dy=y1-y0;
   float px=wx-x0,py=wy-y0;
   float t=clamp((px*dx+py*dy)/(dx*dx+dy*dy),0.0f,1.0f);
   ds=min(ds,fsq(px-t*dx)+fsq(py-t*dy));
   if(((y0>wy)!=(y1>wy))&&(wx<dx*(wy-y0)/dy+x0))inside=!inside;
   }
  }
  float plan=sqrt(ds);if(!inside)plan=-plan;
  float roof=f+height+scale*(2.0f*b+bed)+height*.12f*fhill(x,y,cx - 8.0f*scale,cy+3.0f*scale,18.0f*scale,14.0f*scale)-height*.16f*fhill(x,y,cx+7.0f*scale,cy - 4.0f*scale,13.0f*scale,11.0f*scale);
  float relief=scale*(q.relief+.35f*bed);
  float under=scale*1.4f*fex(-fsq((z-f - 5.0f*scale)/(2.7f*scale)));
  float v=fsm(plan+relief+under,roof-z,3.0f*scale);v=fsm(v,z-f,.8f*scale);
  if(chamber!=0)
  {
   float px=(x-cx - 5.0f*scale)/(3.5f*scale),py=(y-cy+4.0f*scale)/(4.8f*scale);
   float side=(sqrt(px*px+py*py) - 1.0f)*3.5f*scale+scale*(1.8f*b+.65f*bed),bottom=f+height*.52f+scale*b-z;
   v=fsm(v,-fsm(-side,-bottom,3.0f*scale),.8f*scale);
  }
  return (v+.12f*scale*detail*fss(.3f*scale,2.0f*scale,z-f))*.6f;
}
export void CLFamilyCell(
 uniform const double qx[],uniform const double qy[],uniform const double qz[],
 uniform const float broad[],uniform const float beds[],uniform const float detail[],
 uniform const double vx[],uniform const double vy[],uniform int vertices,
 uniform const double contacts[],uniform int numContacts,
 uniform double cx,uniform double cy,uniform double height,uniform double scale,uniform double phase,uniform double floorScale,uniform int chamber,
 uniform double ax,uniform double ay,uniform double bx,uniform double by,uniform double fa,uniform double fb,
 uniform double air[],uniform int num)
{
 foreach(i=0 ... num) {FamilyInputs q=familyInputs(qx[i],qy[i],qz[i],broad[i],beds[i],detail[i],contacts,numContacts,phase,floorScale);air[i]=familyPoint(qx[i],qy[i],qz[i],q,vx,vy,vertices,NULL,false,NULL,0,0,0,NULL,contacts,numContacts,cx,cy,height,scale,phase,chamber,ax,ay,bx,by,fa,fb);}
}
export void CLFamilyUnion(
 uniform const double qx[],uniform const double qy[],uniform const double qz[],uniform const int enabled[],uniform int seed,
 uniform const double vx[],uniform const double vy[],uniform const float edges[],uniform const uint32 masks[],uniform const int tiles[],uniform const float grids[],uniform const int starts[],uniform const int counts[],uniform const double params[],uniform int cells,uniform const int candidates[],uniform int candidateCount,
 uniform const double contacts[],uniform int numContacts,uniform double phase,uniform double air[],uniform int num)
{
 foreach(i=0 ... num)
 {
  if(enabled[i]==0)continue;
  float x=(float)qx[i],y=(float)qy[i],z=(float)qz[i],best=-1.e30f,nearest=1.e30f;bool hit=false,prepared=false;FamilyInputs q;
  for(uniform int ci=0;ci<candidateCount;++ci)
  {
   uniform int r=candidates[ci];uniform int p=r*18;
   float dx=max(max((float)params[p+12]-x,0.f),x-(float)params[p+15]);
   float dy=max(max((float)params[p+13]-y,0.f),y-(float)params[p+16]);
   float dz=max(max((float)params[p+14]-z,0.f),z-(float)params[p+17]);
   float ds=dx*dx+dy*dy+dz*dz;nearest=min(nearest,ds);
   if(ds==0.f)
   {
    hit=true;
    if(!prepared)
    {
     // Use VPP's actual noise implementation, with the original double-space
     // coordinate arithmetic before conversion to its native float inputs.
     float broad=GetPerlin3D(seed,MakeFloat3((float)(qx[i]/16.0d + 71.0d),(float)(qy[i]/13.0d - 39.0d),(float)(qz[i]/9.0d + 113.0d)));
     float bed=GetPerlin3D(seed,MakeFloat3((float)(qx[i]/4.2d - 31.0d),(float)(qy[i]/4.2d + 51.0d),(float)(qz[i]/2.1d + 19.0d)));
     float detail=GetPerlin3D(seed,MakeFloat3((float)(qx[i]/.65d + 13.0d),(float)(qy[i]/.65d + 91.0d),(float)(qz[i]/.65d - 43.0d)));
     q=familyInputs(x,y,z,broad,bed,detail,contacts,numContacts,phase,params[p+10]);prepared=true;
    }
    float v=familyPoint(x,y,z,q,vx+starts[r],vy+starts[r],counts[r],edges+starts[r]*8,true,masks,tiles[r*3],tiles[r*3+1],tiles[r*3+2],grids+r*6,contacts,numContacts,
      params[p+6],params[p+7],params[p+8],params[p+9],phase,(uniform int)params[p+11],params[p],params[p+1],params[p+2],params[p+3],params[p+4],params[p+5]);
    best=max(best,v);
   }
  }
  if(!hit)
  {
   nearest=1.e30f;
   for(uniform int r=0;r<cells;++r)
   {
    uniform int p=r*18;
    float dx=max(max((float)params[p+12]-x,0.f),x-(float)params[p+15]);
    float dy=max(max((float)params[p+13]-y,0.f),y-(float)params[p+16]);
    float dz=max(max((float)params[p+14]-z,0.f),z-(float)params[p+17]);nearest=min(nearest,dx*dx+dy*dy+dz*dz);
   }
  }
  air[i]=max(air[i],hit?best:-.5f*(sqrt(nearest)+2.f));
 }
}


// Distance to the union of finite cell supports. The address grid retains every
// nearest-box candidate by a triangle-inequality certificate. For P outside the
// grid, clamp P to C on its enclosing box B: d(P,S)^2 >= d(P,B)^2+d(C,S)^2
// for S contained in B. Thus the result remains a conservative lower bound.
static inline float supportDistance(float px,float py,float pz,uniform const float boxes[],uniform const float grid[],uniform const int size[],uniform const uint64 masks[])
{
 float x=clamp(px,grid[0],grid[3]),y=clamp(py,grid[1],grid[4]),z=clamp(pz,grid[2],grid[5]);
 int tx=clamp((int)floor((x-grid[0])/grid[6]),0,size[0]-1),ty=clamp((int)floor((y-grid[1])/grid[7]),0,size[1]-1),tz=clamp((int)floor((z-grid[2])/grid[8]),0,size[2]-1);
 uint64 mask=masks[tx+size[0]*(ty+size[1]*tz)];uniform uint64 selected=0;
 foreach_active(lane){selected=selected|extract(mask,lane);}
 float nearest=1.e30f;
 while(selected!=0)
 {
  uniform int r=count_trailing_zeros(selected);selected=selected&(selected-1);uniform int p=6*r;
  float dx=max(max(boxes[p]-x,0.f),x-boxes[p+3]),dy=max(max(boxes[p+1]-y,0.f),y-boxes[p+4]),dz=max(max(boxes[p+2]-z,0.f),z-boxes[p+5]);nearest=min(nearest,dx*dx+dy*dy+dz*dz);
  if(all(nearest==0.f))break;
 }
 float dx=px-x,dy=py-y,dz=pz-z;return max(0.f,sqrt(nearest+dx*dx+dy*dy+dz*dz)-.001f);
}
export void CLCellSupportDistances(uniform const double bx[],uniform const double by[],uniform const double bz[],
 uniform const float boxes[],uniform const float grid[],uniform const int size[],uniform const uint64 masks[],uniform int cells,uniform double out[],uniform int num)
{foreach(i=0 ... num)out[i]=(double)supportDistance((float)bx[i],(float)by[i],(float)bz[i],boxes,grid,size,masks);}

export void CLFamilyPrepare(uniform const double bx[],uniform const double by[],uniform const double bz[],
 uniform const float boxes[],uniform const float grid[],uniform const int size[],uniform const uint64 masks[],
 uniform const double frame[],uniform const int planarSize[],uniform const double planarDistance[],uniform const double nodes[],uniform const int links[],uniform const int lookup[],
 uniform double outX[],uniform double outY[],uniform double outZ[],uniform int enabled[],uniform double air[],uniform double bounds[],uniform int counts[],uniform int num)
{
 varying double minX=1.e300d,minY=1.e300d,minZ=1.e300d,maxX=-1.e300d,maxY=-1.e300d,maxZ=-1.e300d;uniform int activeCount=0,exactCount=0;
 foreach(i=0 ... num)
 {
  double px=bx[i],py=by[i],pz=bz[i];enabled[i]=0;
  double support=(double)supportDistance((float)px,(float)py,(float)pz,boxes,grid,size,masks);
  if(support>.001d){air[i]=max(air[i],-support);continue;}
  double dx=px-frame[0],dy=py-frame[1],dz=pz-frame[2];
  double x=dx*frame[3]+dy*frame[4]+dz*frame[5],y=dx*frame[6]+dy*frame[7]+dz*frame[8];
  if((x>frame[13])&(y>frame[14])&(x<frame[15])&(y<frame[16]))
  {
   int tx=clamp((int)floor((x-frame[13])/frame[17]),0,planarSize[0]-1),ty=clamp((int)floor((y-frame[14])/frame[18]),0,planarSize[1]-1);
   double bound=planarDistance[tx+ty*planarSize[0]]-frame[19];
   if(bound>2.0d){air[i]=max(air[i],-.6d*bound);continue;}
  }
  double along=px*frame[9]+py*frame[10]+pz*frame[11];
  double u=(px*frame[3]+py*frame[4]+pz*frame[5])*(frame[12]/along),v=(px*frame[6]+py*frame[7]+pz*frame[8])*(frame[12]/along);
  int state=2;double z=0.0d;
  if((along>1.e-6d)&(u>nodes[0])&(v>nodes[1])&(u<nodes[0]+nodes[2])&(v<nodes[1]+nodes[3]))
  {
   int tx=clamp((int)floor((u-nodes[0])/nodes[2]*1024.0d),0,1023),ty=clamp((int)floor((v-nodes[1])/nodes[3]*1024.0d),0,1023);int n=lookup[tx+ty*1024];
   while(links[n]>=0)
   {
    int p=n*20;double mx=nodes[p]+nodes[p+2]*.5d,my=nodes[p+1]+nodes[p+3]*.5d;
    n=links[n]+(int)(u>=mx)+2*(int)(v>=my);
   }
   if(links[n]==-1)
   {
    double offset;uniform int common=0;foreach_active(lane){common=extract(n,lane);}
    if(all(n==common))
    {
     // Coherent lanes share a polynomial: load its coefficients once.
     uniform int p=common*20;double tx=(u-nodes[p])/nodes[p+2],ty=(v-nodes[p+1])/nodes[p+3];double rows[4];
     for(uniform int row=0;row<4;++row){uniform int c=p+4+row*4;rows[row]=nodes[c]+tx*(nodes[c+1]+tx*(nodes[c+2]+tx*nodes[c+3]));}
     offset=rows[0]+ty*(rows[1]+ty*(rows[2]+ty*rows[3]));
    }
    else
    {
     int p=n*20;double tx=(u-nodes[p])/nodes[p+2],ty=(v-nodes[p+1])/nodes[p+3];double rows[4];
     for(uniform int row=0;row<4;++row){int c=p+4+row*4;rows[row]=nodes[c]+tx*(nodes[c+1]+tx*(nodes[c+2]+tx*nodes[c+3]));}
     offset=rows[0]+ty*(rows[1]+ty*(rows[2]+ty*rows[3]));
    }
    z=sqrt(px*px+py*py+pz*pz)-offset-frame[12];state=1;
   }
  }
  outX[i]=x;outY[i]=y;outZ[i]=z;enabled[i]=state;
  minX=min(minX,x);minY=min(minY,y);minZ=min(minZ,z);maxX=max(maxX,x);maxY=max(maxY,y);maxZ=max(maxZ,z);
  activeCount+=reduce_add((int)(state>0));exactCount+=reduce_add((int)(state==2));
 }
 bounds[0]=reduce_min(minX);bounds[1]=reduce_min(minY);bounds[2]=reduce_min(minZ);bounds[3]=reduce_max(maxX);bounds[4]=reduce_max(maxY);bounds[5]=reduce_max(maxZ);counts[0]=activeCount;counts[1]=exactCount;
}
