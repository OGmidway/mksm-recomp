#include "ps2x/iop/spu_adpcm.h"
#include <filesystem>
#include <fstream>

namespace ps2_vag { bool decode(const uint8_t*,uint32_t,std::vector<int16_t>&,uint32_t&); }

static bool checkSpuAdpcm() {
 const auto path=std::filesystem::path(__FILE__).parent_path().parent_path()/"logs/sndf-adpcm-fixture.bin";
 std::ifstream input(path,std::ios::binary);
 const std::vector<uint8_t> fixture((std::istreambuf_iterator<char>(input)),{});
 if(fixture.size()<12 || fixture.size()>65536 || std::memcmp(fixture.data(),"SADP",4))return false;
 auto word=[&](size_t offset){return uint32_t(fixture[offset])|(uint32_t(fixture[offset+1])<<8)|
    (uint32_t(fixture[offset+2])<<16)|(uint32_t(fixture[offset+3])<<24);};
 const uint32_t bytes=word(4),count=word(8);
 if(bytes!=812*16 || count!=812*28 || fixture.size()!=12+bytes+count*2)return false;
 ps2x::iop::SpuAdpcmDecoder decoder;
 std::array<int16_t,28> pcm{};
 for(unsigned b=0;b<812;++b) {
  if(!decoder.decode(std::span(fixture.data()+12+b*16,16),pcm))return false;
  for(unsigned i=0;i<28;++i) {
   const auto p=12+bytes+(b*28+i)*2;
   if(uint16_t(pcm[i])!=(unsigned(fixture[p])|(unsigned(fixture[p+1])<<8)))return false;
  }
 }
 // Negative feedback rounds down, not toward zero (the old decoder's bug).
 std::array<uint8_t,16> block{};block[0]=0x1c;
 decoder.previous=-1;decoder.older=0;
 if(!decoder.decode(block,pcm) || pcm[0]!=-1)return false;
 // SPU shift 13 is a real arithmetic shift, not the old remapping to 9.
 decoder={};block[0]=13;block[2]=0x87;
 if(!decoder.decode(block,pcm) || pcm[0]!=3 || pcm[1]!=-4)return false;
 // Saturation feeds back into the next decoded sample.
 decoder.previous=32767;decoder.older=-32768;block.fill(0);block[0]=0x40;
 if(!decoder.decode(block,pcm) || pcm[0]!=32767)return false;
 const auto saved=decoder;const auto savedPcm=pcm;block[0]=0x50;
 if(decoder.decode(block,pcm) || decoder.previous!=saved.previous || decoder.older!=saved.older || pcm!=savedPcm)return false;
 if(decoder.decode(std::span(block.data(),15),pcm))return false;
 // Existing VAG playback uses exactly the same block decoder.
 std::vector<uint8_t> vag(48+bytes);std::memcpy(vag.data(),"VAGp",4);
 for(unsigned i=0;i<4;++i){vag[12+i]=uint8_t(bytes>>(24-i*8));vag[16+i]=uint8_t(48000u>>(24-i*8));}
 std::memcpy(vag.data()+48,fixture.data()+12,bytes);
 std::vector<int16_t> decoded;uint32_t rate=0;
 if(!ps2_vag::decode(vag.data(),uint32_t(vag.size()),decoded,rate) || rate!=48000 || decoded.size()!=count)return false;
 for(unsigned i=0;i<count;++i) {
  const auto p=12+bytes+i*2;
  if(uint16_t(decoded[i])!=(unsigned(fixture[p])|(unsigned(fixture[p+1])<<8)))return false;
 }
 const auto savedDecoded=decoded;
 if(ps2_vag::decode(vag.data(),uint32_t(vag.size()-1),decoded,rate) || decoded!=savedDecoded)return false;
 vag[12]=255;
 if(ps2_vag::decode(vag.data(),uint32_t(vag.size()),decoded,rate) || decoded!=savedDecoded)return false;
 return true;
}
