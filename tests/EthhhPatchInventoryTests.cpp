#include "ethhhpatch_inventory.h"
#include "ethhhpatch_inventory_thunk.h"
#include "ethhhpatch_movie.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <type_traits>
#include <utility>

using namespace eth::patch::inventory;
static int checks = 0;
static void Check(bool value, const char* message) {
    ++checks;
    if (!value) { std::fprintf(stderr, "FAIL %s\n", message); std::exit(1); }
}
struct Memory {
    std::array<unsigned char, 0x10000> bytes{};
    template<class T> void Set(uintptr_t a, T value) { memcpy(bytes.data()+a, &value, sizeof(value)); }
    bool Read(uintptr_t a, void* out, size_t n) {
        if (a > bytes.size() || n > bytes.size()-a) return false;
        memcpy(out, bytes.data()+a, n); return true;
    }
    Memory() {
        Set<uint32_t>(0x21A8, 0x3000); Set<uint32_t>(0x3004, 0x3100);
        memcpy(bytes.data()+0x3100, "Panel", 6);
        for (unsigned i=0; i<6; ++i) {
            const uint32_t object = 0x6000+i*0x100, handle=0x601+i;
            Set(0x5000+(handle&511)*8, handle); Set(0x5004+(handle&511)*8, object);
            Set(object, uint32_t(0x4C8D94)); Set(object+12, uint32_t(0x2000));
            Set(object+16, handle); Set(object+28, 256.0f); Set(object+32, 105.0f);
        }
        const uint32_t words[]={0x15741172,0x15751672,0x15761872,0x80FF7400,0x41DD10};
        memcpy(bytes.data()+0x4000, words, sizeof(words));
    }
};

static uintptr_t gotSelf, gotArgs, gotCaller, gotPC, expectedCaller;
static uint32_t beforeStack, afterStack, gotEsi, gotEdi, gotEbx, gotEbp;
static void __cdecl Scoped(void* self, const Position* args, uintptr_t caller, uintptr_t pc) {
    gotSelf=uintptr_t(self); gotArgs=uintptr_t(args); gotCaller=caller; gotPC=pc;
}
ETH_INVENTORY_THUNK(TestThunk, Scoped)
__declspec(naked) static void Invoke() {
    __asm {
        push ebx
        push esi
        push edi
        push ebp
        mov esi, 4014h
        mov edi, 12345678h
        mov ebx, 87654321h
        mov ebp, 13579BDFh
        mov beforeStack, esp
        mov eax, offset continuation
        mov expectedCaller, eax
        push 9000h
        push 2000h
        call TestThunk
    continuation:
        add esp, 8
        mov afterStack, esp
        mov gotEsi, esi
        mov gotEdi, edi
        mov gotEbx, ebx
        mov gotEbp, ebp
        pop ebp
        pop edi
        pop esi
        pop ebx
        ret
    }
}

static uint32_t __cdecl ClipScoped(const FontClip* font, uintptr_t caller, uintptr_t node) {
    gotSelf=uintptr_t(font); gotCaller=caller; gotPC=node;
    return 0xABCDEF01;
}
ETH_INVENTORY_CLIP_THUNK(TestClipThunk, ClipScoped)
static uint32_t gotResult;
__declspec(naked) static void InvokeClip() {
    __asm {
        push ebx
        push esi
        push edi
        push ebp
        mov esi, 1234h
        mov edi, 2345h
        mov ebx, 3456h
        mov ebp, 4567h
        mov beforeStack, esp
        sub esp, 18h
        mov dword ptr [esp+14h], 9000h
        mov eax, offset continuation
        mov expectedCaller, eax
        push 8000h
        call TestClipThunk
    continuation:
        add esp, 1Ch
        mov afterStack, esp
        mov gotResult, eax
        mov gotEsi, esi
        mov gotEdi, edi
        mov gotEbx, ebx
        mov gotEbp, ebp
        pop ebp
        pop edi
        pop esi
        pop ebx
        ret
    }
}

static eth::patch::MovieDrawArgs capturedMovie;
static void __cdecl CaptureMovie(void* surface,float x,float y,float sx,float sy,float alpha) {
    capturedMovie={surface,x,y,sx,sy,alpha};
}
static void __cdecl FittedMovie(void* surface,float x,float y,float sx,float sy,float alpha) {
    eth::patch::MovieDrawArgs args{surface,x,y,sx,sy,alpha};
    eth::patch::FitMovie(args,640,480,1920,1080);
    eth::patch::SubmitMovie(&CaptureMovie,args);
}
static_assert(std::is_same_v<decltype(&FittedMovie),eth::patch::MovieDrawFn>);
static uint32_t movieSentinel;
__declspec(naked) static void InvokeMovie() {
    __asm {
        mov beforeStack, esp
        push 0DEADBEEFh
        push 03F400000h
        push 040100000h
        push 040400000h
        push 0
        push 0
        push 12340000h
        call FittedMovie
        add esp, 18h
        mov eax, dword ptr [esp]
        mov movieSentinel, eax
        add esp, 4
        mov afterStack, esp
        ret
    }
}

int main() {
    for (float aspect : {4.f/3,16.f/10,16.f/9,21.f/9,32.f/9}) for(float width : {640.f,1280.f,1920.f,3840.f}) {
        const float scale=(4.f/3)/aspect;
        for(float hpX : {-165.f,-465.f,-485.f}) {
            const float root=AnchorGroupX(hpX,-128,aspect);
            const float left=width*.5f+(root-128)*scale*width/640;
            Check(std::abs(left/width-(.5f+(hpX-128)/640))<.000001f,
                "HP left edge retains screen percentage, not its center or a fixed pixel offset");
            const float pixels=256*scale*width/640;
            Check(std::abs(pixels-256*(width/aspect)/480)<.001f,"HP child geometry remains uniformly sized");
            if(hpX<=-465) Check(root+128 < -320/scale,"hidden HP group lies completely outside viewport");
        }
        for(float rageX : {-400.f,-250.f,0.f}) {
            const float root=AnchorGroupX(rageX,0,aspect);
            const float center=width*.5f+root*scale*width/640;
            Check(std::abs(center/width-(.5f+rageX/640))<.000001f,"rage origin follows inventory percentage rule");
            if(rageX==0) Check(root==0,"expanded rage selector remains screen-centered");
            if(rageX==-400) Check(root+16 < -320/scale,"hidden rage diamond remains off-screen");
        }
    }
    for (float aspect : {4.f/3, 16.f/10, 16.f/9, 21.f/9, 32.f/9}) {
        for (float width : {640.f, 1280.f, 1920.f, 2560.f, 3840.f}) {
            const float hudScale=(4.f/3)/aspect;
            for (float x : {256.f, 272.f, 396.f, 416.f}) {
                const float pixel=width*.5f + AnchorX(x,aspect)/320.f*hudScale*width*.5f;
                Check(std::abs(pixel/width-(.5f+x/640.f))<.000001f,
                    "root anchor/slide endpoint retains percentage at every aspect and resolution");
                if (x>=396) Check(pixel>width, "hidden endpoint beyond actual right edge, including ultrawide");
            }
        }
    }
    Check(AnchorX(396,1)==396 && AnchorX(256,4.f/3)==256, "narrow/native aspect unchanged");
    const float nan=std::numeric_limits<float>::quiet_NaN();
    Check(AnchorX(256,nan)==256 && AnchorX(256,9)==256, "unsupported aspect refuses adjustment");
    Check(AnchorX(std::numeric_limits<float>::max(),8)==std::numeric_limits<float>::max(), "overflow refused");
    Memory memory;
    auto read=[&](uintptr_t a,void* out,size_t n) { return memory.Read(a,out,n); };
    const auto baseline=memory.bytes;
    {
        const uint32_t health[]={0x0A790000,0x0A7A000C,0x5A7B0000,0xC3198000,0x80FF7900,0x41DD10};
        const uint32_t rage[]={0x0A560001,0x37595508,0x0D573959,0x5D595500,
            0x42200000,0x5F585900,0x43000000,0x80FF5600,0x41DD10};
        const uint32_t transition[]={0x15811180,0x15821680,0x15831880,0x80FF8100,0x41DD10};
        auto contract=[&](const uint32_t* words,unsigned count,Kind kind) {
            memory.bytes=baseline;
            memcpy(memory.bytes.data()+0x4000,words,count*4);
            const auto good=memory.bytes;
            Check(PositionCall(0x4000+count*4,0x41DD10,read)==kind,"known gameplay HUD position site classified");
            for(unsigned bit=0;bit<count*32;++bit) {
                memory.bytes=good; memory.bytes[0x4000+bit/8]^=uint8_t(1<<(bit%8));
                Check(PositionCall(0x4000+count*4,0x41DD10,read)==Kind::Unrelated,
                    "every HP/rage/transition signature bit guarded");
            }
        };
        contract(health,6,Kind::Health); contract(rage,9,Kind::Rage); contract(transition,5,Kind::Inventory);
        memory.bytes=baseline;
    }
    Check(InventoryCall(0x4014,0x41DD10,read), "exact relocated inventory bytecode accepted");
    for (unsigned bit=0; bit<160; ++bit) {
        memory.bytes=baseline; memory.bytes[0x4000+bit/8]^=uint8_t(1<<(bit%8));
        Check(!InventoryCall(0x4014,0x41DD10,read), "each bytecode contract bit guarded");
    }
    memory.bytes=baseline;
    Check(!InventoryCall(0,0x41DD10,read) && !InventoryCall(UINT32_MAX,0x41DD10,read), "bad program counter refused");
    Root root{};
    Check(Identity(0x2000,0x601,0x5000,0x4C8D94,root,read), "exact Panel/root/self handle accepted");
    for (uintptr_t bad : {0x21A8u,0x3004u,0x5008u,0x500Cu,0x6000u,0x600Cu,0x6010u,0x6018u}) {
        memory.bytes=baseline; memory.Set(bad,uint32_t(0xBAD));
        Check(!Identity(0x2000,0x601,0x5000,0x4C8D94,root,read), "class/owner/generation/vtable/parent mismatch refused");
    }
    memory.bytes=baseline; memcpy(memory.bytes.data()+0x3100,"Ninja",6);
    Check(!Identity(0x2000,0x601,0x5000,0x4C8D94,root,read), "unrelated class refused");
    memory.bytes=baseline;
    Roots roots;
    for (unsigned i=0; i<4; ++i) {
        Check(Identity(0x2000,0x601+i,0x5000,0x4C8D94,root,read), "inventory group identity");
        Root* slot=roots.Slot(root,0x5000,0x4C8D94,read);
        Check(slot!=nullptr,"bounded root tracking available");
        root.native={0x601+i,256,105}; root.installed=root.native;
        root.installed.x=AnchorX(256,16.f/9); *slot=root;
        memory.Set(root.object+28, root.installed.x);
    }
    Identity(0x2000,0x605,0x5000,0x4C8D94,root,read);
    Check(!roots.Slot(root,0x5000,0x4C8D94,read), "fifth live root refused");
    memory.Set<uint32_t>(0x6418,0x6000);
    Check(roots.Contains(0x6400,0x5000,0x4C8D94,read), "parented numeric label belongs to inventory");
    memory.Set<uint32_t>(0x640C,0x2200);
    Check(!roots.Contains(0x6400,0x5000,0x4C8D94,read), "foreign label cannot borrow inventory parent");
    memory.Set<uint32_t>(0x640C,0x2000); memory.Set<uint32_t>(0x6418,0x6400);
    Check(!roots.Contains(0x6400,0x5000,0x4C8D94,read), "cyclic parents bounded");
    memory.Set<uint32_t>(0x6418,0);
    // One dead generation and one foreign position edit must not be overwritten.
    memory.Set<uint32_t>(0x5008,0x801); memory.Set(0x611C,77.f);
    unsigned restored=0;
    roots.Restore(0x5000,0x4C8D94,read,[&](uint32_t self,const Position& args) {
        Check(self==0x2000 && (args.handle==0x603 || args.handle==0x604) && args.x==256 && args.y==105,
            "cleanup restores only owned live native root arguments");
        ++restored;
    });
    Check(restored==2,"stale and foreign-edited roots retained on cleanup");
    roots.Restore(0x5000,0x4C8D94,read,[&](uint32_t,const Position&) { Check(false,"cleanup only once"); });
    {
        memory.bytes=baseline;
        Roots groups;
        Root invalid{}; invalid.kind=static_cast<Kind>(255);
        Check(!groups.Slot(invalid,0x5000,0x4C8D94,read),"unknown HUD kind cannot take a gameplay slot");
        for(unsigned i=0;i<6;++i) {
            Root group{};
            Check(Identity(0x2000,0x601+i,0x5000,0x4C8D94,group,read),"shared gameplay HUD group identity");
            group.kind=i<4 ? Kind::Inventory : i==4 ? Kind::Health : Kind::Rage;
            group.native=group.installed;
            group.native.x=i<4 ? 256.f : i==4 ? -165.f : -250.f;
            group.installed=group.native;
            group.installed.x=AnchorGroupX(group.native.x,i==4 ? -128.f : 0.f,16.f/9);
            Root* slot=groups.Slot(group,0x5000,0x4C8D94,read);
            Check(slot!=nullptr,"HP/rage have independent slots, never displacing four inventory parents");
            *slot=group;
            memory.Set<float>(group.object+28,group.installed.x);
        }
        {
            const auto grouped=memory.bytes;
            // Separate scripts own the clock/counter group and numeric label,
            // but their parent is Panel's anchored HP sprite.
            memory.Set<uint32_t>(0x25A8,0x3400);
            memory.Set<uint32_t>(0x3404,0x3500);
            memory.Set<uint32_t>(0x600C,0x2400);
            memory.Set<uint32_t>(0x610C,0x2400);
            memory.Set<uint32_t>(0x6018,0x6100);
            memory.Set<uint32_t>(0x6118,0x6400);
            for (const char* name : {"MissionTimer","MissionCounter"}) {
                memset(memory.bytes.data()+0x3500,0,32);
                memcpy(memory.bytes.data()+0x3500,name,strlen(name)+1);
                Check(groups.Contains(0x6000,0x5000,0x4C8D94,read),
                    "HP-attached clock/counter numeric text crosses its verified script-owner boundary");
                memory.Set<uint32_t>(0x1000,0x8000);
                memory.Set<uint32_t>(0x8000,0);
                QueueSnapshot before{};
                if (groups.Contains(0x6000,0x5000,0x4C8D94,read)) before.Capture(0x1000,read);
                memory.Set<uint32_t>(0x8000,0x9000);
                const float left=320+AnchorGroupX(-165,-128,16.f/9)-63-36;
                memory.Set<float>(0x9004,left);
                QueuedText text; text.Collect(before,read);
                Check(left<0 && text.Matches(0x8000,0x9000,read),
                    "HP clock's negative authored X reproduces old left clipping but now receives provenance");
                ClipBounds bounds; bounds.Configure(16.f/9);
                FontClip clip{}; clip.right=640; clip.bottom=224;
                Check(text.Matches(0x8000,0x9000,read) && bounds.Apply(clip) && left>clip.left,
                    "attached clock glyph survives the shared viewport clip on repeated zero-tick renders");
                memory.Set<uint32_t>(0x6118,0x6500);
                Check(!groups.Contains(0x6000,0x5000,0x4C8D94,read),
                    "clock exception cannot borrow the rage root");
                memory.Set<uint32_t>(0x6118,0x6200);
                Check(!groups.Contains(0x6000,0x5000,0x4C8D94,read),
                    "clock exception cannot borrow an inventory root");
                memory.Set<uint32_t>(0x6118,0x6400);
            }
            const auto attached=memory.bytes;
            memcpy(memory.bytes.data()+0x3500,"PauseMenu\0\0\0\0\0",15);
            Check(!groups.Contains(0x6000,0x5000,0x4C8D94,read),
                "menu owner remains unrelated even when attached to HP");
            memory.bytes=attached;
            memory.Set<uint32_t>(0x25A8,UINT32_MAX);
            Check(!groups.Contains(0x6000,0x5000,0x4C8D94,read),"unreadable clock owner refused");
            memory.bytes=attached;
            memory.Set<uint32_t>(0x5008,0x801);
            Check(!groups.Contains(0x6000,0x5000,0x4C8D94,read),"recycled clock label handle refused");
            memory.bytes=attached;
            memory.Set<uint32_t>(0x5010,0x802);
            Check(!groups.Contains(0x6000,0x5000,0x4C8D94,read),"recycled clock background handle refused");
            memory.bytes=attached;
            memory.Set<float>(0x641C,99.f);
            Check(!groups.Contains(0x6000,0x5000,0x4C8D94,read),"foreign edit of HP position invalidates clock provenance");
            memory.bytes=attached;
            memory.Set<uint32_t>(0x6118,0);
            Check(!groups.Contains(0x6000,0x5000,0x4C8D94,read),"standalone clock has no expanded clip lease");
            memory.bytes=grouped;
        }
        unsigned restoredGroups=0;
        groups.Restore(0x5000,0x4C8D94,read,[&](uint32_t,const Position& position) {
            Check(position.x==(position.handle<=0x604 ? 256.f : position.handle==0x605 ? -165.f : -250.f),
                "shared restore returns each HP/rage/inventory group to its own unscaled native position");
            ++restoredGroups;
        });
        Check(restoredGroups==6,"shared restoration covers all gameplay groups");
        Root hp{}; hp.object=0x6400;
        memory.Set<uint32_t>(0x6414,0xB000);
        memory.Set<float>(0xB00C,2048); memory.Set<float>(0xB014,1);
        memory.Set<float>(0xB024,1);
        float anchor=0;
        Check(HealthLeftAnchor(hp,anchor,read) && anchor==-128,"HP anchor derives from actual sixteenth-unit sprite half-width");
        memory.Set<float>(0xB00C,1024);
        Check(HealthLeftAnchor(hp,anchor,read) && anchor==-64,"different native sprite width does not require pixel constants");
        memory.Set<float>(0xB024,0);
        Check(!HealthLeftAnchor(hp,anchor,read),"rotated HP sprite refused rather than guessed");
        memory.Set<float>(0xB024,1); memory.Set<float>(0xB0A8,1);
        Check(!HealthLeftAnchor(hp,anchor,read),"cropped HP sprite refused");
        memory.Set<float>(0xB0A8,0); memory.Set<float>(0xB014,nan);
        Check(!HealthLeftAnchor(hp,anchor,read),"nonfinite HP extent refused");
        memory.Set<uint32_t>(0x6414,UINT32_MAX);
        Check(!HealthLeftAnchor(hp,anchor,read),"unreadable HP sprite refused");
        memory.bytes=baseline;
    }
    for (float aspect : {4.f/3,16.f/10,16.f/9,21.f/9,32.f/9}) {
        FontClip clip{}; clip.right=640; clip.bottom=224;
        FontClip prepared=clip;
        ClipBounds bounds; bounds.Configure(aspect);
        const bool changed=ExpandClip(clip,aspect);
        Check(bounds.Apply(prepared)==changed && !memcmp(&prepared,&clip,sizeof(clip)),
            "once-per-render bounds preserve clip decisions without per-glyph math");
        Check(changed==(aspect>4.f/3),"font clip widening gated by widescreen aspect");
        Check(clip.top==0 && clip.bottom==224,"vertical clipping unchanged");
        const float rightNdc=clip.right/320.f-1;
        Check(rightNdc*((4.f/3)/aspect)>=.99999f,"font clip covers actual viewport before HUD squeeze");
        if (changed) {
            const float glyphLeft=320+AnchorX(256,aspect)+8;
            Check(glyphLeft<clip.right,"visible inventory digits survive");
            if (aspect>=16.f/9) Check(glyphLeft>640,"16:9+ reproduces old font clip rejection");
            const float hiddenLeft=320+AnchorX(396,aspect)+8;
            Check(hiddenLeft>clip.right,"hidden digits still clipped outside real screen");
        }
        FontClip custom{}; custom.left=10; custom.right=500;
        Check(!ExpandClip(custom,aspect) && custom.left==10 && custom.right==500,"custom text subrect retained");
    }
    {
        memory.bytes=baseline;
        memory.Set<uint32_t>(0x1000,0x8000);
        QueuedText text;
        for(unsigned batch=0;batch<4;++batch) {
            QueueSnapshot snapshot; snapshot.Capture(0x1000,read);
            for(unsigned j=0;j<32;++j) {
                const uintptr_t node=0x9000+(batch*32+j)*32;
                uint32_t head=0; read(0x8000,&head,4);
                memory.Set<uint32_t>(node,head);
                memory.Set<uint32_t>(0x8000,uint32_t(node));
            }
            text.Collect(snapshot,read);
        }
        unsigned reads=0;
        auto counted=[&](uintptr_t a,void* out,size_t n) { ++reads; return read(a,out,n); };
        for(unsigned i=0;i<128;++i) {
            const unsigned before=reads;
            Check(text.Matches(0x8000,0x9000+i*32,counted) && reads==before+1,
                "full hash index validates exactly one fresh header per tagged string");
        }
        reads=0;
        for(unsigned i=0;i<1024;++i)
            Check(!text.Matches(0x8100,0x9000+i*32,counted),"unrelated text rejected");
        Check(reads==0,"unrelated glyphs perform zero protected reads, regardless of tag count");
        QueueSnapshot snapshot; snapshot.Capture(0x1000,read);
        memory.Set<uint32_t>(0xB000,0xB000); memory.Set<uint32_t>(0x8000,0xB000);
        text.Collect(snapshot,read);
        Check(!text.Matches(0x8000,0xB000,read) && text.Matches(0x8000,0x9000,read),
            "overflow/corrupt collection rolls back hash and preserves older valid tags");
        text.Clear();
        Check(!text.Matches(0x8000,0x9000,read),"clear expires hash index too");
    }
    {
        memory.bytes=baseline;
        memory.Set<uint32_t>(0x1000,0x8000);
        QueueSnapshot snapshot;
        snapshot.Capture(0x1000,read);
        Check(snapshot.valid && snapshot.count==1,"bounded original font queue snapshot");
        memory.Set<uint32_t>(0x8000,0x9000);
        memory.Set<float>(0x9004,661.333f);
        QueuedText text; text.Collect(snapshot,read);
        Check(text.Matches(0x8000,0x9000,read),"only inventory-appended string is tagged");
        Check(text.Matches(0x8000,0x9000,read) && text.Matches(0x8000,0x9000,read),
            "zero-tick renders retain queued label provenance");
        Check(!text.Matches(0x8004,0x9000,read) && !text.Matches(0x8000,0x9004,read),
            "other fonts and strings keep native clipping");
        memory.Set<float>(0x9004,12);
        Check(!text.Matches(0x8000,0x9000,read),"reused/edited queue bytes not mistaken for inventory");
        memory.Set<float>(0x9004,661.333f);
        text.Clear();
        Check(!text.Matches(0x8000,0x9000,read),"native tick/restore cleanup expires queued labels");
        memory.Set<uint32_t>(0x9000,0x9000);
        text.Collect(snapshot,read);
        Check(!text.Matches(0x8000,0x9000,read),"cyclic queue fails closed without partial tags");
        memory.Set<uint32_t>(0x8024,0x8000);
        snapshot.Capture(0x1000,read);
        Check(!snapshot.valid,"cyclic font list refused");
        snapshot.Capture(UINT32_MAX,read);
        Check(!snapshot.valid,"unreadable font list refused");
    }
    for (unsigned i=0;i<1000;++i) {
        Invoke();
        Check(beforeStack==afterStack,"production thunk leaves native cdecl cleanup to caller");
        Check(gotSelf==0x2000 && gotArgs==0x9000 && gotCaller==expectedCaller && gotPC==0x4014,
            "production thunk captures exact caller, ESI and arguments");
        Check(gotEsi==0x4014 && gotEdi==0x12345678 && gotEbx==0x87654321 && gotEbp==0x13579BDF,
            "production thunk preserves nonvolatile registers");
        InvokeClip();
        Check(beforeStack==afterStack,"clip thunk preserves cdecl font argument cleanup");
        Check(gotSelf==0x8000 && gotPC==0x9000 && gotCaller==expectedCaller,
            "clip thunk captures font, queued string stack slot and exact caller");
        Check(gotResult==0xABCDEF01 && gotEsi==0x1234 && gotEdi==0x2345 && gotEbx==0x3456 && gotEbp==0x4567,
            "clip thunk preserves native return value and nonvolatile registers");
        InvokeMovie();
        Check(beforeStack==afterStack && movieSentinel==0xDEADBEEF,"movie wrapper uses exactly six native stack arguments");
        Check(capturedMovie.surface==reinterpret_cast<void*>(0x12340000) && capturedMovie.alpha==.75f &&
            capturedMovie.x==240 && capturedMovie.y==0 && capturedMovie.scaleX==2.25f && capturedMovie.scaleY==2.25f,
            "movie fit preserves surface/alpha and changes BOTH scales, not a nonexistent height argument");
    }
    for(const auto& target : {std::pair<int,int>{640,480},{1920,1080},{3840,2160},{2560,1080},{1080,1920}}) {
        eth::patch::MovieDrawArgs args{reinterpret_cast<void*>(0x12340000),19,27,3,2.25f,.5f};
        Check(eth::patch::FitMovie(args,640,480,target.first,target.second),"valid movie target fitted");
        const float right=args.x+640*args.scaleX, bottom=args.y+480*args.scaleY;
        Check(std::abs(args.x-(target.first-right))<.001f && std::abs(args.y-(target.second-bottom))<.001f,
            "movie borders are symmetric, including portrait and ultrawide");
        Check(args.scaleX==args.scaleY && args.alpha==.5f,"movie aspect and opacity preserved");
        const auto saved=args;
        Check(!eth::patch::FitMovie(args,0,480,target.first,target.second) && !memcmp(&saved,&args,sizeof(args)),
            "unsupported movie dimensions retain every native argument");
    }
    std::printf("PASS widescreen HUD anchors/text/cleanup/movie Win32 ABI: %d assertions\n",checks);
}
