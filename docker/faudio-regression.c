/* Exercises upstream decoder/queue functions directly with ASan and UBSan.
 * Only OS locks and the unused WMA decoder are stubbed; submission, collection,
 * callback dispatch and decoding are the actual bundled source. */
#include <assert.h>
#include <stdio.h>
#include "FAudio_internal.c"

void FAudio_PlatformLockMutex(FAudioMutex m) {}
void FAudio_PlatformUnlockMutex(FAudioMutex m) {}
void decode_wma(FAudioVoice *v, struct queued_buffer *b, float *d, uint32_t n) { abort(); }
void FAudio_WMADEC_end_buffer(FAudioVoice *v) {}

struct allocation { size_t size; max_align_t alignment; };
static void *moving_malloc(size_t n) {
    struct allocation *p = malloc(sizeof(*p) + n);
    p->size = n;
    return p + 1;
}
static void moving_free(void *p) { if(p) free((struct allocation *)p - 1); }
static void *moving_realloc(void *p, size_t n) {
    void *q = moving_malloc(n);
    if(p) { memcpy(q, p, ((struct allocation *)p - 1)->size < n ? ((struct allocation *)p - 1)->size : n); moving_free(p); }
    return q;
}
static FAudioSourceVoice *callback_voice;
static void on_start(FAudioVoiceCallback *cb, void *context) {
    static float next[] = {0.5f, 0.25f};
    FAudioBuffer b = {.AudioBytes=sizeof(next), .pAudioData=(const uint8_t*)next};
    assert(FAudioSourceVoice_SubmitSourceBuffer(callback_voice, &b, NULL) == 0);
}
static void copy_decode(FAudioVoice *v, const void *s, float *d, uint32_t offset, uint32_t n) { memcpy(d, (const uint8_t*)s+offset*4, n*4); }
int main(int argc, char **argv) {
    assert(argc == 2);
    FAudio audio = {.pMalloc=moving_malloc,.pRealloc=moving_realloc,.pFree=moving_free,.version=7};
    FAudioWaveFormatEx format = {.wFormatTag=3,.nChannels=1,.nBlockAlign=4,.wBitsPerSample=32};
    FAudioSourceVoice voice = {.audio=&audio,.type=FAUDIO_VOICE_SOURCE};
    voice.src.format=&format; voice.src.samples_per_block=1; voice.src.decodeSamples=64; voice.src.decode=copy_decode;
    float decoded[64];
    for (int i=0; i<64; ++i) decoded[i]=100.0f;
    audio.decoded_audio=decoded;
    if(!strcmp(argv[1],"wma")) {
        voice.src.wmadec=(void*)1; voice.src.samples_per_block=0;
        assert(FAudio_INTERNAL_GetBytesRequested(&voice,480)==0);
        voice.src.wmadec=NULL; voice.src.samples_per_block=1;
        assert(FAudio_INTERNAL_GetBytesRequested(&voice,480)==1920);
    } else {
        float samples[5]={0.75f, 0.5f, 0.25f, 0.125f, 0.0625f};
        voice.src.queued_buffers=moving_malloc(sizeof(*voice.src.queued_buffers));
        voice.src.queued_buffers_capacity=1;
        FAudioBuffer b={.AudioBytes=sizeof(samples),.pAudioData=(const uint8_t*)samples};
        if(!strcmp(argv[1],"unaligned") || !strcmp(argv[1],"offset")) {
            voice.src.unaligned_data=moving_malloc(4);
            memcpy(voice.src.unaligned_data, samples, 1);
            voice.src.unaligned_size=1;
            b.pAudioData++; b.AudioBytes--;
        }
        if (!strcmp(argv[1], "padding")) b.AudioBytes=8;
        if (!strcmp(argv[1], "loopcallback")) { b.LoopCount=1; b.LoopLength=1; }
        assert(FAudioSourceVoice_SubmitSourceBuffer(&voice,&b,NULL)==0);
        FAudioVoiceCallback cb={.OnBufferStart=on_start};
        if(!strcmp(argv[1],"callback")) { voice.src.callback=&cb; callback_voice=&voice; }
        if (!strcmp(argv[1], "loopcallback")) {
            cb.OnBufferStart=NULL; cb.OnLoopEnd=on_start;
            voice.src.callback=&cb; callback_voice=&voice;
            voice.src.curBufferOffset=1;
            end_buffer(&voice);
            assert(voice.src.queued_buffer_count==2);
        }
        uint64_t count=!strcmp(argv[1],"offset") ? 2 : 1;
        FAudio_INTERNAL_DecodeBuffers(&voice,&count);
        assert(decoded[0]==0.75f);
        if (!strcmp(argv[1], "offset")) assert(count==2 && decoded[1]==0.5f && decoded[2]==0.25f && decoded[3]==0.125f);
        else assert(count==1);
        if (!strcmp(argv[1], "padding")) {
            assert(decoded[1]==0.5f && decoded[2]==0.0f);
        }
        for(size_t i=0;i<voice.src.queued_buffer_count;i++) if(voice.src.queued_buffers[i].internal) moving_free((void*)voice.src.queued_buffers[i].buffer.pAudioData);
        moving_free(voice.src.queued_buffers); moving_free(voice.src.unaligned_data);
    }
    printf("PASS %s\n",argv[1]);
}
