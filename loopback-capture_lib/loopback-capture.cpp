// loopback-capture.cpp
#include "common.h"
//#include <iostream>
#include <algorithm>
#include <iterator>
#include <vector>
#include <sstream>
#include <mutex>          // std::mutex
#include <stdio.h>
#include <iostream>
#include <fstream>
#include "prefs.h"
#include "../threadSafeBuffer.h"
#include "../opus_ogg.h"

using namespace std;

BYTE *pData;
UINT32 nNumFramesToRead;
DWORD dwFlags;

unsigned char pcm[PCMBUFFERLENGTH];
unsigned int pcmLength = 0;
volatile bool initCompleted = false;
long long pnFrames;
WAVEFORMATEX *pwfx;


HRESULT LoopbackCaptureThreadFunction(bool *capture_stop, threadSafePcmBuffer &threadSafePcmBuffer_) {

	IMMDevice *pMMDevice = NULL;
	HRESULT hr = S_OK;

	hr = CoInitialize(NULL);
	if (FAILED(hr)) {
		ERR(L"CoInitialize failed: hr = 0x%08x", hr);
	}
	CoUninitializeOnExit cuoe;

	// create arguments for loopback capture thread
	// open default device if not specified
	if (NULL == pMMDevice) {
		hr = get_default_device(&pMMDevice);
		if (FAILED(hr)) {
			return hr;
		}
	}

    // activate an IAudioClient
    IAudioClient *pAudioClient;
    hr = pMMDevice->Activate(__uuidof(IAudioClient), CLSCTX_ALL, NULL, (void**)&pAudioClient);
    if (FAILED(hr)) {
        ERR(L"IMMDevice::Activate(IAudioClient) failed: hr = 0x%08x", hr);
        return hr;
    }
    ReleaseOnExit releaseAudioClient(pAudioClient);
    
    // get the default device periodicity
    REFERENCE_TIME hnsDefaultDevicePeriod;
	REFERENCE_TIME minDevicePeriod;
	    hr = pAudioClient->GetDevicePeriod(&hnsDefaultDevicePeriod, &minDevicePeriod);
		std::cout << "hnsDefaultDevicePeriod (ms) " << hnsDefaultDevicePeriod  /10000 << std::endl;
		std::cout << "minDevicePeriod  (ms) " << minDevicePeriod  /10000 << std::endl;
    if (FAILED(hr)) {
        ERR(L"IAudioClient::GetDevicePeriod failed: hr = 0x%08x", hr);
        return hr;
    }

    // get the default device format
    
    hr = pAudioClient->GetMixFormat(&pwfx);
    if (FAILED(hr)) {
        ERR(L"IAudioClient::GetMixFormat failed: hr = 0x%08x", hr);
        return hr;
    }
    CoTaskMemFreeOnExit freeMixFormat(pwfx);


	// create a periodic waitable timer
    HANDLE hWakeUp = CreateWaitableTimer(NULL, FALSE, NULL);
    if (NULL == hWakeUp) {
        DWORD dwErr = GetLastError();
        ERR(L"CreateWaitableTimer failed: last error = %u", dwErr);
        return HRESULT_FROM_WIN32(dwErr);
    }
    CloseHandleOnExit closeWakeUp(hWakeUp);

	// set the waitable timer
	LARGE_INTEGER liFirstFire;
	liFirstFire.QuadPart = -hnsDefaultDevicePeriod / 2; // negative means relative time
	LONG lTimeBetweenFires = (LONG)hnsDefaultDevicePeriod / 2 / (10 * 1000); // convert to milliseconds
	BOOL bOK = SetWaitableTimer(
		hWakeUp,
		&liFirstFire,
		lTimeBetweenFires,
		NULL, NULL, FALSE
	);
	if (!bOK) {
		DWORD dwErr = GetLastError();
		ERR(L"SetWaitableTimer failed: last error = %u", dwErr);
		return HRESULT_FROM_WIN32(dwErr);
	}
	CancelWaitableTimerOnExit cancelWakeUp(hWakeUp);

    UINT32 nBlockAlign = pwfx->nBlockAlign;
    pnFrames = 0;
    
    // call IAudioClient::Initialize
    // note that AUDCLNT_STREAMFLAGS_LOOPBACK and AUDCLNT_STREAMFLAGS_EVENTCALLBACK
    // do not work together...
    // the "data ready" event never gets set
    // so we're going to do a timer-driven loop
    hr = pAudioClient->Initialize(
        AUDCLNT_SHAREMODE_SHARED,
        AUDCLNT_STREAMFLAGS_LOOPBACK,
        0, 0, pwfx, 0
    );
    if (FAILED(hr)) {
        ERR(L"IAudioClient::Initialize failed: hr = 0x%08x", hr);
        return hr;
    }

	std::cout << "format type " << pwfx->wFormatTag << std::endl;
	std::cout << "number of channels (i.e. mono, stereo...)  " << pwfx->nChannels << std::endl;
	std::cout << "sample rate " << pwfx->nSamplesPerSec << std::endl;
	std::cout << "for buffer estimation " << pwfx->nAvgBytesPerSec << std::endl;
	std::cout << "block size of data " << pwfx->nBlockAlign << std::endl;
	std::cout << "number of bits per sample of mono data " << pwfx->wBitsPerSample << std::endl;
	std::cout << "the count in bytes of the size of extra information (after cbSize)  " << pwfx->cbSize << std::endl;

	switch (pwfx->wFormatTag) {
	case WAVE_FORMAT_IEEE_FLOAT:
		//pwfx->wFormatTag = WAVE_FORMAT_PCM;
		//pwfx->wBitsPerSample = 16;
		//pwfx->nBlockAlign = pwfx->nChannels * pwfx->wBitsPerSample / 8;
		//pwfx->nAvgBytesPerSec = pwfx->nBlockAlign * pwfx->nSamplesPerSec;

		break;

	case WAVE_FORMAT_EXTENSIBLE:
	{
		// naked scope for case-local variable
		PWAVEFORMATEXTENSIBLE pEx = reinterpret_cast<PWAVEFORMATEXTENSIBLE>(pwfx);
		std::cout << "SubFormat type1 " << pEx->SubFormat.Data1 << std::endl;
		std::cout << "SubFormat type2 " << pEx->SubFormat.Data2 << std::endl;
		std::cout << "SubFormat type3 " << pEx->SubFormat.Data3 << std::endl;
		std::cout << "SubFormat type4 " << pEx->SubFormat.Data4 << std::endl;
		if (IsEqualGUID(KSDATAFORMAT_SUBTYPE_IEEE_FLOAT, pEx->SubFormat)) {
			std::cout << "KSDATAFORMAT_SUBTYPE_IEEE_FLOAT  " << std::endl;
			//pEx->SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
			//pEx->Samples.wValidBitsPerSample = 16;
			//pwfx->wBitsPerSample = 16;
			//pwfx->nBlockAlign = pwfx->nChannels * pwfx->wBitsPerSample / 8;
			//pwfx->nAvgBytesPerSec = pwfx->nBlockAlign * pwfx->nSamplesPerSec;
		}
		else {
			ERR(L"%s", L"Don't know how to coerce mix format to int-16");
			return E_UNEXPECTED;
		}
	}
	break;

	default:
		ERR(L"Don't know how to coerce WAVEFORMATEX with wFormatTag = 0x%08x to int-16", pwfx->wFormatTag);
		return E_UNEXPECTED;
	}

	unsigned int BlockAlign = pwfx->nBlockAlign;
    // activate an IAudioCaptureClient
    IAudioCaptureClient *pAudioCaptureClient;
    hr = pAudioClient->GetService(
        __uuidof(IAudioCaptureClient),
        (void**)&pAudioCaptureClient
    );
    if (FAILED(hr)) {
        ERR(L"IAudioClient::GetService(IAudioCaptureClient) failed: hr = 0x%08x", hr);
        return hr;
    }
    ReleaseOnExit releaseAudioCaptureClient(pAudioCaptureClient);
    
     // call IAudioClient::Start
    hr = pAudioClient->Start();
    if (FAILED(hr)) {
        ERR(L"IAudioClient::Start failed: hr = 0x%08x", hr);
        return hr;
    }
    AudioClientStopOnExit stopAudioClient(pAudioClient);
	initCompleted = true;

    bool bFirstPacket = true;


	//std::ofstream opusFile;
	//opusFile.open("example.opus", ios::out | ios::trunc | ios::binary);
	//FILE * opusFile;
	//opusFile = fopen("example2.opus", "wb");
	//writeOggHeader(opusFile);
	//std::cout << "Created and opened opusFile " << std::endl;

    for (UINT32 nPasses = 0; *capture_stop; nPasses++) {
		
        // drain data while it is available
        UINT32 nNextPacketSize;
        for (
            hr = pAudioCaptureClient->GetNextPacketSize(&nNextPacketSize);
            SUCCEEDED(hr) && nNextPacketSize > 0;
            hr = pAudioCaptureClient->GetNextPacketSize(&nNextPacketSize)
        ) {
            // get the captured data
			
            hr = pAudioCaptureClient->GetBuffer(
                &pData,
                &nNumFramesToRead,
                &dwFlags,
                NULL,
                NULL
                );
			if (nNumFramesToRead > 0) {
				unsigned char *opus;
				unsigned int opus_length;

				std::unique_lock<std::mutex> lck(threadSafePcmBuffer_.pcmMtx);

				//threadSafePcmBuffer_.write(pData, nNumFramesToRead * BlockAlign);
				threadSafePcmBuffer_.encodePcmToOpusOgg(pData, nNumFramesToRead * BlockAlign);

				//if (nNumFramesToRead > 441){
					//opus = threadSafePcmBuffer_.getOpusEncodedBuffer(pData, opus_length);
					//writeOgg(opus, opus_length, opusFile);
					//opusFile.write(reinterpret_cast<char*>(opus), opus_length);
			//}
				threadSafePcmBuffer_.pcmCv.notify_all();

			}

            if (FAILED(hr)) {
                ERR(L"IAudioCaptureClient::GetBuffer failed on pass %u after %u frames: hr = 0x%08x", nPasses, pnFrames, hr);
                return hr;
            }

            if (bFirstPacket && (AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY & dwFlags)) {
                LOG(L"%s", L"Probably spurious glitch reported on first packet");
            } 
			if(bFirstPacket && (AUDCLNT_BUFFERFLAGS_SILENT & dwFlags)) {
				LOG(L"%s", L"Treat all of the data in the packet as silence and ignore the actual data values.");
			}
			if (bFirstPacket && (AUDCLNT_BUFFERFLAGS_TIMESTAMP_ERROR & dwFlags)) {
				LOG(L"%s", L"The time at which the device's stream position was recorded is uncertain.");
			}
			if (dwFlags > 4) {
                LOG(L"IAudioCaptureClient::GetBuffer set flags to 0x%08x on pass %u after %u frames", dwFlags, nPasses, pnFrames);
                return E_UNEXPECTED;
            }

            if (0 == nNumFramesToRead) {
                ERR(L"IAudioCaptureClient::GetBuffer said to read 0 frames on pass %u after %u frames", nPasses, pnFrames);
                return E_UNEXPECTED;
            }

#pragma prefast(suppress: __WARNING_INCORRECT_ANNOTATION, "IAudioCaptureClient::GetBuffer SAL annotation implies a 1-byte buffer")
           
            hr = pAudioCaptureClient->ReleaseBuffer(nNumFramesToRead);
            if (FAILED(hr)) {
                ERR(L"IAudioCaptureClient::ReleaseBuffer failed on pass %u after %u frames: hr = 0x%08x", nPasses, pnFrames, hr);
                return hr;
            }

            bFirstPacket = false;
        }

        if (FAILED(hr)) {
            ERR(L"IAudioCaptureClient::GetNextPacketSize failed on pass %u after %u frames: hr = 0x%08x", nPasses, pnFrames, hr);
            return hr;
        }
		
		WaitForSingleObject(
			hWakeUp,
			INFINITE
		);
    } // capture loop
	threadSafePcmBuffer_.CloseOpusFile();
	//fclose(opusFile);
    return hr;
}
int LCGetFormat(void){
	PWAVEFORMATEXTENSIBLE pEx = reinterpret_cast<PWAVEFORMATEXTENSIBLE>(pwfx);
	return pEx->SubFormat.Data1;
}

int LCGetBitsPerSample(void) {
	return pwfx->wBitsPerSample;
}

int LCGetSampleRate(void) {
	return pwfx->nSamplesPerSec;
}

int LCGetNChannels(void) {
	return pwfx->nChannels;
}

bool LCInitCompeted(void) {
	return initCompleted;
}