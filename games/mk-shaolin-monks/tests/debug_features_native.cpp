#include "ps2_runtime.h"
#include "runtime/ee_scheduler.h"
#include "../runtime/kernel-output/mk_recovered_0024e680_0x24e680.cpp"
#include "ps2_runtime_macros.h"
#include "runtime/gs/gs_frontend.h"
#include <cstring>
#include <iostream>
#include <sstream>
#include <cstdlib>
#include <vector>
#include "../runtime/kernel-output/mk_debug_SelectFreeCam_0x1aa558.cpp"
#include "../runtime/kernel-output/mk_debug_SelectFreezeCam_0x1aa5b8.cpp"
#include "../runtime/kernel-output/mk_debug_SetGodModeText_0x1aae10.cpp"
#include "../runtime/kernel-output/mk_file_GetAsyncFileSize_0x211878.cpp"
#include "../runtime/kernel-output/mk_recovered_00384158_0x384158.cpp"
#include "../runtime/kernel-output/mk_recovered_003841d0_0x3841d0.cpp"
#include "../runtime/kernel-output/mk_recovered_00384600_0x384600.cpp"
#include "../runtime/kernel-output/mk_recovered_001d32e0_0x1d32e0.cpp"
#include "../runtime/debug-retail-controls.h"
#include "../runtime/kernel-output/mk_recovered_00382f40_0x382f40.cpp"
#include "../runtime/kernel-output/mk_recovered_003832b8_0x3832b8.cpp"
using Memory=std::vector<uint8_t>;
#include "cri_resume_native.h"
#include "cri_ring_native.h"
#include "cri_queue_native.h"
#include "gif_builder_native.h"
#include "vif_dma_native.h"
#include "gs_scanout_native.h"
#include "pad_dma_native.h"
#include "movie_display_native.h"
#include "controller_profile_native.h"
#include "rpc_trace_native.h"
#include "sound_upload_native.h"
#include "sndf_pitch_native.h"
#include "spu_adpcm_native.h"
static void put(Memory& m,uint32_t a,uint32_t v){std::memcpy(m.data()+a,&v,4);}
int main(int argc,char** argv){
 if(argc==2 && std::strcmp(argv[1],"--missing-report")==0){
  const auto env=[](const char* value){
#ifdef _WIN32
   _putenv_s("PS2_MISSING_TARGET_LIMIT",value?value:"");
#else
   if(value)setenv("PS2_MISSING_TARGET_LIMIT",value,1);else unsetenv("PS2_MISSING_TARGET_LIMIT");
#endif
  };
  const auto emit=[](PS2Runtime& runtime,uint32_t target){
   R5900Context ctx{};ctx.pc=0x100000;ctx.vu0_status=0x123;
   runtime.reportMissingFunction(nullptr,&ctx,target,0x100000,PS2Runtime::GuestBranchKind::IndirectCall,"test");
   return ctx.pc==target&&ctx.vu0_status==0x123;
  };
  const auto count=[](const std::string& text){size_t n=0,pos=0;while((pos=text.find("[guest-branch:missing-target]",pos))!=std::string::npos){++n;++pos;}return n;};
  env("999"); // User input is clamped to the bounded maximum of 16.
  auto first=std::make_unique<PS2Runtime>();first->setMissingFunctionPolicy(PS2Runtime::MissingFunctionPolicy::ContinueToTarget);
  std::ostringstream capture;auto* previous=std::cerr.rdbuf(capture.rdbuf());bool ok=true;
  for(uint32_t i=0;i<24;++i){ok&=emit(*first,0x180000+i*4);ok&=emit(*first,0x180000+i*4);}
  ok&=count(capture.str())==16;
  auto second=std::make_unique<PS2Runtime>();second->setMissingFunctionPolicy(PS2Runtime::MissingFunctionPolicy::ContinueToTarget);
  ok&=emit(*second,0x180000);ok&=count(capture.str())==17;
  first->resetMissingFunctionReportOnce();ok&=emit(*first,0x180000);ok&=count(capture.str())==18;
  env(nullptr);
  auto ordinary=std::make_unique<PS2Runtime>();ordinary->setMissingFunctionPolicy(PS2Runtime::MissingFunctionPolicy::ContinueToTarget);
  ok&=emit(*ordinary,0x180000);ok&=emit(*ordinary,0x180004);ok&=count(capture.str())==19;
  std::cerr.rdbuf(previous);
  std::cout<<(ok?"PASS":"FAIL")<<": bounded distinct missing targets, duplicates, reset, runtime isolation, default-once behavior, and guest PC preservation\n";
  return ok?0:1;
 }

 // Bound a busy wait at its first scheduler checkpoint so both branch outcomes
 // can be tested without a host thread or a potentially infinite loop.
 {
  auto branchRuntime=std::make_unique<PS2Runtime>();
  if(!branchRuntime->memory().initialize())return 1;
  branchRuntime->eeScheduler().requestStop();
  auto* branchRam=branchRuntime->memory().getRDRAM();unsigned branchCases=0;
  for(uint16_t status:{uint16_t(0),uint16_t(1),uint16_t(0xffff)})
   for(uint32_t vpu:{0u,1u,0x100u,0x101u,0x200u,0x400u,0x600u,0x700u}){
    R5900Context ctx{};ctx.pc=0x24e770;ctx.vu0_status=status;ctx.vu0_vpu_stat=vpu;
    SET_GPR_U32(&ctx,31,0x80000);
    mk_recovered_0024e680_0x24e680(branchRam,&ctx,branchRuntime.get());
    const uint32_t expected=(vpu&0x100u)?0x24e770u:0x80000u;
    if(ctx.pc!=expected||ctx.vu0_status!=status||ctx.vu0_vpu_stat!=vpu){
     std::cerr<<"FAIL retail BC2 wait: status="<<status<<" vpu="<<vpu<<" pc="<<ctx.pc<<"\n";return 1;
    }
    ++branchCases;
   }
  std::cout<<"PASS: "<<branchCases<<" original retail BC2 wait cases distinguish VU1 busy from arithmetic and stop flags\n";
 }
 if(argc==2 && std::strcmp(argv[1],"--vu-branch")==0)return 0;

 if(!checkSpuAdpcm()){std::cerr<<"FAIL retail SPU ADPCM/VAG PCM\n";return 1;}
 std::cout<<"PASS: 22736 retail ADPCM samples match Python, predictor rounding/saturation, shifts and VAG bounds\n";
 if(!checkSndfPitchBase()){std::cerr<<"FAIL retail sound pitch double ABI\n";return 1;}
 std::cout<<"PASS: retail soft-double sound pitch chain, startup base zero, log ABI and special values\n";
 if(!checkRpcAliasedPreview()){std::cerr<<"FAIL RPC aliased request trace or IOP allocator bounds\n";return 1;}
 std::cout<<"PASS: aliased RPC previews retain request bytes; IOP allocation bounds and release\n";
 if(!checkSoundMemoryUploads()){std::cerr<<"FAIL sound RAM upload/readback/status\n";return 1;}
 std::cout<<"PASS: IOP sound uploads copy bytes before completion, bounds, n32 zero args, isolation and reset\n";
 if(!checkControllerProfiles()){std::cerr<<"FAIL controller profile mapping\n";return 1;}
 std::cout<<"PASS: controller profiles, independent ports, button remaps, trigger thresholds and Python stick fixtures\n";
 if(!checkGsDrawEnvironmentClear()){std::cerr<<"FAIL complete draw environment color/depth clear\n";return 1;}
 if(!checkRetailMovieDisplay()){std::cerr<<"FAIL original retail movie display setup\n";return 1;}
 std::cout<<"PASS: original movie field mode, display dimensions, clear packets and framebuffer bounds\n";
 if(argc==2 && std::strcmp(argv[1],"--pcm-device")==0) {
   const bool ok=checkPcmDevice();
   std::cout<<(ok?"PASS":"FAIL")<<": host PCM device pull, underrun, pause/resume and close\n";
   return ok?0:1;
 }
 if(!checkGifBuilderFinalization()){std::cerr<<"FAIL mixed GIF builder doubles DMA length\n";return 1;}
 if(!checkPadDmaStatus()){std::cerr<<"FAIL pad DMA status differs from scePadRead output\n";return 1;}
 std::cout<<"PASS: pad DMA banks and original retail neutral/Cross/release wrapper\n";
 if(!checkGsFieldWeaving()){std::cerr<<"FAIL interlaced field history\n";return 1;}
 std::cout<<"PASS: complementary field rows update independently; movie/progressive modes and reset clear history\n";
 if(!checkGsInterlacedFrameRead()){std::cerr<<"FAIL interlaced frame scanout reads adjacent framebuffer\n";return 1;}
 std::cout<<"PASS: both display circuits read half-height INT+FFMD frames and preserve progressive rows\n";
 if(!checkVifDmaTagTransfer()){std::cerr<<"FAIL VIF DMA tag transfer loses MPG or ignores TTE\n";return 1;}
 std::cout<<"PASS: VIF0/VIF1 REF/REFS/REFE MPG headers and TTE-disabled inline stream\n";
 std::cout<<"PASS: retail GIF builder finalizes each DMA length once\n";
 if(!checkPcmQueue()){std::cerr<<"FAIL PCM ring and consumed-frame clock\n";return 1;}
 std::cout<<"PASS: bounded PCM callback ring, wrap, pressure and silent underrun clock\n";
 if(!checkCriSurvivesRpcInit()){std::cerr<<"FAIL repeated SifInitRpc resets remote CRI objects\n";return 1;}
 std::cout<<"PASS: repeated RPC init preserves CRI objects; explicit reset removes them\n";
 if(!checkCriDescriptorQueue()){std::cerr<<"FAIL CRI descriptor queue\n";return 1;}
 std::cout<<"PASS: 34 native CRI queue steps match original IOP instructions\n";
 if(!checkCriVideoRing() || !checkMpegGuestLayout()){std::cerr<<"FAIL CRI video ring or MPEG SDK layout\n";return 1;}
 std::cout<<"PASS: CRI FIFO wrap/backpressure/bounds and MPEG SDK field isolation\n";
 if(!checkCriResume()){std::cerr<<"FAIL retail CRI resume/wakeup syscall contract\n";return 1;}
 std::cout<<"PASS: retail CRI resume/wakeup chain and signed syscall errors\n";
 PS2Runtime runtime;unsigned cases=0;
 // Stop at the first file-open preparation call, after original WAD decoding.
 runtime.registerFunction(0x211148,[](uint8_t*,R5900Context* ctx,PS2Runtime*){ctx->pc=0x80000;});
 struct Entry {uint64_t raw;uint32_t offset,stored,unpacked;};
 for(const auto entry:{Entry{0x803aa0026d400028ull,0x14000,0x9b5,0x1d50},
                       Entry{0x82085001f580001cull,0xe000,0x7d6,0x10428},
                       Entry{0x000000c800013800ull,0x13800,0xc8,0xc8}}){
  Memory ram(PS2_RAM_SIZE);put(ram,0x4ef7d0,1);put(ram,0x4ef7d4,1);put(ram,0x651e1c,0x100000);put(ram,0x100008,2);
  std::memcpy(ram.data()+0x100020,&entry.raw,8);
  R5900Context ctx{};ctx.pc=0x211878;SET_GPR_U32(&ctx,4,30);SET_GPR_U32(&ctx,5,0x90000);SET_GPR_U32(&ctx,29,0x1e00000);SET_GPR_U32(&ctx,31,0x80000);
  mk_file_GetAsyncFileSize_0x211878(ram.data(),&ctx,&runtime);
  uint32_t offset,stored,unpacked;std::memcpy(&offset,ram.data()+0x651e30,4);std::memcpy(&stored,ram.data()+0x651e34,4);std::memcpy(&unpacked,ram.data()+0x90000,4);
  if(ctx.pc!=0x80000||offset!=entry.offset||stored!=entry.stored||unpacked!=entry.unpacked){std::cerr<<"FAIL retail packed WAD "<<std::hex<<entry.raw<<" pc="<<ctx.pc<<" result="<<offset<<","<<stored<<","<<unpacked<<"\n";return 1;}++cases;
 }

 for(unsigned object:{0u,1u,3u})for(bool freeze:{false,true}){
  Memory actual(PS2_RAM_SIZE);const uint32_t trigger=0x71e4e0+2*0x600-0x570;
  put(actual,0x50c134,object);put(actual,0x5e3780+object*0x33c0+0x68,2);put(actual,trigger,0xabcdef);
  Memory expected=actual;put(expected,0x511378,freeze?4:1);put(expected,0x5d1e4c,1);
  if(object)put(expected,trigger,1);
  if(freeze){put(expected,0x519ab4,1);put(expected,0x6bee10,1);}
  R5900Context ctx{};ctx.pc=freeze?0x1aa5b8:0x1aa558;SET_GPR_U32(&ctx,31,0x80000);
  if(freeze)mk_debug_SelectFreezeCam_0x1aa5b8(actual.data(),&ctx,&runtime);
  else mk_debug_SelectFreeCam_0x1aa558(actual.data(),&ctx,&runtime);
  if(actual!=expected||ctx.pc!=0x80000){std::cerr<<"FAIL camera fixture "<<object<<" "<<freeze<<"\n";return 1;}++cases;
 }
 for(unsigned god:{0u,1u,2u}){
  Memory actual(PS2_RAM_SIZE);put(actual,0x4c7778,god);put(actual,0x90000,0x666);put(actual,0x90008,0x667);put(actual,0x90010,99);put(actual,0x90018,0xffffffff);
  Memory expected=actual;put(expected,0x90000,god==1?0x667:0x666);put(expected,0x90008,god==1?0x667:0x666);
  R5900Context ctx{};ctx.pc=0x1aae10;SET_GPR_U32(&ctx,31,0x80000);SET_GPR_U32(&ctx,4,0x90000);
  mk_debug_SetGodModeText_0x1aae10(actual.data(),&ctx,&runtime);
  if(actual!=expected||ctx.pc!=0x80000){std::cerr<<"FAIL god-mode text fixture "<<god<<"\n";return 1;}++cases;
 }
 for(uint32_t originalFlag:{0u,1u,2u}) {
  Memory ram(PS2_RAM_SIZE);put(ram,0x513a04,originalFlag);
  R5900Context ctx{};ctx.pc=0x1d32e0;SET_GPR_U32(&ctx,29,0x1e00000);SET_GPR_U32(&ctx,31,0x80000);
  mkDebugSkipIntro(ram.data(),&ctx,&runtime);
  uint32_t restored;std::memcpy(&restored,ram.data()+0x513a04,4);
  if(ctx.pc!=0x80000||getRegU32(&ctx,2)!=3||getRegU32(&ctx,29)!=0x1e00000||restored!=originalFlag){std::cerr<<"FAIL scoped retail intro gate\n";return 1;}
  ++cases;
 }
 if(!runtime.memory().initialize())return 1;
 runtime.registerFunction(0x384158,mk_recovered_00384158_0x384158);
 runtime.registerFunction(0x3841d0,mk_recovered_003841d0_0x3841d0);
 auto& mem=runtime.memory();
 runtime.registerFunction(0x206258,[](uint8_t*,R5900Context* ctx,PS2Runtime*){
  SET_GPR_U32(ctx,2,0x4edc60);ctx->pc=getRegU32(ctx,31);
 });
 struct DisplayCase {uint16_t params[4];int args[5];uint64_t expected[5];};
 const DisplayCase displays[]={
#include "gs_display_cases.inc"
 };
 for(const auto& d:displays){
  std::memcpy(mem.getRDRAM()+0x4edc60,d.params,sizeof(d.params));
  std::memset(mem.getRDRAM()+0x90000,0xcd,64);
  R5900Context ctx{};ctx.pc=0x382f40;
  SET_GPR_U32(&ctx,4,0x20090000); // Retail uses this uncached EE RAM alias.
  for(unsigned i=0;i<5;++i){SET_GPR_S32(&ctx,5+i,d.args[i]);}
  SET_GPR_U32(&ctx,29,0x1e00000);SET_GPR_U32(&ctx,31,0x80000);
  mk_recovered_00382f40_0x382f40(mem.getRDRAM(),&ctx,&runtime);
  if(ctx.pc!=0x80000||getRegU32(&ctx,29)!=0x1e00000||std::memcmp(mem.getRDRAM()+0x90000,d.expected,40)!=0){std::cerr<<"FAIL retail GS setup vs Python instruction probe\n";return 1;}
  for(unsigned i=40;i<64;++i)if(mem.getRDRAM()[0x90000+i]!=0xcd){std::cerr<<"FAIL GS env overwrite\n";return 1;}
  mem.gs().dispfb1=0x1234;mem.gs().display1=0x5678;
  ctx.pc=0x3832b8;SET_GPR_U32(&ctx,4,0x20090000);SET_GPR_U32(&ctx,31,0x80000);
  mk_recovered_003832b8_0x3832b8(mem.getRDRAM(),&ctx,&runtime);
  const auto& g=mem.gs();
  if(ctx.pc!=0x80000||g.pmode!=d.expected[0]||g.smode2!=d.expected[1]||g.dispfb2!=d.expected[2]||g.display2!=d.expected[3]||g.bgcolor!=0||g.dispfb1!=0x1234||g.display1!=0x5678){std::cerr<<"FAIL retail GS private register writes\n";return 1;}
  ++cases;
 }
 if(!runtime.syncCoreSubsystems())return 1;
 {
  auto& g=mem.gs();g.dispfb1=0x1400;g.display1=0x1bf9ff0183227cull;
  g.dispfb2=0x1446;g.display2=g.display1;g.bgcolor=0x123456;
  // Observed corrupt display payload must not reach private MMIO via reserved A+D addresses.
  const uint64_t packet[]={0x1000000000008006ull,0xe,
   0x8000033c01e086cbull,0x59,0x8000033c01d4a1ffull,0x5a,
   0x8153933c01d494acull,0x5b,0x8153933c01d494acull,0x5c,
   0xfedcba9876543210ull,0x5f,0x00df0000027full,0x41};
  runtime.gs().processGIFPacket(reinterpret_cast<const uint8_t*>(packet),sizeof(packet));
  const auto snap=runtime.gs().getDebugSnapshot();
  if(g.dispfb1!=0x1400||g.dispfb2!=0x1446||g.display1!=0x1bf9ff0183227cull||g.display2!=g.display1||g.bgcolor!=0x123456){std::cerr<<"FAIL reserved GIF addresses corrupted private GS registers\n";return 1;}
  if(snap.ctx[1].scissor.x0!=639){std::cerr<<"FAIL valid GIF SCISSOR_2 write lost\n";return 1;}
  ++cases;
  const auto beforeDma=mem.dmaStartCount();
  R5900Context reset{};SET_GPR_U32(&reset,4,0);SET_GPR_U32(&reset,5,1);SET_GPR_U32(&reset,6,2);SET_GPR_U32(&reset,7,1);
  ps2_stubs::sceGsResetGraph(mem.getRDRAM(),&reset,&runtime);
  if(g.pmode!=0x8005||g.smode2!=3||g.dispfb1!=0x1400||g.dispfb2!=0x1400||g.display1!=g.display2||g.bgcolor!=0||mem.dmaStartCount()!=beforeDma||runtime.gs().getDebugSnapshot().ctx[1].scissor.x0!=639){std::cerr<<"FAIL GS reset private MMIO separation\n";return 1;}
  ++cases;
 }
 for(uint32_t i=0;i<16384;++i)mem.getScratchpad()[i]=static_cast<uint8_t>(i*37+11);
 {
  // SDK scratchpad DMAtag pointer 0x80000000 must not become a RAM-zero chain.
  const uint64_t chain[]{0x70000002,0,0x1000000000008001ull,0xe,0x00340012,0x40};
  for(uint32_t address:{0x80000000u,0x80000100u,0x70000200u,0x22000u}) {
   runtime.gs().writeRegister(0x40,0);
   uint8_t* target=address==0x22000?mem.getRDRAM()+address:mem.getScratchpad()+(address&0x3fff);
   std::memcpy(target,chain,sizeof(chain));
   R5900Context send{};SET_GPR_U32(&send,4,0x1000a000);SET_GPR_U32(&send,5,address);
   ps2_stubs::sceDmaSend(mem.getRDRAM(),&send,&runtime);
   const auto scissor=runtime.gs().getDebugSnapshot().ctx[0].scissor;
   if(scissor.x0!=0x12 || scissor.x1!=0x34){std::cerr<<"FAIL SDK scratchpad GIF chain "<<std::hex<<address<<"\n";return 1;}
  }
  std::cout<<"PASS: SDK encoded/canonical scratchpad and main RAM GIF chains\n";
 }
 mem.writeIORegister(0x1000d080,0);
 R5900Context dma{};dma.pc=0x384600;
 SET_GPR_U32(&dma,4,0x1000d000);SET_GPR_U32(&dma,5,0x75f100);SET_GPR_U32(&dma,6,0x400);
 SET_GPR_U32(&dma,29,0x1e00000);SET_GPR_U32(&dma,31,0x80000);
 mk_recovered_00384600_0x384600(mem.getRDRAM(),&dma,&runtime);
 if(dma.pc!=0x80000||std::memcmp(mem.getRDRAM()+0x75f100,mem.getScratchpad(),16384)!=0||mem.readIORegister(0x1000d010)!=0x763100){std::cerr<<"FAIL retail sceDmaRecvN transfer\n";return 1;}
 ++cases;
 std::cout<<"PASS: "<<cases<<" native fixtures: WAD decoder, camera/text, scoped intro gate, GS display, retail sceDmaRecvN\n";
}
