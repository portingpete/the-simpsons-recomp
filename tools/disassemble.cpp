// Offline analysis executable only. Never linked into SimpsonsNative.
#include <disasm.h>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <vector>
int main(int argc,char** argv) {
    if(argc!=5) { fprintf(stderr,"SimpsonsDisasm image base address count\n"); return 2; }
    uint64_t base=strtoull(argv[2],nullptr,0),address=strtoull(argv[3],nullptr,0),count=strtoull(argv[4],nullptr,0);
    std::ifstream f(argv[1],std::ios::binary|std::ios::ate);
    if(!f || address<base || (address&3) || count>0x100000 || address-base+count*4>uint64_t(f.tellg())) return 2;
    std::vector<uint8_t> bytes(count*4);
    f.seekg(address-base); f.read(reinterpret_cast<char*>(bytes.data()),bytes.size());
    if(!f) return 2;
    for(size_t n=0;n<count;++n) {
        ppc_insn insn{};
        ppc::Disassemble(bytes.data()+n*4,address+n*4,insn);
        const auto* b=bytes.data()+n*4;
        printf("%08llX  %02x%02x%02x%02x  %s %s\n",address+n*4,b[0],b[1],b[2],b[3],
               insn.opcode?insn.opcode->name:"INVALID",insn.opcode?insn.op_str:"");
    }
}
