// Offline test of the harmonizer / frequency shifter / phaser. Build: g++ -std=c++17 -O2 -I.. dsp_test2.cpp ../CeciliaDsp.cpp
#include "CeciliaDsp.h"
#include <cmath>
#include <cstdio>
#include <vector>
using namespace lmms::mandeldsp;
static float zc(const std::vector<float>& v,int a,int b,float sr){int c=0;for(int i=a+1;i<b;i++)if(v[i-1]<0&&v[i]>=0)c++;return c*sr/(b-a);}
static float rms(const std::vector<float>& v,int a,int b){double s=0;for(int i=a;i<b;i++)s+=v[i]*v[i];return std::sqrt(s/(b-a));}
int main(){ const float sr=44100; const int N=44100*2; std::vector<float> o(N);
 Harmonizer h; h.prepare(sr); for(int i=0;i<N;i++) o[i]=h.process(0.5f*std::sin(2*M_PI*440*i/sr),7,0,0.1f);
 printf("Harmonizer +7 st: %.1f Hz (expect %.1f) rms %.3f\n", zc(o,10000,N,sr), 440*std::pow(2,7/12.0), rms(o,10000,N));
 h.prepare(sr); for(int i=0;i<N;i++) o[i]=h.process(0.5f*std::sin(2*M_PI*440*i/sr),-12,0,0.1f);
 printf("Harmonizer -12 st: %.1f Hz (expect 220)\n", zc(o,10000,N,sr));
 FreqShifter f; f.prepare(sr); for(int i=0;i<N;i++) o[i]=f.process(0.5f*std::sin(2*M_PI*440*i/sr),100);
 printf("FreqShift +100: %.1f Hz (expect 540) rms %.3f\n", zc(o,10000,N,sr), rms(o,10000,N));
 f.prepare(sr); for(int i=0;i<N;i++) o[i]=f.process(0.5f*std::sin(2*M_PI*440*i/sr),-140);
 printf("FreqShift -140: %.1f Hz (expect 300)\n", zc(o,10000,N,sr));
 Phaser p; p.prepare(sr); p.set(800,1.5f,5,8); unsigned s=1; double in2=0,out2=0; for(int i=0;i<N;i++){ s^=s<<13;s^=s>>17;s^=s<<5; float x=(s>>8)/16777216.f-0.5f; float y=p.process(x,0.5f); in2+=x*x; out2+=y*y; if(!std::isfinite(y)){printf("phaser NaN\n");return 1;} }
 printf("Phaser noise in rms %.3f out rms %.3f (stable)\n", std::sqrt(in2/N), std::sqrt(out2/N)); }
