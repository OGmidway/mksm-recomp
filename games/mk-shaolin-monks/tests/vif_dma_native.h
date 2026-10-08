// REF tags carry VIF command words when CHCR.TTE is set, just like CNT/END.
static bool checkVifDmaTagTransfer() {
 for(uint32_t channel:{0x10008000u,0x10009000u}) for(unsigned id:{0u,3u,4u}) {
  auto mem=std::make_unique<PS2Memory>();if(!mem->initialize())return false;
  const uint64_t program[]{0x80001efc5000300dull,0x000002ff8000033cull};
  std::memcpy(mem->getRDRAM()+0x22000,program,sizeof(program));
  const uint64_t tags[]{(uint64_t(0x22000)<<32)|(uint64_t(id)<<28)|1,
                         uint64_t(0x4a020000)<<32,0x70000000,0};
  std::memcpy(mem->getRDRAM()+0x21000,tags,sizeof(tags));
  mem->writeIORegister(0x1000e000,1);
  mem->writeIORegister(channel+0x30,0x21000);
  mem->writeIORegister(channel,0x145);
  mem->processPendingTransfers();
  const uint8_t* code=channel==0x10008000?mem->getVU0Code():mem->getVU1Code();
  if(std::memcmp(code,program,sizeof(program)))return false;
 }
 // With TTE clear, upper tag words must not be inserted into the VIF stream.
 auto mem=std::make_unique<PS2Memory>();if(!mem->initialize())return false;
 const uint32_t chain[]{0x70000002,0,0x4a020001,0,0,0x4a020000,
                        0x5000300d,0x80001efc,0x8000033c,0x000002ff,0,0};
 std::memcpy(mem->getRDRAM()+0x21000,chain,sizeof(chain));
 mem->writeIORegister(0x1000e000,1);
 mem->writeIORegister(0x10009030,0x21000);mem->writeIORegister(0x10009000,0x105);
 mem->processPendingTransfers();
 if(std::memcmp(mem->getVU1Code(),chain+6,16))return false;
 auto runtime=std::make_unique<PS2Runtime>();auto& host=runtime->memory();
 if(!host.initialize())return false;
 const uint64_t ref[]{(uint64_t(0x22000)<<32)|1,uint64_t(0x4a020000)<<32};
 std::memcpy(host.getRDRAM()+0x21000,ref,sizeof(ref));
 std::memcpy(host.getRDRAM()+0x22000,chain+6,16);
 host.writeIORegister(0x1000e000,1);host.writeIORegister(0x10009000,0xc0);
 R5900Context send{};SET_GPR_U32(&send,4,0x10009000);SET_GPR_U32(&send,5,0x21000);
 ps2_stubs::sceDmaSend(host.getRDRAM(),&send,runtime.get());
 return std::memcmp(host.getVU1Code(),chain+6,16)==0 &&
        (host.readIORegister(0x10009000)&0xc0)==0xc0;
}
