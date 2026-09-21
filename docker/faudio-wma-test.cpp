// Exercise the packaged DLL's WMA decoder on game-style worker threads.
// Run under both Wine/Proton 10 and 11, for each architecture. No game assets
// are needed: decoder activation and format negotiation happen at voice creation.
#include <windows.h>
#include <xaudio2.h>
#include <cstdio>
#include <initializer_list>

static HRESULT (WINAPI *getApartmentType)(APTTYPE *, APTTYPEQUALIFIER *);

struct Worker {
    IXAudio2 *audio;
    IXAudio2SourceVoice *voice = nullptr;
    HRESULT result = E_FAIL;
    HRESULT apartment = E_FAIL;
};

static DWORD WINAPI createWmaVoice(void *context)
{
    auto &worker = *static_cast<Worker *>(context);
    WAVEFORMATEXTENSIBLE format = {};
    format.Format = {WAVE_FORMAT_EXTENSIBLE, 2, 44100, 20000, 2230, 16, 22};
    format.Samples.wValidBitsPerSample = 16;
    format.dwChannelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT;
    format.SubFormat = {0x161, 0, 0x10, {0x80, 0, 0, 0xaa, 0, 0x38, 0x9b, 0x71}};
    // Deliberately do not call CoInitialize[Ex] on this thread.
    worker.result = worker.audio->CreateSourceVoice(
        &worker.voice, &format.Format, 0, 1.0f);
    return 0;
}

static DWORD WINAPI queryApartment(void *context)
{
    APTTYPE type;
    APTTYPEQUALIFIER qualifier;
    static_cast<Worker *>(context)->apartment = getApartmentType(&type, &qualifier);
    return 0;
}

static bool runWorker(LPTHREAD_START_ROUTINE function, Worker &worker)
{
    HANDLE thread = CreateThread(nullptr, 0, function, &worker, 0, nullptr);
    if (!thread) return false;
    const bool completed = WaitForSingleObject(thread, 10000) == WAIT_OBJECT_0;
    CloseHandle(thread);
    return completed;
}

int main(int argc, char **argv)
{
    if (argc > 1 && !std::freopen(argv[1], "w", stdout)) return 1;
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    getApartmentType = reinterpret_cast<decltype(getApartmentType)>(
        GetProcAddress(LoadLibraryW(L"combase.dll"), "CoGetApartmentType"));
    if (!getApartmentType) return 2;
    // An MTA main thread would mask the regression by giving every worker an
    // implicit apartment before FAudio creates a decoder.
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) return 2;
    HMODULE module = LoadLibraryW(L"xaudio2_9.dll");
    if (!module) return 3;
    char path[4096];
    GetModuleFileNameA(module, path, sizeof(path));
    std::printf("LOADED %s\n", path);
    auto create = reinterpret_cast<HRESULT (WINAPI *)(IXAudio2 **, UINT32, XAUDIO2_PROCESSOR)>(
        GetProcAddress(module, "XAudio2Create"));
    IXAudio2 *audio = nullptr;
    if (!create || FAILED(create(&audio, 0, XAUDIO2_DEFAULT_PROCESSOR))) return 4;
    IXAudio2MasteringVoice *master = nullptr;
    if (FAILED(audio->CreateMasteringVoice(&master, 2, 48000))) return 5;

    // Show that ordinary PCM voice creation works before testing WMA.
    WAVEFORMATEX pcm = {WAVE_FORMAT_PCM, 2, 44100, 176400, 4, 16, 0};
    IXAudio2SourceVoice *pcmVoice = nullptr;
    if (FAILED(audio->CreateSourceVoice(&pcmVoice, &pcm))) return 6;
    pcmVoice->DestroyVoice();
    std::puts("PASS PCM voice");

    Worker first{audio}, second{audio}, third{audio};
    for (Worker *worker : {&first, &second}) {
        if (!runWorker(createWmaVoice, *worker)) return 7;
        std::printf("WMA worker voice=%08lx\n", static_cast<unsigned long>(worker->result));
        if (FAILED(worker->result)) return 8;
    }
    // Destroy on a different thread from creation, retaining the second voice.
    first.voice->DestroyVoice();
    if (!runWorker(createWmaVoice, third) || FAILED(third.result)) return 10;
    second.voice->DestroyVoice();
    third.voice->DestroyVoice();
    // No MTA reference should remain once the last decoder has been released.
    if (!runWorker(queryApartment, third) || third.apartment != CO_E_NOTINITIALIZED) return 11;
    APTTYPE type;
    APTTYPEQUALIFIER qualifier;
    if (FAILED(getApartmentType(&type, &qualifier)) ||
        (type != APTTYPE_STA && type != APTTYPE_MAINSTA)) return 12;

    master->DestroyVoice();
    audio->Release();
    FreeLibrary(module);
    CoUninitialize();
    std::puts("PASS WMA worker threads, overlapping voices, cross-thread destruction, COM lifetime");
    return 0;
}
