/* Exercise the actual DSP delay, including supported zero/maximum values. */
#include <assert.h>
#include <stdio.h>
#include "FAudioFX_reverb.c"

int main(int argc, char **argv)
{
    const int rates[] = {22050, 44100, 48000};
    const float delays[] = {0.0f, 1.0f, 85.0f, 300.0f};
    assert(argc == 2);
    for (unsigned r = 0; r < sizeof(rates) / sizeof(*rates); ++r)
    {
        for (unsigned d = 0; d < sizeof(delays) / sizeof(*delays); ++d)
        {
            if (!strcmp(argv[1], "zero") && d != 0) continue;
            if (!strcmp(argv[1], "maximum") && d != 3) continue;
            DspDelay line;
            DspDelay_Initialize(&line, rates[r], delays[d], malloc);
            for (unsigned pass = 0; pass < 3; ++pass)
            {
                if (pass == 1) DspDelay_Reset(&line);
                if (pass == 2)
                {
                    DspDelay_Change(&line, 0);
                    assert(DspDelay_Process(&line, 0.25f) == 0.25f);
                    DspDelay_Change(&line, delays[d]);
                    DspDelay_Reset(&line);
                }
                unsigned expected = MsToSamples(delays[d], rates[r]);
                for (unsigned i = 0; i < (unsigned)rates[r]; ++i)
                {
                    float output = DspDelay_Process(&line, i == 0 ? 1.0f : 0.0f);
                    assert(output == (i == expected ? 1.0f : 0.0f));
                }
            }
            DspDelay_Destroy(&line, free);
        }
    }
    printf("PASS reverb delay %s at 22050/44100/48000 Hz\n", argv[1]);
}
