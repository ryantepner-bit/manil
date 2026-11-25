// Offline test of the ported pyo DSP. Build: g++ -std=c++17 -O2 -I.. dsp_test.cpp ../CeciliaDsp.cpp && ./a.out
#include "CeciliaDsp.h"
#include <cmath>
#include <cstdio>
#include <vector>
using namespace lmms::mandeldsp;
static float rms(const std::vector<float>& v,int a,int b){double s=0;for(int i=a;i<b;i++)s+=v[i]*v[i];return std::sqrt(s/(b-a));}
// dominant freq via zero crossings
static float zc(const std::vector<float>& v,int a,int b,float sr){int c=0;for(int i=a+1;i<b;i++)if(v[i-1]<0&&v[i]>=0)c++;return c*sr/(b-a);}
int main(){
 const float sr=44100; const int N=44100*3;
 // PV identity
 PhaseVocoder pv; pv.prepare(sr,2048,4); PhaseVocoder::Params pp; pp.decay=0; pp.blur=0;
 std::vector<float> o(N); for(int i=0;i<N;i++) o[i]=pv.process(0.5f*std::sin(2*M_PI*440*i/sr),pp);
 printf("PV identity rms %.4f (expect %.4f) freq %.1f\n",rms(o,20000,N),0.5/std::sqrt(2),zc(o,20000,N,sr));
 // check sample-accurate: compare against delayed input
 double err=0; int lat=-1; for(int L=1500;L<2600;L++){double e=0;for(int i=30000;i<31000;i++){double d=o[i]-0.5*std::sin(2*M_PI*440*(i-L)/sr);e+=d*d;} if(lat<0||e<err){err=e;lat=L;}}
 printf("PV latency %d err %.5f\n",lat,std::sqrt(err/1000));
 // PV shift +220Hz
 pp.shiftHz=220; pv.reset(); for(int i=0;i<N;i++) o[i]=pv.process(0.5f*std::sin(2*M_PI*440*i/sr),pp);
 printf("PV shift freq %.1f rms %.3f\n",zc(o,20000,N,sr),rms(o,20000,N));
 // PV freeze: input stops after 1s, frozen output continues
 pp.shiftHz=0; pv.reset(); for(int i=0;i<N;i++){pp.freeze=i>sr*0.8; o[i]=pv.process(i<sr?0.5f*std::sin(2*M_PI*330*i/sr):0.f,pp);}
 printf("PV freeze tail rms %.3f freq %.1f\n",rms(o,2*44100,N),zc(o,2*44100,N,sr));
 // PV verb tail: decay=1 after input stops
 pp.freeze=false; pp.decay=0.9; pp.damp=0.3; pv.reset(); for(int i=0;i<N;i++) o[i]=pv.process(i<sr?0.5f*std::sin(2*M_PI*330*i/sr):0.f,pp);
 printf("PV verb rms 1.1-1.5s %.3f 2.5-3s %.4f\n",rms(o,48500,66000),rms(o,110000,N));
 // Waveguide: impulse -> tone 220 that decays
 Waveguide wg; wg.prepare(sr,20); wg.setFreq(220); wg.setDecay(1.0); for(int i=0;i<N;i++) o[i]=wg.process(i==0?1.f:0.f);
 printf("WG freq %.1f rms0-0.2 %.4f rms1.0-1.2 %.5f\n",zc(o,100,8820,sr),rms(o,100,8820),rms(o,44100,52920));
 // Granulator
 LiveGranulator g; g.prepare(sr,6); LiveGranulator::Params gp; gp.grains=16; gp.pitchSpray=7; gp.posSpray=1;
 std::vector<float> in(N),L(N),R(N); for(int i=0;i<N;i++) in[i]=0.5f*std::sin(2*M_PI*440*i/sr);
 g.process(in.data(),in.data(),L.data(),R.data(),N,gp); bool nan=false; for(float x:L) if(!std::isfinite(x)) nan=true;
 printf("Gran rmsL %.3f rmsR %.3f nan %d\n",rms(L,44100,N),rms(R,44100,N),nan);
 gp.freeze=true; std::vector<float> z(N,0.f); g.process(z.data(),z.data(),L.data(),R.data(),N,gp);
 printf("Gran frozen (silent input) rms %.3f\n",rms(L,0,N));
}
