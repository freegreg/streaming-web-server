// loopback-capture.h

// call CreateThread on this function
// feed it the address of a LoopbackCaptureThreadFunctionArguments
// it will capture via loopback from the IMMDevice
// and dump output to the HMMIO
// until the stop event is set
// any failures will be propagated back via hr
#pragma once
#include "../threadSafeBuffer.h"

struct LoopbackCaptureThreadFunctionArguments {
    IMMDevice *pMMDevice;
    bool bInt16;
    HMMIO hFile;
    UINT32 nFrames;
    HRESULT hr;
};

HRESULT LoopbackCaptureThreadFunction(bool *capture_stop, threadSafePcmBuffer &threadSafePcmBuffer_);

int LCGetSampleRate(void);
int LCGetNChannels(void);
int LCGetFormat(void);
int LCGetBitsPerSample(void);

bool LCInitCompeted(void);


#define PCMBUFFERLENGTH 192000