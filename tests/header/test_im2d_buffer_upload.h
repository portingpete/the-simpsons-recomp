#pragma once
// Included after require/rejects. Raw readback deliberately bypasses public
// flushes, so retirement tests cannot accidentally submit the missing writes.
namespace Simpsons::Graphics {
struct NativeBufferUploadProbe {
    static std::vector<uint8_t> readSubmitted(NativeBackend& b,const std::shared_ptr<Buffer>& buffer) {
        D3D11_BUFFER_DESC desc{};buffer->buffer->GetDesc(&desc);
        desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Buffer> staging;
        require(SUCCEEDED(b.device->CreateBuffer(&desc,nullptr,&staging)),"Upload probe staging allocation failed");
        b.context->CopyResource(staging.Get(),buffer->buffer.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{};
        require(SUCCEEDED(b.context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped)),"Upload probe staging map failed");
        std::vector<uint8_t> result(desc.ByteWidth);
        std::memcpy(result.data(),mapped.pData,result.size());b.context->Unmap(staging.Get(),0);return result;
    }
};
}
void im2dBufferUploadContracts(NativeBackend& b,NativeBackend& foreign) {
    constexpr UINT size=0x40000;
    auto buffer=b.createBuffer(size,BufferKind::Vertex);
    std::vector<uint8_t> expected(size,0xAD);b.writeBuffer(buffer,0,expected);
    const auto before=b.bufferUploadCount();
    // Thousands of original-sized adjacent writes, source poisoned immediately.
    for(UINT n=0;n<2000;++n) {
        std::array<uint8_t,112> bytes;
        for(UINT i=0;i<bytes.size();++i)bytes[i]=uint8_t(n*13+i*7);
        std::copy(bytes.begin(),bytes.end(),expected.begin()+n*bytes.size());
        b.queueIm2DBufferWrite(buffer,n*UINT(bytes.size()),bytes);bytes.fill(0);
    }
    require(b.bufferUploadCount()==before,"Contiguous upload queue submitted per packet");
    b.waitIdle();
    require(b.bufferUploadCount()==before+1,"Contiguous raw bytes did not become one native copy");
    require(NativeBufferUploadProbe::readSubmitted(b,buffer)==expected,"Idle omitted queued bytes or source snapshot");
    // Overlap, discontinuities and four owners across several upload-ring wraps.
    std::array<std::shared_ptr<Buffer>,4> buffers;
    std::array<std::vector<uint8_t>,4> values;
    for(UINT i=0;i<4;++i) {
        buffers[i]=b.createBuffer(size,BufferKind::Vertex);
        values[i].assign(size,uint8_t(i*31));b.writeBuffer(buffers[i],0,values[i]);
    }
    for(UINT n=0;n<3000;++n) {
        const UINT slot=n/23%4,length=113+n%1900,offset=(n*769)%(size-length+1);
        std::vector<uint8_t> bytes(length);
        for(UINT i=0;i<length;++i)bytes[i]=uint8_t(n*11+i*29);
        std::copy(bytes.begin(),bytes.end(),values[slot].begin()+offset);
        b.queueIm2DBufferWrite(buffers[slot],offset,bytes);std::fill(bytes.begin(),bytes.end(),0);
    }
    for(UINT i=0;i<4;++i)
        require(b.readbackBuffer(buffers[i])==values[i],"Queued raw uploads lost overlap order or bytes across ring wraps");
    // A normal write must follow (and override) an earlier queued overlapping write.
    std::array<uint8_t,128> first{},second{};first.fill(0x71);second.fill(0x32);
    b.queueIm2DBufferWrite(buffer,0,first);b.writeBuffer(buffer,64,second);
    std::copy(first.begin(),first.end(),expected.begin());
    std::copy(second.begin(),second.end(),expected.begin()+64);
    require(NativeBufferUploadProbe::readSubmitted(b,buffer)==expected,"General write overtook a queued upload");
    // Validation remains fresh, and rejection does not discard accepted data.
    b.queueIm2DBufferWrite(buffer,256,first);
    std::copy(first.begin(),first.end(),expected.begin()+256);
    const auto accepted=b.bufferUploadCount();
    rejects([&]{foreign.queueIm2DBufferWrite(buffer,0,first);});
    rejects([&]{b.queueIm2DBufferWrite(buffer,size-64,first);});
    rejects([&]{b.queueIm2DBufferWrite(buffer,0xFFFFFFFF,first);});
    rejects([&]{b.queueIm2DBufferWrite({},0,first);});
    auto index=b.createBuffer(1024,BufferKind::Index16),large=b.createBuffer(size+1,BufferKind::Vertex);
    rejects([&]{b.queueIm2DBufferWrite(index,0,first);});
    rejects([&]{b.queueIm2DBufferWrite(large,0,first);});
    std::atomic<bool> rejected=false;
    std::thread other([&]{try{b.queueIm2DBufferWrite(buffer,0,first);}catch(const Error&){rejected=true;}});
    other.join();require(rejected,"Queued upload accepted another context owner thread");
    require(b.bufferUploadCount()==accepted,"Rejected upload changed accepted queue submission");
    require(b.readbackBuffer(buffer)==expected,"Rejected upload discarded previously owned data");
    // Replacement of the caller wrapper cannot retarget a previously owned COM resource.
    auto original=std::make_shared<Buffer>(*buffer);
    auto replacement=b.createBuffer(size,BufferKind::Vertex);
    std::vector<uint8_t> replacementBytes(size,0xBE);b.writeBuffer(replacement,0,replacementBytes);
    b.queueIm2DBufferWrite(buffer,512,first);
    std::copy(first.begin(),first.end(),expected.begin()+512);
    *buffer=*replacement;b.flushIm2D();
    require(NativeBufferUploadProbe::readSubmitted(b,original)==expected,"Caller replacement retargeted an owned upload");
    require(NativeBufferUploadProbe::readSubmitted(b,replacement)==replacementBytes,"Queued upload overwrote replacement storage");
    // A graphics clear and an empty-geometry flush must publish raw bytes too.
    b.queueIm2DBufferWrite(original,1024,second);
    std::copy(second.begin(),second.end(),expected.begin()+1024);
    auto target=b.createTarget(2,2,TargetFormat::RGB10A2);b.clearTarget(target,{0,0,0,1});
    require(NativeBufferUploadProbe::readSubmitted(b,original)==expected,"Clear did not drain raw upload queue");
    b.queueIm2DBufferWrite(original,size,{});
    require(b.readbackBuffer(original)==expected,"Empty upload changed storage");
}
