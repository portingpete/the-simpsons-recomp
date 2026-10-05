#pragma once
#include "native_backend.h"
#include <d3d11_4.h>
#include <atomic>
#include <cstdio>

namespace Simpsons::Graphics {
// D3D11's one-shot removal notification replaces repeated kernel queries in
// the per-primitive owner guards. HRESULTs from resource/Map/GetData/Present
// operations remain independently checked; GPU retirement also queries status.
// https://learn.microsoft.com/en-us/windows/win32/api/d3d11_4/nf-d3d11_4-id3d11device4-registerdeviceremovedevent
class DeviceAvailability {
    friend struct NativePresentationProbe;
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11Device4> notifier;
    HANDLE event{};
    PTP_WAIT wait{};
    DWORD cookie{};
    bool registered=false;
    std::atomic<bool> removed=false;
    static void CALLBACK notified(PTP_CALLBACK_INSTANCE,void* state,PTP_WAIT,TP_WAIT_RESULT) noexcept {
        static_cast<DeviceAvailability*>(state)->removed.store(true,std::memory_order_release);
    }
    void close() noexcept {
        if(registered){notifier->UnregisterDeviceRemoved(cookie);registered=false;}
        if(wait) {
            SetThreadpoolWait(wait,nullptr,nullptr);
            WaitForThreadpoolWaitCallbacks(wait,TRUE);
            CloseThreadpoolWait(wait);wait=nullptr;
        }
        if(event){CloseHandle(event);event=nullptr;}
    }
    void query() const {
        const HRESULT status=device->GetDeviceRemovedReason();
        if(FAILED(status)) {
            char message[160];std::snprintf(message,sizeof(message),
                "Native graphics device availability failed: 0x%08lX",ULONG(status));
            throw Error(message);
        }
    }
public:
    explicit DeviceAvailability(ID3D11Device* source):device(source) {
        if(!device)throw Error("Native device availability requires an owned device");
        query();
        const HRESULT result=device.As(&notifier);
        if(result==E_NOINTERFACE)return; // Older D3D11 retains synchronous polling.
        if(FAILED(result))throw Error("Native device removal interface query failed");
        try {
            event=CreateEventW(nullptr,TRUE,FALSE,nullptr);
            if(!event)throw Error("Native device removal event creation failed");
            wait=CreateThreadpoolWait(notified,this,nullptr);
            if(!wait)throw Error("Native device removal wait creation failed");
            if(FAILED(notifier->RegisterDeviceRemovedEvent(event,&cookie)))
                throw Error("Native device removal notification registration failed");
            registered=true;
            SetThreadpoolWait(wait,event,nullptr);
            query(); // Registration on an already removed device can succeed.
        } catch(...) {close();throw;}
    }
    ~DeviceAvailability(){close();}
    DeviceAvailability(const DeviceAvailability&)=delete;
    DeviceAvailability& operator=(const DeviceAvailability&)=delete;
    void require() const {
        if(!registered){query();return;}
        if(removed.load(std::memory_order_acquire)) {
            query();
            throw Error("Native graphics device removal notification received");
        }
    }
};
}
