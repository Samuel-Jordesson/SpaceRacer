#include <cstdio>
#include "vehicle.h"
using namespace phys;
static double hdg(const Vehicle& v){ V3 f=v.quat.toMat3().col(2)*-1; return std::atan2(-f.x,-f.z)*180/M_PI; }
int main(){
  FlatGround g(0,1); double dt=0.001;
  for(CarSpec* sp:{&FERRARI_296_GT3,&PORSCHE_992_GT3_R,&BMW_M4_GT3_EVO,&LAMBORGHINI_FENOMENO,&HONDA_CIVIC_TYPE_R}){
    // 1) arrancada reta, TC off: nao pode acionar estabilidade nem desviar
    Vehicle v(*sp,&g); v.assists.tc=false; Controls c; bool stab=false; double t100=-1, maxY=0;
    for(int i=0;i<12000;i++){c.throttle=1;v.step(dt,c); stab|=v.stabilityActive; maxY=std::max(maxY,std::fabs(hdg(v)));
      if(t100<0&&v.speed()*3.6>=100)t100=i*dt;}
    // 2) freada reta de 200 -> 0 com ABS off e on
    double d[2];
    for(int abs=0;abs<2;abs++){ Vehicle w(*sp,&g); w.assists.abs=abs; Controls k; for(int i=0;i<90000&&w.speed()*3.6<180;i++){k.throttle=1;w.step(dt,k);} k.throttle=0;k.brake=1; double x0=w.pos.z; for(int i=0;i<15000&&w.speed()>1;i++)w.step(dt,k); d[abs]=std::fabs(w.pos.z-x0)+std::fabs(w.pos.x);}
    // 3) freia + curva a 150, ABS off: estavel?
    Vehicle u(*sp,&g); u.assists.abs=false; Controls k; for(int i=0;i<90000&&u.speed()*3.6<150;i++){k.throttle=1;u.step(dt,k);} double mb=0; k.throttle=0;k.brake=1;k.steer=1;
    for(int i=0;i<2500;i++){u.step(dt,k); mb=std::max(mb,std::fabs(u.bodySlip)*57.3);} 
    // 4) simetria
    double hl=0,hr=0; for(int s=-1;s<=1;s+=2){Vehicle q(*sp,&g); q.assists.tc=false; Controls z; for(int i=0;i<60000&&q.speed()*3.6<100;i++){z.throttle=1;q.step(dt,z);} double h0=hdg(q); z.steer=s; z.throttle=0.6; for(int i=0;i<2000;i++)q.step(dt,z); (s<0?hl:hr)=hdg(q)-h0;}
    // 5) re
    Vehicle r(*sp,&g); Controls rc; r.assists.autoGearbox=false; r.gear=-1; for(int i=0;i<4000;i++){rc.throttle=1;rc.steer=0.5;r.step(dt,rc);} 
    printf("%-26s 0-100=%.2fs reta:stab=%d desvio=%.2f | freio200->0: ABSoff %.0fm ABSon %.0fm | freia+curva maxDeriva=%.1f | simetria L=%+.1f R=%+.1f | re v=%.1f km/h gear=%d NaN=%d\n",
      sp->name.c_str(),t100,stab,maxY,d[0],d[1],mb,hl,hr,r.forwardSpeed()*3.6,r.gear,(int)!std::isfinite(r.pos.x));
  }
}
