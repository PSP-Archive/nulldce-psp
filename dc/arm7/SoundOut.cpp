//Taken from nulldc 360
#include "SoundOut.h"
#include <pspaudio.h>

#define MAX_UNPLAYED 16384
#define BUFFER_SIZE 65536

static int bufs=441*2;
static int buf48s=bufs*48000/44100;
static s16 buffer[BUFFER_SIZE];
static s16 buffer48[BUFFER_SIZE];
static int bufpos=0;

static int channelUsed = -1;
static bool soundInited = false;

static s16 prevLastSample[2]={0,0};
// resamples pStereoSamples (taken from http://pcsx2.googlecode.com/svn/trunk/plugins/zerospu2/zerospu2.cpp)
void ResampleLinear(s16* pStereoSamples, s32 oldsamples, s16* pNewSamples, s32 newsamples)
{
		s32 newsampL, newsampR;
		s32 i;
		
		for (i = 0; i < newsamples; ++i)
        {
                s32 io = i * oldsamples;
                s32 old = io / newsamples;
                s32 rem = io - old * newsamples;

                old *= 2;
				//printf("%d %d\n",old,oldsamples);
				if (old==0){
					newsampL = prevLastSample[0] * (newsamples - rem) + pStereoSamples[0] * rem;
					newsampR = prevLastSample[1] * (newsamples - rem) + pStereoSamples[1] * rem;
				}else{
					newsampL = pStereoSamples[old-2] * (newsamples - rem) + pStereoSamples[old] * rem;
					newsampR = pStereoSamples[old-1] * (newsamples - rem) + pStereoSamples[old+1] * rem;
				}
                pNewSamples[2 * i] = newsampL / newsamples;
                pNewSamples[2 * i + 1] = newsampR / newsamples;
        }

		prevLastSample[0]=pStereoSamples[oldsamples*2-2];
		prevLastSample[1]=pStereoSamples[oldsamples*2-1];
}

void PSP_InitAudio()
{
   channelUsed = sceAudioChReserve(PSP_AUDIO_NEXT_CHANNEL, PSP_AUDIO_SAMPLE_ALIGN(bufs), PSP_AUDIO_FORMAT_STEREO);

   soundInited = !!channelUsed; 
}

void PSP_TermAudio()
{
    sceAudioChRelease(channelUsed);
}
    
void PSP_WriteSample(s16 r,s16 l)
{
   // if (!soundInited) return;

    //printf("AUDIO OUT\n");

    buffer[bufpos++]=r;
    buffer[bufpos++]=l;
    
    if (bufpos>=bufs)
    {
        //ResampleLinear(buffer,bufs/2,buffer48,buf48s/2);

        sceAudioOutputPanned(channelUsed, PSP_AUDIO_VOLUME_MAX, PSP_AUDIO_VOLUME_MAX, buffer);   

        bufpos=0;
    }
}
