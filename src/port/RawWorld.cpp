#include "port/RawWorld.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <unordered_set>
#include <vector>

namespace {
struct C{uint32_t id,n,p,e;};
struct L{uint32_t id,p,e;};
struct I{uint32_t key;char name[25];M11Vec3 p;float r[9];};
struct M{unsigned s,nv,nt;uint32_t fx;};

static uint32_t al(uint32_t x,uint32_t a){return (x+a-1)&~(a-1);}
static bool rd(FILE*f,uint32_t o,void*d,size_t n){return !fseek(f,(long)o,SEEK_SET)&&fread(d,1,n,f)==n;}
static uint32_t u32(const unsigned char*p){uint32_t v;memcpy(&v,p,4);return v;}
static int16_t i16(const unsigned char*p){int16_t v;memcpy(&v,p,2);return v;}
static float f32(const unsigned char*p){float v;memcpy(&v,p,4);return v;}
static bool ch(FILE*f,uint32_t o,uint32_t z,C&c){
 unsigned char h[8];if(z-o<8||!rd(f,o,h,8))return false;
 c={u32(h),u32(h+4),o+8,0};uint64_t e=(uint64_t)c.p+c.n;
 if(e>z||e<=o)return false;c.e=(uint32_t)e;return true;
}
static bool leaves(FILE*f,uint32_t a,uint32_t z,std::vector<L>&v,int d=0){
 if(d>12)return false;for(uint32_t p=a;p<z;){C c;if(!ch(f,p,z,c))return false;
  if(c.id==0x80134100){if(!leaves(f,c.p,c.e,v,d+1))return false;}else v.push_back({c.id,c.p,c.e});p=c.e;
 }return true;
}
static void name24(char*out,const unsigned char*p,size_t n){
 size_t i=0;for(;i<24&&i<n&&p[i];i++)out[i]=(p[i]>=32&&p[i]<=126)?(char)p[i]:'?';out[i]=0;
}
static bool section(FILE*f,uint32_t fs,int want,std::vector<I>&out){
 for(uint32_t p=0;p<fs;){C t;if(!ch(f,p,fs,t))return false;
  if(t.id==0x80034100){
   int sid=-1;uint32_t ia=0,iz=0,xa=0,xz=0;
   for(uint32_t q=t.p;q<t.e;){C c;if(!ch(f,q,t.e,c))return false;
    if(c.id==0x34101&&c.n>=16){unsigned char b[16];if(!rd(f,c.p,b,16))return false;memcpy(&sid,b+12,4);}
    else if(c.id==0x34102){ia=c.p;iz=c.e;}else if(c.id==0x34103){xa=c.p;xz=c.e;}q=c.e;
   }
   if(sid==want){
    if(!ia||!xa||(iz-ia)%72)return false;size_t ni=(iz-ia)/72;struct Info{char n[25];uint32_t k;};
    std::vector<Info> info(ni);unsigned char b[72];
    for(size_t k=0;k<ni;k++){if(!rd(f,ia+k*72,b,72))return false;name24(info[k].n,b,24);info[k].k=u32(b+24);}
    xa=al(xa,16);if(xa>xz||(xz-xa)%64)return false;unsigned char x[64];
    for(uint32_t q=xa;q<xz;q+=64){if(!rd(f,q,x,64))return false;int j=i16(x+62);if(j<0||(size_t)j>=ni)return false;
     I a{};a.key=info[j].k;strncpy(a.name,info[j].n,24);a.p={f32(x+32),f32(x+36),f32(x+40)};
     for(int k=0;k<9;k++)a.r[k]=i16(x+44+k*2)/8192.f;out.push_back(a);
    }return !out.empty();
   }
  }p=t.e;
 }return false;
}
static bool identity(FILE*f,const C&o,uint32_t&key,char name[25]){
 std::vector<L>v;if(!leaves(f,o.p,o.e,v))return false;for(auto&a:v)if(a.id==0x134011){
  uint32_t s=al(a.p,16);if(a.e-s<161)return false;unsigned char b[256];size_t n=std::min<size_t>(256,a.e-s);
  if(!rd(f,s,b,n)||b[12]!=0x16)return false;key=u32(b+16);name24(name,b+160,n-160);return true;
 }return false;
}
static M11Vec3 xf(const M11Vec3&v,const I&i){
 float a[3]={v.x,v.y,v.z},p[3]={i.p.x,i.p.y,i.p.z},o[3]={};
 for(int k=0;k<3;k++){o[k]=p[k];for(int j=0;j<3;j++)o[k]+=a[j]*i.r[j*3+k];}
 return {o[0],o[1],o[2]};
}
static float norm(const M11Triangle&t,float&x,float&y,float&z){
 float ax=t.p[1].x-t.p[0].x,ay=t.p[1].y-t.p[0].y,az=t.p[1].z-t.p[0].z;
 float bx=t.p[2].x-t.p[0].x,by=t.p[2].y-t.p[0].y,bz=t.p[2].z-t.p[0].z;
 x=ay*bz-az*by;y=az*bx-ax*bz;z=ax*by-ay*bx;return sqrtf(x*x+y*y+z*z);
}
static uint32_t col(const char*n,float x,float y,float z,float m){
 int r=166,g=148,b=112;if(!strncmp(n,"TRN_CT_ROAD",11)){r=g=83;b=73;}
 else if(strstr(n,"TREE")||strstr(n,"GRASS")||strstr(n,"BUSH")){r=107;g=111;b=62;}
 float s=.65f+.35f*fabsf((x*.3f+y*.4f+z*.866f)/std::max(.001f,m));
 r=std::min(255,(int)(r*s));g=std::min(255,(int)(g*s));b=std::min(255,(int)(b*s));
 return 0xff000000u|r|(g<<8)|(b<<16);
}
static bool solid(FILE*f,const C&o,const I&i,M11World&w){
 std::vector<L>ls;if(!leaves(f,o.p,o.e,ls))return false;std::vector<L>vb;L ma{},ix{};char nm[25]{};uint32_t key=0;
 for(auto&a:ls){if(a.id==0x134b01)vb.push_back(a);else if(a.id==0x134b02)ma=a;else if(a.id==0x134b03)ix=a;
  else if(a.id==0x134011){uint32_t s=al(a.p,16);unsigned char b[256];size_t n=std::min<size_t>(256,a.e-s);
   if(n>=161&&rd(f,s,b,n)&&b[12]==0x16){key=u32(b+16);name24(nm,b+160,n-160);}}
 }
 if(key!=i.key)return true;if(!ma.p||!ix.p||vb.empty())return false;
 if(!strncmp(nm,"SHD",3)||!strncmp(nm,"SHADOW",6)||strstr(nm,"TRACKBARRIERPLAYER"))return true;
 uint32_t ms=al(ma.p,16);if(ms>ma.e||(ma.e-ms)%104)return false;std::vector<M>m;unsigned st=0;bool have=0;uint32_t last=0;unsigned char b[104];
 for(uint32_t p=ms;p<ma.e;p+=104){if(!rd(f,p,b,104))return false;uint32_t fx=u32(b+48);if(have&&fx!=last)st++;have=1;last=fx;m.push_back({st,u32(b+60),u32(b+64),fx});}
 std::vector<uint32_t>cnt(vb.size());for(auto&a:m){if(a.s>=cnt.size())return false;cnt[a.s]+=a.nv;}
 std::vector<std::vector<M11Vec3>>vv(vb.size());unsigned char xyz[12];
 for(size_t s=0;s<vb.size();s++){uint32_t a=al(vb[s].p,128);if(a>vb[s].e||!cnt[s]||(vb[s].e-a)%cnt[s])return false;uint32_t stride=(vb[s].e-a)/cnt[s];
  if(stride!=24&&stride!=36&&stride!=44&&stride!=60)return false;vv[s].resize(cnt[s]);
  for(uint32_t k=0;k<cnt[s];k++){if(!rd(f,a+k*stride,xyz,12))return false;vv[s][k]={f32(xyz),f32(xyz+4),f32(xyz+8)};}
 }
 uint32_t at=al(ix.p,16);bool road=!strncmp(nm,"TRN_CT_ROAD",11);for(auto&a:m){uint64_t bytes=(uint64_t)a.nt*6;if(bytes>ix.e-at)return false;
  for(uint32_t k=0;k<a.nt;k++){unsigned char q[6];if(!rd(f,at+k*6,q,6))return false;uint16_t id[3];memcpy(&id[0],q,2);memcpy(&id[1],q+2,2);memcpy(&id[2],q+4,2);
   if(id[0]>=vv[a.s].size()||id[1]>=vv[a.s].size()||id[2]>=vv[a.s].size())return false;M11Triangle t{};
   for(int j=0;j<3;j++)t.p[j]=xf(vv[a.s][id[j]],i);float nx,ny,nz,mg=norm(t,nx,ny,nz);if(mg<.001f)continue;t.color=col(nm,nx,ny,nz,mg);
   if(w.world.size()>=36000)return false;w.world.push_back(t);if(road&&fabsf(nz)/mg>.8f){if(w.road.size()>=8000)return false;w.road.push_back(t);}
  }at+=(uint32_t)bytes;
 }w.object_count++;return true;
}
}

M11World LoadM11RockportSection(const char*path,int sid){
 M11World w;FILE*f=fopen(path,"rb");if(!f){w.error="STREAML2RA.BUN missing";return w;}fseek(f,0,SEEK_END);long n=ftell(f);if(n<=0){w.error="STREAML2RA size";fclose(f);return w;}uint32_t fs=(uint32_t)n;
 std::vector<I>in;if(!section(f,fs,sid,in)){w.error="Rockport section 101 parse failed";fclose(f);return w;}w.instance_count=in.size();std::unordered_set<uint32_t>need;for(auto&a:in)need.insert(a.key);
 for(uint32_t p=0;p<fs;){C t;if(!ch(f,p,fs,t)){w.error="world chunk tree";fclose(f);return w;}if(t.id==0x80134000)for(uint32_t q=t.p;q<t.e;){C o;if(!ch(f,q,t.e,o)){w.error="geometry tree";fclose(f);return w;}
   if(o.id==0x80134010){uint32_t key;char nm[25];if(identity(f,o,key,nm)&&need.count(key))for(auto&a:in)if(a.key==key&&!solid(f,o,a,w)){w.error="Rockport solid decode failed";fclose(f);return w;}}
   q=o.e;}p=t.e;
 }fclose(f);if(w.world.empty()||w.road.empty()){w.error="Rockport section has no drivable geometry";return w;}
 auto&s=w.road[0];w.origin={(s.p[0].x+s.p[1].x+s.p[2].x)/3,(s.p[0].y+s.p[1].y+s.p[2].y)/3,(s.p[0].z+s.p[1].z+s.p[2].z)/3};
 auto loc=[&](std::vector<M11Triangle>&v){for(auto&t:v)for(auto&p:t.p){p.x-=w.origin.x;p.y-=w.origin.y;p.z-=w.origin.z;}};loc(w.world);loc(w.road);w.valid=1;return w;
}

bool M11Ground(const M11World&w,float x,float y,float pz,float*z){
 float best=4;bool ok=0;for(auto&t:w.road){auto&a=t.p[0];auto&b=t.p[1];auto&c=t.p[2];float d=(b.y-c.y)*(a.x-c.x)+(c.x-b.x)*(a.y-c.y);if(fabsf(d)<.001f)continue;
  float u=((b.y-c.y)*(x-c.x)+(c.x-b.x)*(y-c.y))/d,v=((c.y-a.y)*(x-c.x)+(a.x-c.x)*(y-c.y))/d;if(u<-.0001f||v<-.0001f||u+v>1.0001f)continue;
  float h=u*a.z+v*b.z+(1-u-v)*c.z,dd=fabsf(h-pz);if(dd<best){best=dd;*z=h;ok=1;}}
 return ok;
}
