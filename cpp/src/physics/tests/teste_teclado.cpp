#include <cstdio>
#include "vehicle.h"
using namespace phys;
static double bodySlipDeg(const Vehicle& v){ Mat3 R=v.quat.toMat3(); V3 f=R.col(2)*-1, r=R.col(0);
  double vf=v.vel.dot(f), vl=v.vel.dot(r); return std::atan2(vl,std::max(std::fabs(vf),0.1))*180/M_PI; }
static double hdg(const Vehicle& v){ V3 f=v.quat.toMat3().col(2)*-1; return std::atan2(-f.x,-f.z)*180/M_PI; }
struct R{double maxBeta,maxRearSlip,dHead,vEnd,latG;};
// teclado: acelerador/volante binários, ABS e TC desligados
R run(const CarSpec& sp,double kmh,double holdT,double steer,double thr,double totalT){
  FlatGround g(0,1); Vehicle v(sp,&g); v.assists.abs=false; v.assists.tc=false;
  double dt=0.001; Controls c;
  v.assists.autoGearbox=true;
  for(int i=0;i<90000&&v.speed()*3.6<kmh;i++){c.throttle=1;v.step(dt,c);}
  // estabiliza velocidade (cruzeiro)
  for(int i=0;i<1500;i++){c.throttle = v.speed()*3.6<kmh?0.6:0.1; v.step(dt,c);}
  double h0=hdg(v); R r{0,0,0,0,0};
  for(int i=0;i<(int)(totalT/dt);i++){
    double t=i*dt; c.steer = t<holdT? steer:0; c.throttle=thr; c.brake=0;
    v.step(dt,c);
    r.maxBeta=std::max(r.maxBeta,std::fabs(bodySlipDeg(v)));
    r.maxRearSlip=std::max(r.maxRearSlip,std::fabs(v.wheels[2].slipAngle)*180/M_PI);
    r.latG=std::max(r.latG,std::fabs(v.accel.x)/9.81);
  }
  double dh=hdg(v)-h0; while(dh>180)dh-=360; while(dh<-180)dh+=360; r.dHead=dh; r.vEnd=v.speed()*3.6; return r;
}
int main(){
  struct S{const char* n; double kmh,hold,steer,thr,tot;};
  S sc[]={{"40km/h segura volante (acel total)",40,1.5,1,1,3},{"80km/h segura volante (acel total)",80,1.5,1,1,3},
          {"150km/h toque curto 0.15s (curva de leve)",150,0.15,1,0.5,3},{"200km/h toque curto 0.15s",200,0.15,1,0.5,3},
          {"200km/h segura volante 1.5s",200,1.5,1,1,3},{"120km/h esq/dir (troca)",120,0.5,1,0.5,3}};
  CarSpec* sp[]={&FERRARI_296_GT3,&HONDA_CIVIC_TYPE_R};
  for(CarSpec* s:sp){ printf("== %s\n",s->name.c_str());
    for(auto&x:sc){ R r=run(*s,x.kmh,x.hold,x.steer,x.thr,x.tot);
      printf(" %-44s derrapagem max=%5.1f deg  slipTras=%5.1f  rumo=%+7.1f  latG=%.2f  v=%.0f %s\n",x.n,r.maxBeta,r.maxRearSlip,r.dHead,r.latG,r.vEnd, r.maxBeta>35?" <<< RODOU":""); } }
}
