#include "particle_system.h"
#include "wrapper/skia/skia_engine_bridge.h"
#include <algorithm>
#include <cmath>

namespace image {

ParticleSystem::ParticleSystem(int cap)
    : capacity_(cap), rng_(std::random_device{}()) {}

void ParticleSystem::Emit(float x, float y, int count) {
    std::uniform_real_distribution<float> angle(0.0f, 6.2831853f);
    std::uniform_real_distribution<float> speed(30.0f, 120.0f);
    std::uniform_real_distribution<float> sz(2.0f, 8.0f);
    for (int i=0; i<count && static_cast<int>(particles_.size())<capacity_; ++i) {
        float a=angle(rng_), s=speed(rng_);
        Particle p;
        p.x=x; p.y=y; p.vx=std::cos(a)*s; p.vy=std::sin(a)*s;
        p.life=1.0f; p.size=sz(rng_);
        p.color=XER::Bridge::Skia::MakeColorRGBA(255,200,50,200);
        particles_.push_back(p);
    }
}

void ParticleSystem::Update(float dt) {
    for (auto& p : particles_) {
        p.vx+=gx_*dt; p.vy+=gy_*dt;
        p.x+=p.vx*dt; p.y+=p.vy*dt;
        p.life-=dt*0.5f;
    }
    particles_.erase(
        std::remove_if(particles_.begin(), particles_.end(),
                       [](const Particle& p){ return p.life<=0.0f; }),
        particles_.end());
}

void ParticleSystem::Render(ImageBuffer& img) const {
    if (!img.IsValid() || particles_.empty()) return;
    if (!XER::Bridge::Skia::IsAvailable()) return;
    int w=img.Width(), h=img.Height();
    std::vector<uint32_t> px(w*h);
    const uint8_t* d=img.Data();
    for (int i=0;i<w*h;++i)
        px[i]=XER::Bridge::Skia::MakeColorRGBA(d[i*4+0],d[i*4+1],d[i*4+2],d[i*4+3]);
    for (auto& p : particles_) {
        int r=std::max(1,static_cast<int>(p.size*p.life));
        std::vector<std::pair<int,int>> pts;
        for (int a=0;a<16;++a) {
            float ang=a*6.2831853f/16.0f;
            pts.push_back({static_cast<int>(p.x+std::cos(ang)*r),
                           static_cast<int>(p.y+std::sin(ang)*r)});
        }
        uint32_t c=p.color;
        uint8_t alpha=static_cast<uint8_t>((c&0xFF)*p.life);
        XER::Bridge::Skia::RasterFillPolygon(&px,w,h,pts,(c&0xFFFFFF00u)|alpha,"src_over");
    }
    uint8_t* dst=img.Data();
    for (int i=0;i<w*h;++i) {
        uint32_t c=px[i];
        dst[i*4+0]=(c>>24)&0xFF; dst[i*4+1]=(c>>16)&0xFF;
        dst[i*4+2]=(c>>8)&0xFF;  dst[i*4+3]=c&0xFF;
    }
}

}  // namespace image
