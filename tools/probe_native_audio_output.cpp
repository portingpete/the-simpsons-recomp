// Native output capability probe: synthetic or explicitly declared float PCM.
// Master/source stay muted. No decoder, guest memory or port integration.
#include <windows.h>
#include <xaudio2.h>
#include <wrl/client.h>
#include <array>
#include <atomic>
#include <cstdio>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string_view>
#include <vector>

void checked(HRESULT status,const char* operation) {
    if(FAILED(status)) {
        char message[192];std::snprintf(message,sizeof(message),"%s failed: %08lX",operation,static_cast<unsigned long>(status));
        throw std::runtime_error(message);
    }
}
struct Apartment {
    Apartment(){checked(CoInitializeEx(nullptr,COINIT_MULTITHREADED),"CoInitializeEx");}
    ~Apartment(){CoUninitialize();}
};
struct Callback final:IXAudio2VoiceCallback {
    HANDLE complete=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    std::atomic<unsigned> ended=0;
    std::atomic<HRESULT> error=S_OK;
    std::array<std::atomic<uintptr_t>,2> order{};
    Callback(){if(!complete) throw std::runtime_error("Completion event creation failed");}
    ~Callback(){CloseHandle(complete);}
    void STDMETHODCALLTYPE OnVoiceProcessingPassStart(UINT32) override {}
    void STDMETHODCALLTYPE OnVoiceProcessingPassEnd() override {}
    void STDMETHODCALLTYPE OnStreamEnd() override {}
    void STDMETHODCALLTYPE OnBufferStart(void*) override {}
    void STDMETHODCALLTYPE OnLoopEnd(void*) override {}
    void STDMETHODCALLTYPE OnBufferEnd(void* context) override {
        const unsigned index=ended.fetch_add(1);
        if(index<order.size()) order[index]=reinterpret_cast<uintptr_t>(context);
        else error=E_UNEXPECTED;
        if(index>=1 && !SetEvent(complete)) error=HRESULT_FROM_WIN32(GetLastError());
    }
    void STDMETHODCALLTYPE OnVoiceError(void*,HRESULT status) override {
        error=status;SetEvent(complete);
    }
};
struct Output {
    Microsoft::WRL::ComPtr<IXAudio2> engine;
    IXAudio2MasteringVoice* master{};
    IXAudio2SourceVoice* source{};
    Callback callback;
    std::array<std::vector<float>,2> pcm;
    std::array<unsigned,2> contexts{1,2};
    ~Output() {
        // DestroyVoice quiesces callbacks and data reads before these members die.
        if(source) source->DestroyVoice();
        if(master) master->DestroyVoice();
        engine.Reset();
    }
};
int wmain(int argc,wchar_t** argv) {
    try {
        const bool decoded=argc==3 && std::wstring_view(argv[1])==L"--mono-f32-48000";
        if(argc!=1 && !decoded) throw std::runtime_error("Usage: xaudio2_probe [--mono-f32-48000 PCM_FILE]");
        Apartment apartment;Output output;
        const unsigned channels=decoded?1:2;
        if(decoded) {
            // Probe format is explicit. No metadata is guessed from raw bytes.
            std::ifstream input(std::filesystem::path(argv[2]),std::ios::binary|std::ios::ate);
            if(!input) throw std::runtime_error("Cannot read declared mono float48000 fixture");
            const auto bytes=input.tellg();
            if(bytes<8 || bytes>16*1024*1024 || bytes%4) throw std::runtime_error("Invalid bounded PCM fixture length");
            std::vector<float> samples(static_cast<size_t>(bytes)/4);
            input.seekg(0);
            if(!input.read(reinterpret_cast<char*>(samples.data()),bytes)) throw std::runtime_error("Truncated PCM fixture");
            for(float sample:samples) if(!std::isfinite(sample)) throw std::runtime_error("Non-finite PCM fixture");
            const auto middle=samples.begin()+samples.size()/2;
            output.pcm[0].assign(samples.begin(),middle);output.pcm[1].assign(middle,samples.end());
        } else {
            for(auto& pcm:output.pcm) {
                pcm.resize(4800*2);
                for(size_t sample=0;sample<pcm.size();++sample) pcm[sample]=float(int(sample%64)-32)/128.0f;
            }
        }
        checked(XAudio2Create(output.engine.GetAddressOf(),0,XAUDIO2_DEFAULT_PROCESSOR),"XAudio2Create");
        checked(output.engine->CreateMasteringVoice(&output.master),"CreateMasteringVoice");
        checked(output.master->SetVolume(0),"Mute master");
        float masterVolume=1;output.master->GetVolume(&masterVolume);
        if(masterVolume!=0) throw std::runtime_error("Master mute did not read back");
        WAVEFORMATEX format{};format.wFormatTag=WAVE_FORMAT_IEEE_FLOAT;
        format.nChannels=WORD(channels);format.nSamplesPerSec=48000;format.wBitsPerSample=32;
        format.nBlockAlign=WORD(channels*4);format.nAvgBytesPerSec=48000*channels*4;
        checked(output.engine->CreateSourceVoice(&output.source,&format,0,1,&output.callback),"CreateSourceVoice");
        checked(output.source->SetVolume(0),"Mute source");
        for(unsigned index=0;index<2;++index) {
            const auto& pcm=output.pcm[index];
            XAUDIO2_BUFFER buffer{};buffer.Flags=index==1?XAUDIO2_END_OF_STREAM:0;
            buffer.AudioBytes=UINT32(pcm.size()*sizeof(float));
            buffer.pAudioData=reinterpret_cast<const BYTE*>(pcm.data());
            buffer.pContext=&output.contexts[index];
            checked(output.source->SubmitSourceBuffer(&buffer),"SubmitSourceBuffer");
        }
        XAUDIO2_VOICE_STATE before{};output.source->GetState(&before);
        if(before.BuffersQueued!=2) throw std::runtime_error("Stopped voice did not retain both submitted buffers");
        checked(output.source->Start(),"Start source");
        if(WaitForSingleObject(output.callback.complete,2000)!=WAIT_OBJECT_0)
            throw std::runtime_error("Muted native processing did not complete within two seconds");
        checked(output.callback.error,"Native voice callback");
        if(output.callback.ended!=2 || output.callback.order[0]!=reinterpret_cast<uintptr_t>(&output.contexts[0]) ||
           output.callback.order[1]!=reinterpret_cast<uintptr_t>(&output.contexts[1]))
            throw std::runtime_error("Native completion order/context mismatch");
        XAUDIO2_VOICE_STATE after{};output.source->GetState(&after);
        if(after.BuffersQueued || after.pCurrentBufferContext) throw std::runtime_error("Completed native buffers remain queued");
        XAUDIO2_VOICE_DETAILS master{};output.master->GetVoiceDetails(&master);
        checked(output.source->Stop(),"Stop source");
        std::printf("PASS native XAudio2: master %u channels/%u Hz, master_volume=0, input=%s, %u channels float48000, "
                    "frames=%zu, ordered completion=2, queued=0, samples_after_stream=%llu\n",
                    master.InputChannels,master.InputSampleRate,decoded?"declared-PCM-fixture":"synthetic-fixture",channels,
                    (output.pcm[0].size()+output.pcm[1].size())/channels,static_cast<unsigned long long>(after.SamplesPlayed));
        return 0;
    } catch(const std::exception& error) {
        std::fprintf(stderr,"FAIL native output probe: %s\n",error.what());return 1;
    }
}
