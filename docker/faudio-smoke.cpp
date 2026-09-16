// Load every packaged API module and exercise the real Win32/WASAPI backend.
#include <windows.h>
#include <xaudio2.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <cstdio>
#include <cstring>

struct LegacyDetails { WCHAR id[256], name[256]; UINT role; WAVEFORMATEXTENSIBLE format; };
struct LegacyAudio : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE GetDeviceCount(UINT32*)=0;
    virtual HRESULT STDMETHODCALLTYPE GetDeviceDetails(UINT32,LegacyDetails*)=0;
    virtual HRESULT STDMETHODCALLTYPE Initialize(UINT32,UINT32)=0;
};
struct Callback : IXAudio2VoiceCallback {
    HANDLE done = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HRESULT error = S_OK;
    ~Callback() { CloseHandle(done); }
    void STDMETHODCALLTYPE OnVoiceProcessingPassStart(UINT32) override {}
    void STDMETHODCALLTYPE OnVoiceProcessingPassEnd() override {}
    void STDMETHODCALLTYPE OnStreamEnd() override { SetEvent(done); }
    void STDMETHODCALLTYPE OnBufferStart(void*) override {}
    void STDMETHODCALLTYPE OnBufferEnd(void*) override {}
    void STDMETHODCALLTYPE OnLoopEnd(void*) override {}
    void STDMETHODCALLTYPE OnVoiceError(void*, HRESULT hr) override { error=hr; SetEvent(done); }
};
int main(int argc, char **argv) {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    IMMDeviceEnumerator *enumerator=nullptr;
    if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
            CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void **)&enumerator))) {
        IMMDevice *endpoint=nullptr;
        if (SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eRender,eConsole,&endpoint))) {
            WCHAR *id=nullptr;
            endpoint->GetId(&id);
            std::printf("ENDPOINT %ls\n",id);
            CoTaskMemFree(id);
            IAudioClient *client=nullptr;
            if (SUCCEEDED(endpoint->Activate(__uuidof(IAudioClient),CLSCTX_ALL,nullptr,(void **)&client))) {
                WAVEFORMATEX *format=nullptr;
                if (SUCCEEDED(client->GetMixFormat(&format))) {
                    auto ext=reinterpret_cast<WAVEFORMATEXTENSIBLE*>(format);
                    std::printf("ENDPOINT FORMAT channels=%u rate=%lu mask=%lx\n",
                        format->nChannels,format->nSamplesPerSec,
                        format->wFormatTag==WAVE_FORMAT_EXTENSIBLE ? ext->dwChannelMask : 0);
                    CoTaskMemFree(format);
                }
                client->Release();
            }
            endpoint->Release();
        }
        enumerator->Release();
    }
    const char* families[]={"xaudio2_", "x3daudio1_", "xapofx1_", "xactengine3_", "xactengine2_"};
    const int starts[]={0,0,1,0,0}, ends[]={9,7,5,7,9};
    unsigned count=0;
    for (int f=0;f<5;++f) for(int n=starts[f];n<=ends[f];++n) {
        if(f==4 && n!=0 && n!=4 && n!=7 && n!=9) continue;
        char name[128], path[4096];
        std::snprintf(name,sizeof(name),"%s%d.dll",families[f],n);
        HMODULE module=LoadLibraryA(name);
        if(!module) { std::printf("FAIL %s load error %lu\n",name,GetLastError()); return 1; }
        GetModuleFileNameA(module,path,sizeof(path));
        std::printf("LOADED %s %s\n",name,path);
        if(argc>1 && !std::strcmp(argv[1],"register")) {
            auto reg=reinterpret_cast<HRESULT (WINAPI*)()>(GetProcAddress(module,"DllRegisterServer"));
            if(reg && FAILED(reg())) { std::printf("FAIL %s registration\n",name); return 1; }
        }
        ++count;
    }
    const CLSID legacy_clsid={0x5a508685,0xa254,0x4fba,{0x9b,0x82,0x9a,0x24,0xb0,0x03,0x06,0xaf}};
    LegacyAudio *legacy=nullptr;
    if (SUCCEEDED(CoCreateInstance(legacy_clsid,nullptr,CLSCTX_INPROC_SERVER,
            __uuidof(IUnknown),(void**)&legacy))) {
        legacy->Initialize(0,0xffffffff);
        UINT32 count=0; legacy->GetDeviceCount(&count);
        for (UINT32 i=0;i<count;++i) {
            LegacyDetails d={};
            if (SUCCEEDED(legacy->GetDeviceDetails(i,&d)))
                std::printf("LEGACY DEVICE %u %ls channels=%u rate=%lu mask=%lx\n",
                    i,d.name,d.format.Format.nChannels,d.format.Format.nSamplesPerSec,d.format.dwChannelMask);
        }
        legacy->Release();
    } else return 7;
    auto create=reinterpret_cast<HRESULT (WINAPI*)(IXAudio2**,UINT32,XAUDIO2_PROCESSOR)>(
        GetProcAddress(GetModuleHandleA("xaudio2_9.dll"),"XAudio2Create"));
    IXAudio2 *engine=nullptr;
    if(!create || FAILED(create(&engine,0,XAUDIO2_DEFAULT_PROCESSOR))) return 2;
    IXAudio2MasteringVoice *master=nullptr;
    if(FAILED(engine->CreateMasteringVoice(&master,2,48000))) return 3;
    XAUDIO2_VOICE_DETAILS details={}; master->GetVoiceDetails(&details);
    DWORD mask=0; master->GetChannelMask(&mask);
    std::printf("MASTER channels=%u rate=%u mask=%lx\n",details.InputChannels,details.InputSampleRate,mask);
    Callback cb;
    WAVEFORMATEX fmt={WAVE_FORMAT_PCM,1,44100,88200,2,16,0};
    IXAudio2SourceVoice *voice=nullptr;
    if(FAILED(engine->CreateSourceVoice(&voice,&fmt,0,2.0f,&cb))) return 4;
    short silence[4410]={};
    XAUDIO2_BUFFER buffer={}; buffer.Flags=XAUDIO2_END_OF_STREAM;
    buffer.AudioBytes=sizeof(silence); buffer.pAudioData=reinterpret_cast<BYTE*>(silence);
    if(FAILED(voice->SubmitSourceBuffer(&buffer)) || FAILED(voice->Start())) return 5;
    if(WaitForSingleObject(cb.done,5000)!=WAIT_OBJECT_0 || FAILED(cb.error)) return 6;
    voice->DestroyVoice(); master->DestroyVoice(); engine->Release();
    std::printf("PASS %u modules; 44.1kHz PCM -> stereo 48kHz WASAPI; stream completed\n",count);
    CoUninitialize();
    return 0;
}
