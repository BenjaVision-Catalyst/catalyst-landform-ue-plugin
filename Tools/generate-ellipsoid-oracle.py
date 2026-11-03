#!/usr/bin/env python3
"""Generate independent 70-digit geometric references; no PROJ or runtime code.
The normal defines a supporting plane of the quadratic figure. Its contact point
is D*n/sqrt(n^T D n), D=diag(a*a,a*a,b*b). Height then follows that normal.
"""
import argparse,json
from decimal import Decimal as D,localcontext
from pathlib import Path
PI=D('3.141592653589793238462643383279502884197169399375105820974944592307816406286208998628')

def sin(x):
    term=x;total=x
    for k in range(1,130):
        term *= -x*x / D(2*k*(2*k+1));total+=term
    return total

def cos(x):return sin(PI/2-x)

def generate():
    with localcontext() as ctx:
        ctx.prec=70
        figures=[('sphere-100','100','100'),('sphere-1000','1000','1000'),('oblate-1000','1000','990'),
                 ('mars-test','3396190','3376200'),('sphere-earth','6371000','6371000'),
                 ('wgs84','6378137',str(D('6378137')*(1-1/D('298.257223563'))))]
        sites=[('0','0'),('0','90'),('0','-90'),('0','180'),('0','-180'),('45','45'),('-37.8136','144.9631'),
               ('80','-123.75'),('-80','123.75'),('89.999999','179.999999'),('-89.999999','-179.999999'),
               ('90','0'),('-90','145'),('31.2','179.999999'),('31.2','-179.999999')]
        result={'schema':1,'oracle':'70-digit decimal supporting-plane quadratic contact; independent of PROJ',
                'axes':'X longitude 0, Y 90 east, Z north; right-handed metres',
                'heightReference':'ellipsoid-normal-metres','positionBudgetMeters':.001,'normalBudget':1e-9,'figures':[]}
        for name,aa,bb in figures:
            a,b=D(aa),D(bb);band=min(D(10000),b/(D(10) if a==b else D(50)));samples=[]
            for lat,lon in sites:
                phi,lam=D(lat)*PI/180,D(lon)*PI/180
                n=[cos(phi)*cos(lam),cos(phi)*sin(lam),sin(phi)]
                # Exact axes: avoid residuals from the finite Taylor sum at poles.
                if abs(D(lat))==90:n=[D(0),D(0),D(1) if D(lat)>0 else D(-1)]
                root=(a*a*(n[0]*n[0]+n[1]*n[1])+b*b*n[2]*n[2]).sqrt()
                s=[a*a*n[0]/root,a*a*n[1]/root,b*b*n[2]/root]
                for h in [D(0),D('.001'),D('-.001'),band/D(2),-band/D(2),band-D('.001'),-band+D('.001')]:
                    p=[s[i]+h*n[i] for i in range(3)]
                    samples.append({'latitudeDegrees':float(lat),'longitudeEastDegrees':float(lon),'heightMeters':float(h),
                                    'bodyMeters':[float(v) for v in p],'normal':[float(v) for v in n]})
            result['figures'].append({'bodyId':name,'frameId':name+'-fixed-v1','datumId':name+'-ellipsoid-v1',
                'equatorialRadiusMeters':float(a),'polarRadiusMeters':float(b),'samples':samples})
        return result

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--output',type=Path,required=True);args=parser.parse_args()
    args.output.write_text(json.dumps(generate(),indent=2,allow_nan=False)+'\n')
