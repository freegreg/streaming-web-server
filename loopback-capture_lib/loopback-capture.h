// loopback-capture.h

// call CreateThread on this function
// feed it the address of a LoopbackCaptureThreadFunctionArguments
// it will capture via loopback from the IMMDevice
// and dump output to the HMMIO
// until the stop event is set
// any failures will be propagated back via hr
#pragma once
struct LoopbackCaptureThreadFunctionArguments {
    IMMDevice *pMMDevice;
    bool bInt16;
    HMMIO hFile;
    UINT32 nFrames;
    HRESULT hr;
};

HRESULT LoopbackCapture(IMMDevice *pMMDevice, bool *capture_stop);
void LoopbackCaptureThreadFunction(bool *capture_stop);

int LCGetSampleRate(void);
int LCGetNChannels(void);
int LCGetFormat(void);
int LCGetBitsPerSample(void);

bool LCInitCompeted(void);


#define PCMBUFFERLENGTH 192000