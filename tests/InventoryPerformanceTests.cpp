#include "ethhhpatch_inventory.h"
#include "ethhhpatch_entity_reader.h"
#include "util.h"
#include <cstdio>
#include <cstdlib>

using namespace eth::patch;
static void Check(bool ok, const char* message) {
    if (!ok) { std::fprintf(stderr,"FAIL %s\n",message); std::exit(1); }
}
static int64_t Counter() { LARGE_INTEGER q{}; QueryPerformanceCounter(&q); return q.QuadPart; }
struct Scene {
    unsigned char* data = nullptr;
    uintptr_t base = 0, table = 0, self = 0, fonts = 0, list = 0;
    bool combined = false;
    inventory::Roots roots;
    inventory::QueuedText text;
    explicit Scene(bool allGroups=false, bool attachedClock=false) : combined(allGroups) {
        data=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x40000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
        Check(data!=nullptr,"isolated fixture");
        base=uintptr_t(data); table=base; self=base+0x2000; fonts=base+0x3000; list=base+0x3400;
        Put<uint32_t>(self+0x1A8,uint32_t(base+0x2400));
        Put<uint32_t>(base+0x2404,uint32_t(base+0x2500));
        memcpy(data+0x2500,"Panel",6);
        Put<uint32_t>(list,uint32_t(fonts));
        for(unsigned i=0;i<3;++i) {
            Put<uint32_t>(fonts+i*0x80+0x24,i==2?0:uint32_t(fonts+(i+1)*0x80));
            Put<int32_t>(fonts+i*0x80+0x2C,640);
            Put<int32_t>(fonts+i*0x80+0x34,224);
        }
        for(unsigned i=0;i<64;++i) {
            const uintptr_t object=Object(i);
            Put<uint32_t>(table+i*8,0x600+i); Put<uint32_t>(table+i*8+4,uint32_t(object));
            Put<uint32_t>(object,0x4C8D94); Put<uint32_t>(object+12,uint32_t(self));
            Put<uint32_t>(object+16,0x600+i);
            Put<float>(object+28,inventory::AnchorX(256,16.f/9)); Put<float>(object+32,105);
            if(i>=4 && i<16) Put<uint32_t>(object+24,uint32_t(Object((i-4)/3)));
            if(combined && i>=18 && i<24) Put<uint32_t>(object+24,uint32_t(Object(i<21?16:17)));
        }
        auto read=[](uintptr_t a,void* out,size_t n) { return eth::SafeReadBytes(a,out,n); };
        for(unsigned ordinal=0;ordinal<(combined?6u:4u);++ordinal) {
            const unsigned i=ordinal<4?ordinal:ordinal+12;
            inventory::Root root{};
            Check(inventory::Identity(uint32_t(self),0x600+i,table,0x4C8D94,root,read),"fixture root");
            root.kind=ordinal<4?inventory::Kind::Inventory:ordinal==4?inventory::Kind::Health:inventory::Kind::Rage;
            root.native=root.installed; root.native.x=256;
            auto* slot=roots.Slot(root,table,0x4C8D94,read);
            Check(slot!=nullptr,"fixture root slot"); *slot=root;
        }
        Put<uint32_t>(Object(16)+0x14,uint32_t(base+0x3500));
        Put<float>(base+0x350C,2048); Put<float>(base+0x3514,1); Put<float>(base+0x3524,1);
        if (attachedClock) {
            Put<uint32_t>(base+0x29A8,uint32_t(base+0x2A00));
            Put<uint32_t>(base+0x2A04,uint32_t(base+0x2B00));
            memcpy(data+0x2B00,"MissionTimer",13);
            for(unsigned i=18;i<21;++i) Put<uint32_t>(Object(i)+12,uint32_t(base+0x2800));
        }
    }
    ~Scene() { VirtualFree(data,0,MEM_RELEASE); }
    uintptr_t Object(unsigned i) const { return base+0x4000+i*0x100; }
    uintptr_t Node(unsigned i) const { return base+0x10000+i*0x40; }
    template<class T> void Put(uintptr_t a,T value) { memcpy(reinterpret_cast<void*>(a),&value,sizeof(value)); }
    void ResetQueues() {
        for(unsigned i=0;i<3;++i) Put<uint32_t>(fonts+i*0x80,0);
        text.Clear();
    }
    void Queue(unsigned i) {
        const uintptr_t font=fonts+(i%3)*0x80, node=Node(i);
        uint32_t old=0; memcpy(&old,reinterpret_cast<void*>(font),4);
        Put<uint32_t>(node,old); Put<float>(node+4,661.333f); Put<uint32_t>(font,uint32_t(node));
    }
    template<class Read>
    unsigned Tick(Read read) {
        ResetQueues();
        if(combined) {
            inventory::Root hp{}; hp.object=uint32_t(Object(16));
            float anchor=0;
            Check(inventory::HealthLeftAnchor(hp,anchor,read) && anchor==-128,"combined HP anchor geometry");
        }
        for(unsigned i=0;i<64;++i) {
            inventory::QueueSnapshot before{};
            if(roots.Contains(Object(i),table,0x4C8D94,read)) before.Capture(list,read);
            Queue(i); // Native work is fixture-only, not the game's renderer.
            text.Collect(before,read);
        }
        return 64;
    }
    template<class Read>
    unsigned Render(Read read) {
        unsigned matches=0;
        inventory::ClipBounds bounds; bounds.Configure(16.f/9);
        for(unsigned i=0;i<64;++i) for(unsigned glyph=0;glyph<16;++glyph) {
            const uintptr_t font=fonts+(i%3)*0x80;
            if(!text.Matches(font,Node(i),read)) continue;
            inventory::FontClip clip{};
            Check(read(font,&clip,sizeof(clip)) && bounds.Apply(clip),"inventory clip");
            ++matches;
        }
        return matches;
    }
};

int main() {
    Scene scene;
    {
        entity::ReadScope scope;
        auto safe=[](uintptr_t a,void* out,size_t n) { return eth::SafeReadBytes(a,out,n); };
        auto read=[&](uintptr_t a,void* out,size_t n) { return entity::ReadProtected(scope,a,out,n); };
        scene.Tick(safe);
        Check(scene.text.Matches(scene.fonts+(4%3)*0x80,scene.Node(4),read),"warm native queue header");
        const size_t queries=scope.Queries();
        DWORD old=0;
        Check(VirtualProtect(reinterpret_cast<void*>(scene.Node(0)),0x1000,PAGE_NOACCESS,&old)!=FALSE,"revoke queue page");
        Check(!scene.text.Matches(scene.fonts+(4%3)*0x80,scene.Node(4),read) &&
            scope.Queries()==queries,"cached region still copies under SEH after page revocation");
        Check(VirtualProtect(reinterpret_cast<void*>(scene.Node(0)),0x1000,PAGE_READWRITE,&old)!=FALSE,"restore queue page");
        scope.Reset();
        Check(scene.text.Matches(scene.fonts+(4%3)*0x80,scene.Node(4),read),"new phase reads fresh queue bytes");
        scene.Put<float>(scene.Node(4)+4,12);
        Check(!scene.text.Matches(scene.fonts+(4%3)*0x80,scene.Node(4),read),"cached permissions never cache queue data");
        Check(VirtualProtect(reinterpret_cast<void*>(scene.Node(0)),0x1000,PAGE_READWRITE|PAGE_GUARD,&old)!=FALSE,"guard queue page");
        scope.Reset();
        Check(!scene.text.Matches(scene.fonts+(4%3)*0x80,scene.Node(4),read) && scope.Copies()==0,
            "new phase refuses guard page without probing native bytes");
        Check(VirtualProtect(reinterpret_cast<void*>(scene.Node(0)),0x1000,PAGE_READWRITE,&old)!=FALSE,"restore guarded queue");
    }
    LARGE_INTEGER frequency{}; QueryPerformanceFrequency(&frequency);
    constexpr unsigned iterations=200;
    for(unsigned combined=0;combined<3;++combined) {
      Scene measured(combined!=0,combined==2);
      for(unsigned strategy=0;strategy<2;++strategy) {
        entity::ReadScope scope;
        size_t queries=0,copies=0;
        auto read=[&](uintptr_t a,void* out,size_t n) {
            ++copies;
            if(!strategy) { ++queries; return eth::SafeReadBytes(a,out,n); }
            return entity::ReadProtected(scope,a,out,n);
        };
        const int64_t start=Counter();
        unsigned expected=0;
        for(unsigned i=0;i<iterations;++i) {
            scope.Reset(); measured.Tick(read); if(strategy) queries+=scope.Queries();
            scope.Reset();
            const unsigned matches=measured.Render(read); if(strategy) queries+=scope.Queries();
            if(!i) expected=matches;
            Check(matches==expected && matches==(combined?384u:256u),"same HUD membership and clip decisions");
        }
        const double ms=1000.0*double(Counter()-start)/frequency.QuadPart/iterations;
        std::printf("%s %s: %.4f ms/tick+render, %zu queries, %zu copies (64 UI objects, 1024 glyph calls)\n",
            combined==2?"HUD+attached clock":combined?"combined HUD":"inventory",strategy?"phase-scoped":"conservative",
            ms,queries/iterations,copies/iterations);
      }
    }
    std::puts("PASS isolated HUD SDK-path benchmark; not observed in-game FPS");
}
