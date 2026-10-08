#include "ps2recomp/code_generator.h"
#include "ps2recomp/types.h"
#include "ps2recomp/instructions.h"
#include "ps2recomp/r5900_decoder.h"
#include <sstream>
#include <iomanip>
#include <iostream>
using namespace ps2recomp;
int main(){
 struct Case {uint32_t opcode;uint8_t rt;const char* comparison;};
 const Case cases[]={{OPCODE_BLEZ,0,"<= 0"},{OPCODE_BLEZL,0,"<= 0"},{OPCODE_BGTZ,0,"> 0"},{OPCODE_BGTZL,0,"> 0"},
 {OPCODE_REGIMM,REGIMM_BLTZ,"< 0"},{OPCODE_REGIMM,REGIMM_BLTZL,"< 0"},{OPCODE_REGIMM,REGIMM_BLTZAL,"< 0"},{OPCODE_REGIMM,REGIMM_BLTZALL,"< 0"},
 {OPCODE_REGIMM,REGIMM_BGEZ,">= 0"},{OPCODE_REGIMM,REGIMM_BGEZL,">= 0"},{OPCODE_REGIMM,REGIMM_BGEZAL,">= 0"},{OPCODE_REGIMM,REGIMM_BGEZALL,">= 0"}};
 const std::vector<Symbol> symbols;const std::vector<Section> sections;
 for(auto c:cases){
  Function f{};f.name="branch64";f.start=0x1000;f.end=0x1010;f.isRecompiled=true;
  Instruction b{};b.address=0x1000;b.opcode=c.opcode;b.rs=6;b.rt=c.rt;b.immediate=b.simmediate=2;b.raw=(c.opcode<<26)|(6<<21)|(c.rt<<16)|2;b.isBranch=true;b.hasDelaySlot=true;
  std::vector<Instruction> ins{b};for(uint32_t a=0x1004;a<0x1010;a+=4){Instruction n{};n.address=a;n.opcode=OPCODE_ADDIU;ins.push_back(n);}
  CodeGenerator gen(symbols,sections);const auto text=gen.generateFunction(f,ins,false);
  if(text.find(std::string("GPR_S64(ctx, 6) ")+c.comparison)==std::string::npos||text.find("GPR_S32(ctx, 6)")!=std::string::npos){std::cerr<<"FAIL signed branch opcode="<<c.opcode<<" rt="<<unsigned(c.rt)<<"\n";return 1;}
 }
 std::cout<<"PASS: all 12 signed branch variants emit low-64-bit comparisons\n";

 for(uint8_t variant=0;variant<4;++variant){
  Function f{};f.name="cop2_branch";f.start=0x1000;f.end=0x1010;f.isRecompiled=true;
  Instruction b{};b.address=0x1000;b.opcode=OPCODE_COP2;b.rs=COP2_BC;b.rt=variant;
  b.immediate=b.simmediate=2;b.raw=(OPCODE_COP2<<26)|(COP2_BC<<21)|(variant<<16)|2;b.isBranch=true;b.hasDelaySlot=true;
  std::vector<Instruction> ins{b};for(uint32_t a=0x1004;a<0x1010;a+=4){Instruction n{};n.address=a;n.opcode=OPCODE_ADDIU;ins.push_back(n);}
  CodeGenerator gen(symbols,sections);const auto text=gen.generateFunction(f,ins,false);
  const std::string expected=(variant&1)?"(ctx->vu0_vpu_stat & 0x100u)":"!(ctx->vu0_vpu_stat & 0x100u)";
  if(text.find("= ("+expected+");")==std::string::npos||text.find("ctx->vu0_status")!=std::string::npos){std::cerr<<"FAIL COP2 branch variant="<<unsigned(variant)<<" must use VPU_STAT bit 8, not arithmetic STATUS\n";return 1;}
 }
 std::cout<<"PASS: BC2F/T/FL/TL use VU1 running bit independently of arithmetic flags\n";

 R5900Decoder decoder;
 unsigned subCases=0;
 for(unsigned op:{0x2cu,4u,5u,6u,7u,0x24u,0x26u}) for(unsigned mask=0;mask<16;++mask) for(unsigned dst:{0u,7u}) {
  const uint32_t raw=0x4a000000u|(mask<<21)|(2u<<16)|(1u<<11)|(dst<<6)|op;
  const auto instruction=decoder.decodeInstruction(0x1000,raw);
  Function f{};f.name="vu_sub";f.start=0x1000;f.end=0x1004;f.isRecompiled=true;
  CodeGenerator gen(symbols,sections);const auto text=gen.generateFunction(f,{instruction},true);
  std::ostringstream expected;expected<<"ps2VuSub(ctx, 0x"<<std::hex<<std::setw(8)<<std::setfill('0')<<raw<<"u);";
  if(text.find(expected.str())==std::string::npos||text.find("runtime/ps2_vu_sub.h")==std::string::npos||text.find("ctx->vu0_vf[0] =")!=std::string::npos){std::cerr<<"FAIL VSUB helper emission "<<std::hex<<raw<<'\n';return 1;}
  ++subCases;
 }
 std::cout<<"PASS: "<<subCases<<" decoded VSUB variants include the shared result/flag helper\n";

 unsigned clipCases=0;
 for(unsigned fs:{0u,1u,12u,31u}) for(unsigned ft:{0u,1u,13u,31u}) {
  const uint32_t raw=0x4bc001ffu|(ft<<16)|(fs<<11);
  const auto instruction=decoder.decodeInstruction(0x1000,raw);
  Function f{};f.name="vu_clip";f.start=0x1000;f.end=0x1004;f.isRecompiled=true;
  CodeGenerator gen(symbols,sections);const auto text=gen.generateFunction(f,{instruction},true);
  std::ostringstream expected;expected<<"ps2VuClip(ctx, 0x"<<std::hex<<std::setw(8)<<std::setfill('0')<<raw<<"u);";
  if(text.find(expected.str())==std::string::npos||text.find("runtime/ps2_vu_clip.h")==std::string::npos){std::cerr<<"FAIL CLIP helper emission "<<std::hex<<raw<<'\n';return 1;}
  ++clipCases;
 }
 std::cout<<"PASS: "<<clipCases<<" decoded CLIP source combinations use shared helper\n";

}
