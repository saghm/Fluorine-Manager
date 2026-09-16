/* Mixed output must equal the sum of independently filtered voices, in
 * either voice order. Run the actual source and submix mixers and SIMD code. */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include "FAudio_internal.c"

void FAudio_PlatformLockMutex(FAudioMutex mutex) {}
void FAudio_PlatformUnlockMutex(FAudioMutex mutex) {}
extern void FAudio_INTERNAL_Mix_Generic_Scalar(uint32_t, uint32_t, uint32_t,
                                               float *, float *, float *);

enum { FRAMES = 519, BLOCKS = 2, SAMPLES = FRAMES * BLOCKS * MAX_CHANNELS };

static void copy_decode(FAudioVoice *voice, const void *input, float *output,
                        uint32_t offset, uint32_t frames)
{
    unsigned channels = voice->src.format->nChannels;
    memcpy(output, (const float *)input + offset * channels,
           frames * channels * sizeof(float));
}

static void render(int source, int input_channels, int output_channels,
                   int enabled, int reverse, float *result)
{
    FAudio audio = {.pMalloc = malloc, .pFree = free, .pRealloc = realloc,
                    .version = 7, .updateSize = FRAMES};
    float destination[2][FRAMES * MAX_CHANNELS] = {{0}};
    float samples[2][SAMPLES] = {{0}};
    float decoded[(FRAMES + EXTRA_DECODE_PADDING) * MAX_CHANNELS];
    float resampled[FRAMES * MAX_CHANNELS];
    FAudioVoice targets[2] = {{.type = FAUDIO_VOICE_MASTER},
                             {.type = FAUDIO_VOICE_SUBMIX}};
    targets[0].master.output = destination[0];
    targets[0].master.inputChannels = output_channels;
    targets[0].master.inputSampleRate = 48000;
    targets[1].mix.input = destination[1];
    targets[1].mix.inputChannels = output_channels;
    targets[1].mix.inputSampleRate = 48000;
    audio.master = &targets[0];
    audio.decoded_audio = decoded;
    audio.decodeSamples = sizeof(decoded) / sizeof(float);
    audio.resampled_audio = resampled;
    audio.resampleSamples = sizeof(resampled) / sizeof(float);
    FAudioWaveFormatEx format = {.wFormatTag = 3, .nChannels = input_channels,
        .nSamplesPerSec = 48000, .nBlockAlign = input_channels * 4,
        .wBitsPerSample = 32};
    FAudioVoice voices[2] = {0};
    FAudioSendDescriptor sends[2] = {
        {.Flags = FAUDIO_SEND_USEFILTER, .pOutputVoice = &targets[0]},
        {.Flags = 0, .pOutputVoice = &targets[1]}};
    FAudioMixCallback mixers[2] = {FAudio_INTERNAL_Mix_Generic,
                                   FAudio_INTERNAL_Mix_Generic};
    FAudioFilterParametersEXT filters[2][2] = {0};
    FAudioFilterState state[2][MAX_CHANNELS] = {0};
    FAudioFilterState *states[2][2] = {{state[0], NULL}, {state[1], NULL}};
    float coefficients[2][MAX_CHANNELS * MAX_CHANNELS] = {{0}};
    float *matrices[2] = {coefficients[0], coefficients[1]};
    for (int i = 0; i < input_channels * output_channels; ++i)
        coefficients[0][i] = coefficients[1][i] = 0.25f + 0.01f * i;
    for (int v = 0; v < 2; ++v)
    {
        FAudioVoice *voice = &voices[v];
        voice->audio = &audio;
        voice->type = source ? FAUDIO_VOICE_SOURCE : FAUDIO_VOICE_SUBMIX;
        voice->outputChannels = input_channels;
        voice->volume = 1;
        voice->sends = (FAudioVoiceSends){2, sends};
        voice->sendMix = mixers;
        voice->mixCoefficients = matrices;
        filters[v][0] = (FAudioFilterParametersEXT){
            .Type = v ? FAudioHighPassFilter : FAudioLowPassFilter,
            .Frequency = v ? 0.4f : 0.2f, .OneOverQ = 1,
            .WetDryMix = v ? 0.75f : 1.0f};
        voice->sendFilter = filters[v];
        voice->sendFilterState = states[v];
        for (int i = 0; i < FRAMES * BLOCKS * input_channels; ++i)
            samples[v][i] = 0.1f * sinf(i * (v ? 0.43f : 0.17f));
        if (source)
        {
            voice->src.active = 1;
            voice->src.format = &format;
            voice->src.samples_per_block = 1;
            voice->src.decode = copy_decode;
            voice->src.decodeSamples = FRAMES + EXTRA_DECODE_PADDING;
            voice->src.resampleSamples = FRAMES;
            voice->src.resampleStep = FIXED_ONE;
            voice->src.resampleFreq = 48000;
            voice->src.freqRatio = 1;
            FAudioBuffer buffer = {.pAudioData = (const uint8_t *)samples[v],
                .AudioBytes = FRAMES * BLOCKS * input_channels * sizeof(float)};
            assert(!FAudioSourceVoice_SubmitSourceBuffer(voice, &buffer, NULL));
        }
        else
        {
            voice->mix.inputChannels = input_channels;
            voice->mix.inputSamples = FRAMES * input_channels;
            voice->mix.outputSamples = FRAMES;
            voice->mix.resampleStep = FIXED_ONE;
        }
    }
    for (int block = 0; block < BLOCKS; ++block)
    {
        memset(destination, 0, sizeof(destination));
        for (int i = 0; i < 2; ++i)
        {
            int v = reverse ? 1 - i : i;
            if (!(enabled & (1 << v))) continue;
            if (source) FAudio_INTERNAL_MixSource(&voices[v]);
            else
            {
                voices[v].mix.input = samples[v] + block * FRAMES * input_channels;
                FAudio_INTERNAL_MixSubmix(&voices[v]);
            }
        }
        for (int target = 0; target < 2; ++target)
            memcpy(result + (target * BLOCKS + block) * FRAMES * output_channels,
                   destination[target], FRAMES * output_channels * sizeof(float));
    }
    if (source)
        for (int v = 0; v < 2; ++v) free(voices[v].src.queued_buffers);
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    int source = !strcmp(argv[1], "source");
    const int outputs[] = {1, 2, 6, 8};
    float first[2 * SAMPLES], second[2 * SAMPLES], mixed[2 * SAMPLES];
    for (int simd = 0; simd < 2; ++simd)
    {
        FAudio_INTERNAL_InitSIMDFunctions(1, 0);
        if (!simd) FAudio_INTERNAL_Mix_Generic = FAudio_INTERNAL_Mix_Generic_Scalar;
        for (int in = 1; in <= 2; ++in)
        for (unsigned out = 0; out < sizeof(outputs) / sizeof(*outputs); ++out)
        {
            render(source, in, outputs[out], 1, 0, first);
            render(source, in, outputs[out], 2, 0, second);
            for (int reverse = 0; reverse < 2; ++reverse)
            {
                render(source, in, outputs[out], 3, reverse, mixed);
                for (int i = 0; i < 2 * FRAMES * BLOCKS * outputs[out]; ++i)
                    assert(fabsf(mixed[i] - first[i] - second[i]) < 0.00001f);
            }
        }
    }
    printf("PASS %s send isolation: both orders, two blocks, mono/stereo -> 1/2/6/8 channels, scalar/SSE2\n", argv[1]);
}
