// Weapon wheel and keyboard cheats; game assets are read from the live TXDs only.
#include "plugin.h"
#include "CCheat.h"
#include "CFont.h"
#include "CHud.h"
#include "CKeyGen.h"
#include "CMenuManager.h"
#include "CModelInfo.h"
#include "CPad.h"
#include "CPlayerPed.h"
#include "CPools.h"
#include "CStreaming.h"
#include "CSprite2d.h"
#include "CTimer.h"
#include "CTxdStore.h"
#include "CWeaponInfo.h"
#include "common.h"
#include <windows.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include "arsenal.h"
#include "arsenal_logic.h"
#include "shoot.h"
#include "skatemode.h"
#include "spawner.h"
#include "../common/link.h"

namespace mp::gta {
namespace {
constexpr float pi = 3.14159265f;
LogFn logLine = nullptr;
bool menu = false, wheel = false, guard = false, backDown = false, backUsed = false;
DWORD backAt = 0;
int taps = 0, hover = 0, count = 0, slots[13] = {}, types[13] = {};
int equip = -1, equipType = -1, page = 0, selected = 0;
bool keys[256] = {};
char message[160] = {};
DWORD messageUntil = 0;
bool god = false, infinite = false, savedInfinite = false;
int protectedPed = -1;
unsigned savedProofs = 0;
bool savedDamage = true;
int pendingWeapon = -1;
DWORD pendingAt = 0;
DWORD repeatAt = 0;

const char* names[] = {"Unarmed", "Brass knuckles", "Golf club", "Nightstick", "Knife", "Baseball bat", "Shovel",
 "Pool cue", "Katana", "Chainsaw", "Purple dildo", "Dildo", "Vibrator", "Silver vibrator", "Flowers", "Cane",
 "Grenade", "Tear gas", "Molotov", "", "", "", "Pistol", "Silenced pistol", "Desert Eagle", "Shotgun",
 "Sawed-off shotgun", "Combat shotgun", "Micro SMG", "MP5", "AK-47", "M4", "Tec-9", "Country rifle", "Sniper rifle",
 "Rocket launcher", "Heat-seeking launcher", "Flamethrower", "Minigun", "Satchel charges", "Detonator", "Spray can",
 "Fire extinguisher", "Camera", "Night vision", "Thermal goggles", "Parachute"};
const char* pages[] = {"Cheat menu", "Player", "Choose weapons", "Player cheats", "World & weather", "Traffic", "Peds & themes", "Spawn vehicles"};
struct CheatEntry { int id, category; const char* name; };
const CheatEntry cheats[] = {
#include "cheat_entries.inl"
};
const char* WeaponName(int t) { return arsenal::WeaponType(t) ? names[t] : "Unknown weapon"; }
bool Foreground() { DWORD pid=0; GetWindowThreadProcessId(GetForegroundWindow(), &pid); return pid==GetCurrentProcessId(); }
bool Paused() { return FrontEndMenuManager.m_bMenuActive || CTimer::m_UserPause || CTimer::m_CodePause; }
bool Press(int k) { bool d=(GetAsyncKeyState(k)&0x8000)!=0; bool p=d&&!keys[k]; keys[k]=d; return p; }
void Say(const char* s) { sprintf_s(message,"%s",s); messageUntil=GetTickCount()+3500; if(logLine) logLine("arsenal: %s",s); }
void CloseWheel(bool choose) {
    if (choose && hover>=0 && hover<count) { equip=slots[hover]; equipType=types[hover]; }
    wheel=false; guard=true;
}
int RowCount() {
    if(page==0) return 7;
    if(page==1) return 2;
    if(page==2) return 44;
    int n=0; for(const auto& c:cheats) if(c.category==page) ++n; return n;
}
int WeaponRow(int row) { int n=0; for(int t=0;t<=46;++t) if(arsenal::WeaponType(t) && n++==row) return t; return 0; }
const CheatEntry* CheatRow(int row) { for(const auto& c:cheats) if(c.category==page && row--==0) return &c; return nullptr; }
unsigned Proofs(CPlayerPed* p) { return static_cast<unsigned>(p->bBulletProof) | p->bFireProof<<1 | p->bCollisionProof<<2 | p->bMeleeProof<<3 | p->bInvulnerable<<4 | p->bExplosionProof<<5; }
void SetProofs(CPlayerPed* p,unsigned f) { p->bBulletProof=f&1; p->bFireProof=(f>>1)&1; p->bCollisionProof=(f>>2)&1; p->bMeleeProof=(f>>3)&1; p->bInvulnerable=(f>>4)&1; p->bExplosionProof=(f>>5)&1; }
void Protect(CPlayerPed* p) {
    int ref=CPools::GetPedRef(p);
    if(god) {
        if(protectedPed!=ref) { protectedPed=ref; savedProofs=Proofs(p); savedDamage=!p->m_pPlayerData || p->m_pPlayerData->m_bCanBeDamaged; }
        SetProofs(p,63);
        if(p->m_pPlayerData) { p->m_pPlayerData->m_bCanBeDamaged=false; if(p->m_pPlayerData->m_fBreath<100) p->m_pPlayerData->m_fBreath=100; }
        if(p->m_fHealth>0 && p->m_fHealth<100) p->m_fHealth=100;
    } else if(protectedPed>=0) {
        if(protectedPed==ref) { SetProofs(p,savedProofs); if(p->m_pPlayerData) p->m_pPlayerData->m_bCanBeDamaged=savedDamage; }
        protectedPed=-1;
    }
    if(infinite) {
        CCheat::m_aCheatsActive[CHEAT_INFINITE_AMMO]=true;
        for(auto& w:p->m_aWeapons) {
            int t=static_cast<int>(w.m_eWeaponType); if(!arsenal::WeaponType(t) || t<16) continue;
            auto* info=CWeaponInfo::GetWeaponInfo(w.m_eWeaponType,1);
            if(!info || !info->m_nAmmoClip) continue;
            w.m_nAmmoTotal=99999; w.m_nAmmoInClip=info->m_nAmmoClip;
            if(w.m_nState==WEAPONSTATE_RELOADING || w.m_nState==WEAPONSTATE_OUT_OF_AMMO) w.m_nState=WEAPONSTATE_READY;
        }
    }
}
void NativeCheat(int id) {
    if(id<0 || id>=TOTAL_CHEATS || id==CHEAT_UNUSED) return;
    auto fn=CCheat::m_aCheatFunctions[id];
    if(fn) fn(); else CCheat::m_aCheatsActive[id]=!CCheat::m_aCheatsActive[id];
    CCheat::m_bHasPlayerCheated=true;
}
void ReleaseRequest() {
    if(pendingWeapon<0) return;
    auto* i=CWeaponInfo::GetWeaponInfo(static_cast<eWeaponType>(pendingWeapon),1);
    if(i) { if(i->m_nModelId>0) CStreaming::SetModelIsDeletable(i->m_nModelId); if(i->m_nModelId2>0) CStreaming::SetModelIsDeletable(i->m_nModelId2); }
    pendingWeapon=-1;
}
void Give(int t) {
    ReleaseRequest();
    auto* i=CWeaponInfo::GetWeaponInfo(static_cast<eWeaponType>(t),1);
    if(!i || i->m_nSlot>=13) { Say("Weapon unavailable"); return; }
    pendingWeapon=t; pendingAt=GetTickCount();
    if(i->m_nModelId>0) CStreaming::RequestModel(i->m_nModelId,2);
    if(i->m_nModelId2>0) CStreaming::RequestModel(i->m_nModelId2,2);
    Say("Loading weapon...");
}
void Activate(CPlayerPed* p, bool remove) {
    if(page==0) { page=selected+1; selected=0; return; }
    if(page==1) {
        if(selected==0) { god=!god; Say(god?"God mode ON":"God mode OFF"); }
        else { infinite=!infinite; if(infinite) savedInfinite=CCheat::m_aCheatsActive[CHEAT_INFINITE_AMMO]; else CCheat::m_aCheatsActive[CHEAT_INFINITE_AMMO]=savedInfinite;
            Say(infinite?"Unlimited ammo / no reload ON":"Unlimited ammo / no reload OFF"); }
    } else if(page==2) {
        int t=WeaponRow(selected); auto* i=CWeaponInfo::GetWeaponInfo(static_cast<eWeaponType>(t),1);
        if(remove) { if(t && i && i->m_nSlot<13 && p->m_aWeapons[i->m_nSlot].m_eWeaponType==t) { ReleaseRequest(); if(p->m_nSelectedWepSlot==i->m_nSlot) p->SetCurrentWeapon(0); p->ClearWeapon(static_cast<eWeaponType>(t)); Say("Weapon removed"); } }
        else Give(t);
    } else if(const auto* c=CheatRow(selected)) { NativeCheat(c->id); Say(c->name); }
}
RwRaster* Icon(int type);
void BuildWheel(CPlayerPed* p) {
    count=0; hover=0;
    for(int s=0;s<13;++s) {
        int t=static_cast<int>(p->m_aWeapons[s].m_eWeaponType);
        if(!arsenal::WeaponType(t) || (s && t==0)) continue;
        slots[count]=s; types[count]=t; if(s==p->m_nSelectedWepSlot) hover=count; ++count;
    }
    wheel=count>0;
    if(logLine) {
        int icons=0; for(int n=0;n<count;++n) if(Icon(types[n])) ++icons;
        logLine("arsenal: wheel opened, %d carried slots, %d native icons",count,icons);
    }
}
RwRaster* Icon(int type) {
    if(type==0) return CHud::Sprites[HUDSPRITE_FIST].m_pTexture ? CHud::Sprites[HUDSPRITE_FIST].m_pTexture->raster : nullptr;
    auto* info=CWeaponInfo::GetWeaponInfo(static_cast<eWeaponType>(type),1);
    if(!info || info->m_nModelId<=0) return nullptr;
    auto* model=CModelInfo::GetModelInfo(info->m_nModelId);
    if(!model || model->m_nTxdIndex<0 || !CTxdStore::ms_pTxdPool) return nullptr;
    auto* txd=CTxdStore::ms_pTxdPool->GetAt(model->m_nTxdIndex);
    if(!txd || !txd->m_pRwDictionary) return nullptr;
    auto* texture=RwTexDictionaryFindHashNamedTexture(txd->m_pRwDictionary,CKeyGen::AppendStringToKey(model->m_nKey,"ICON"));
    return texture?texture->raster:nullptr;
}
void Text(float x,float y,const char* text,float size=0.65f,bool centre=true,CRGBA color=CRGBA(255,255,255,255)) {
    float w=static_cast<float>(RsGlobal.maximumWidth),h=static_cast<float>(RsGlobal.maximumHeight);
    CFont::SetBackground(false,false); CFont::SetProportional(true); CFont::SetJustify(false);
    CFont::SetOrientation(centre?ALIGN_CENTER:ALIGN_LEFT); CFont::SetCentreSize(w*.8f); CFont::SetWrapx(w*.9f);
    CFont::SetFontStyle(FONT_MENU); CFont::SetScale(w/640*.5f*size,h/448*size);
    CFont::SetColor(color); CFont::SetEdge(1); CFont::SetDropColor(CRGBA(0,0,0,255)); CFont::PrintString(x,y,text);
}
void Vertex(RwIm2DVertex& v,float x,float y,CRGBA c,float u=0,float vv=0) {
    RwIm2DVertexSetScreenX(&v,x); RwIm2DVertexSetScreenY(&v,y); RwIm2DVertexSetScreenZ(&v,RwIm2DGetNearScreenZ());
    RwIm2DVertexSetRecipCameraZ(&v,1); RwIm2DVertexSetIntRGBA(&v,c.r,c.g,c.b,c.a);
    RwIm2DVertexSetU(&v,u,1); RwIm2DVertexSetV(&v,vv,1);
}
void Picture(RwRaster* raster,float x,float y,float size) {
    RwIm2DVertex v[4]={}; CRGBA white(255,255,255,255);
    Vertex(v[0],x-size,y-size,white,0,0); Vertex(v[1],x+size,y-size,white,1,0);
    Vertex(v[2],x-size,y+size,white,0,1); Vertex(v[3],x+size,y+size,white,1,1);
    RwRenderStateSet(rwRENDERSTATETEXTURERASTER,raster); RwIm2DRenderPrimitive(rwPRIMTYPETRISTRIP,v,4);
}
void Segment(float cx,float cy,float inner,float outer,float start,float end,CRGBA color) {
    RwIm2DVertex v[34]={};
    for(int n=0;n<=16;++n) { float a=start+(end-start)*n/16;
        Vertex(v[n*2],cx+inner*std::sin(a),cy-inner*std::cos(a),color);
        Vertex(v[n*2+1],cx+outer*std::sin(a),cy-outer*std::cos(a),color); }
    RwRenderStateSet(rwRENDERSTATETEXTURERASTER,nullptr); RwIm2DRenderPrimitive(rwPRIMTYPETRISTRIP,v,34);
}
struct RenderState {
    RwRenderState ids[9]={rwRENDERSTATETEXTURERASTER,rwRENDERSTATEZTESTENABLE,rwRENDERSTATEZWRITEENABLE,rwRENDERSTATEVERTEXALPHAENABLE,rwRENDERSTATESRCBLEND,rwRENDERSTATEDESTBLEND,rwRENDERSTATETEXTUREFILTER,rwRENDERSTATETEXTUREADDRESS,rwRENDERSTATEFOGENABLE};
    void* values[9]={};
    RenderState() { for(int i=0;i<9;++i) RwRenderStateGet(ids[i],&values[i]);
        RwRenderStateSet(ids[1],nullptr); RwRenderStateSet(ids[2],nullptr); RwRenderStateSet(ids[3],reinterpret_cast<void*>(TRUE));
        RwRenderStateSet(ids[4],reinterpret_cast<void*>(rwBLENDSRCALPHA)); RwRenderStateSet(ids[5],reinterpret_cast<void*>(rwBLENDINVSRCALPHA));
        RwRenderStateSet(ids[6],reinterpret_cast<void*>(rwFILTERLINEAR)); RwRenderStateSet(ids[7],reinterpret_cast<void*>(rwTEXTUREADDRESSCLAMP)); RwRenderStateSet(ids[8],nullptr); }
    ~RenderState() { for(int i=0;i<9;++i) RwRenderStateSet(ids[i],values[i]); }
};
void Draw() {
    if(Paused() || !Foreground()) return;
    auto* p=FindPlayerPed(); if(!p) return;
    float w=static_cast<float>(RsGlobal.maximumWidth),h=static_cast<float>(RsGlobal.maximumHeight);
    RenderState state;
    if(wheel && count && hover<count) {
        float cx=w*.5f,cy=h*.48f;
        for(int n=0;n<count;++n) {
            float a=2*pi*n/count,half=pi/count;
            Segment(cx,cy,h*.17f,h*.39f,a-half+.012f,a+half-.012f,n==hover?CRGBA(100,155,210,225):CRGBA(15,20,25,200));
            float x=cx+h*.28f*std::sin(a),y=cy-h*.28f*std::cos(a);
            if(auto* icon=Icon(types[n])) Picture(icon,x,y,h*.047f);
            else Text(x,y,WeaponName(types[n]),.40f);
            char ammo[48]; const auto& gun=p->m_aWeapons[slots[n]];
            sprintf_s(ammo,"%u",gun.m_nAmmoTotal); if(types[n]>=16) Text(x,y+h*.052f,ammo,.43f);
            if(slots[n]==p->m_nSelectedWepSlot) Text(x,y-h*.075f,"EQUIPPED",.38f,true,CRGBA(230,190,100,255));
        }
        Text(cx,cy-h*.045f,WeaponName(types[hover]),.85f);
        char ammo[64]; const auto& gun=p->m_aWeapons[slots[hover]];
        sprintf_s(ammo,"%u in clip / %u total",gun.m_nAmmoInClip,gun.m_nAmmoTotal);
        if(types[hover]>=16) Text(cx,cy+h*.005f,ammo,.55f);
        Text(cx,h*.91f,"Right stick: select   Release Back: equip   Circle: cancel",.6f);
    }
    if(menu) {
        CSprite2d::DrawRect(CRect(w*.18f,h*.09f,w*.82f,h*.88f),CRGBA(10,14,20,235));
        Text(w*.5f,h*.12f,pages[page],1.05f);
        int n=RowCount(), top=std::max(0,selected-9), end=std::min(n,top+10);
        for(int row=top;row<end;++row) {
            float y=h*(.22f+(row-top)*.048f);
            if(row==selected) CSprite2d::DrawRect(CRect(w*.20f,y-h*.004f,w*.80f,y+h*.039f),CRGBA(70,115,155,235));
            char label[160]={};
            if(page==0) sprintf_s(label,"%s  >",pages[row+1]);
            else if(page==1) sprintf_s(label,"%s: %s",row==0?"God mode":"Unlimited ammo / no reload",(row==0?god:infinite)?"ON":"OFF");
            else if(page==2) { int t=WeaponRow(row); auto* i=CWeaponInfo::GetWeaponInfo(static_cast<eWeaponType>(t),1);
                bool own=i && i->m_nSlot<13 && p->m_aWeapons[i->m_nSlot].m_eWeaponType==t;
                sprintf_s(label,"%s%s",WeaponName(t),own?"  [owned]":""); }
            else if(const auto* c=CheatRow(row)) sprintf_s(label,"%s%s",c->name,CCheat::m_aCheatsActive[c->id]?"  [ON]":"");
            Text(w*.23f,y,label,.65f,false);
        }
        char pos[40]; sprintf_s(pos,"%d / %d",selected+1,n); Text(w*.5f,h*.73f,pos,.5f);
        Text(w*.5f,h*.78f,"Arrows: move   Enter: select   Backspace: back   Insert: close",.48f);
        if(page==2) Text(w*.5f,h*.82f,"Enter: give/equip   Delete: remove   One weapon per GTA slot",.48f);
    }
    if(GetTickCount()<messageUntil && message[0]) Text(w*.5f,h*.94f,message,.65f);
}
void Frame(LogFn log) {
    logLine=log;
    bool toggle=Press(VK_INSERT), up=Press(VK_UP),down=Press(VK_DOWN),enter=Press(VK_RETURN),back=Press(VK_BACK),del=Press(VK_DELETE);
    auto* p=FindPlayerPed();
    auto* pad=CPad::GetPad(0);
    bool scripted=pad && (pad->DisablePlayerControls & ~(1u<<4))!=0;
    bool active=Foreground()&&!Paused()&&p&&p->m_fHealth>0&&!scripted;
    if(!active) { if(menu) guard=true; menu=false; if(wheel) CloseWheel(false); backDown=false;backUsed=true;taps=0;equip=-1; ReleaseRequest(); return; }
    if(toggle && !SpawnerIsOpen()) { menu=!menu; if(wheel) CloseWheel(false); guard=true; backDown=false; backUsed=true; taps=0; Say(menu?"Cheat menu opened (Insert to close)":"Cheat menu closed"); }
    if(up||down) repeatAt=GetTickCount()+350;
    else if(menu && (keys[VK_UP]||keys[VK_DOWN]) && static_cast<LONG>(GetTickCount()-repeatAt)>=0) {up=keys[VK_UP];down=!up&&keys[VK_DOWN];repeatAt=GetTickCount()+90;}
    if(menu) { if(up) selected=arsenal::Wrap(selected-1,RowCount()); if(down) selected=arsenal::Wrap(selected+1,RowCount());
        if(back) { if(page) {page=0;selected=0;} else {menu=false;guard=true;} }
        else if(enter || (del&&page==2)) Activate(p,del); }
    Protect(p);
    if(wheel&&!ShootModeIsOn()) CloseWheel(false);
    if(equip>=0) { if(equip<13 && p->m_aWeapons[equip].m_eWeaponType==equipType) {p->SetCurrentWeapon(equip); Say(WeaponName(equipType));} equip=-1; }
    if(pendingWeapon>=0) {
        auto* i=CWeaponInfo::GetWeaponInfo(static_cast<eWeaponType>(pendingWeapon),1);
        bool loaded=i&&(i->m_nModelId<=0||CStreaming::HasModelLoaded(i->m_nModelId))&&(i->m_nModelId2<=0||CStreaming::HasModelLoaded(i->m_nModelId2));
        if(loaded) { int t=pendingWeapon; p->GiveWeapon(static_cast<eWeaponType>(t),500,true); p->SetCurrentWeapon(static_cast<eWeaponType>(t)); ReleaseRequest(); Say(WeaponName(t)); }
        else if(GetTickCount()-pendingAt>5000) {ReleaseRequest();Say("Weapon model could not be loaded");}
    }
}
} // namespace
bool ArsenalBlocksInput() { return menu||wheel||guard; }
void ArsenalFilterPad(LONG& buttons,short* axes) {
    const LONG raw=buttons;
    if(menu || guard) {
        bool held=raw!=0; for(int i=0;i<kAxisCount;++i) if(std::abs(static_cast<int>(axes[i]))>30) held=true;
        if(!menu&&!held) guard=false;
        buttons=0; std::memset(axes,0,sizeof(short)*kAxisCount); return;
    }
    if(!ShootModeIsOn() || !Foreground() || Paused()) {
        if(wheel) {CloseWheel(false);buttons=0;std::memset(axes,0,sizeof(short)*kAxisCount);}
        backDown=false; taps=0; return;
    }
    bool back=(raw&kBtnSelect)!=0;
    buttons&=~kBtnSelect;
    if(back) {
        if(!backDown) {backDown=true;backUsed=false;backAt=GetTickCount();}
        if(!backUsed && GetTickCount()-backAt>=400) {backUsed=true; if(auto* p=FindPlayerPed()) BuildWheel(p);}
    } else if(backDown) { backDown=false; if(wheel) CloseWheel(true); else if(!backUsed) taps=2; }
    if(wheel) {
        hover=arsenal::WheelSlot(axes[kAxisRX]/128.0f,axes[kAxisRY]/128.0f,count,hover);
        if(raw&kBtnCircle) {CloseWheel(false);backUsed=true;}
        buttons=0; std::memset(axes,0,sizeof(short)*kAxisCount);
    } else if(guard) {buttons=0;std::memset(axes,0,sizeof(short)*kAxisCount);}
    else if(taps>0) {buttons|=kBtnSelect;--taps;}
}
void ArsenalFrame(LogFn log) {
    __try { Frame(log); } __except(EXCEPTION_EXECUTE_HANDLER) {menu=wheel=false;guard=true;if(log)log("arsenal: exception %08lX",GetExceptionCode());}
}
void ArsenalAddHook() {
    plugin::Events::drawHudEvent.Add([] { Draw(); });
    plugin::Events::reInitGameEvent.Add([] {
        // GTA has reset its world/cheats; do not restore values from the previous save.
        menu=wheel=guard=backDown=backUsed=god=infinite=false;
        protectedPed=pendingWeapon=equip=-1; taps=0; page=selected=0; messageUntil=0;
    });
}
}
