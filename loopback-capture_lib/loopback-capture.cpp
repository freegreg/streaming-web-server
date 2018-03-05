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

using namespace std;

int sum;
int sound_length;
PCCH pSound;

BYTE *pData;
UINT32 nNumFramesToRead;
DWORD dwFlags;
mutex mtx;
std::condition_variable cv;
short int pcm_l[8000];
short int pcm_r[8000];
int pcmLength;

void createWav(LPCWAVEFORMATEX pwfx, BYTE* pSoundData, LONG pSoundDataLength, UINT32 nFrames);

void LoopbackCaptureThreadFunction(LoopbackCaptureThreadFunctionArguments *pArgs, bool *capture_stop) {
	pArgs->hr = CoInitialize(NULL);
	if (FAILED(pArgs->hr)) {
		ERR(L"CoInitialize failed: hr = 0x%08x", pArgs->hr);
	}
	CoUninitializeOnExit cuoe;

	pArgs->hr = LoopbackCapture(
		pArgs->pMMDevice,
		pArgs->hFile,
		pArgs->bInt16,
		&pArgs->nFrames,
		capture_stop
	);
}

HRESULT LoopbackCapture(IMMDevice *pMMDevice,HMMIO hFile,bool bInt16,PUINT32 pnFrames, bool *capture_stop) {
    HRESULT hr;

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
    hr = pAudioClient->GetDevicePeriod(&hnsDefaultDevicePeriod, NULL);
    if (FAILED(hr)) {
        ERR(L"IAudioClient::GetDevicePeriod failed: hr = 0x%08x", hr);
        return hr;
    }

    // get the default device format
    WAVEFORMATEX *pwfx;
    hr = pAudioClient->GetMixFormat(&pwfx);
    if (FAILED(hr)) {
        ERR(L"IAudioClient::GetMixFormat failed: hr = 0x%08x", hr);
        return hr;
    }
    CoTaskMemFreeOnExit freeMixFormat(pwfx);



        // coerce int-16 wave format
        // can do this in-place since we're not changing the size of the format
        // also, the engine will auto-convert from float to int for us
        switch (pwfx->wFormatTag) {
            case WAVE_FORMAT_IEEE_FLOAT:
                pwfx->wFormatTag = WAVE_FORMAT_PCM;
                pwfx->wBitsPerSample = 16;
                pwfx->nBlockAlign = pwfx->nChannels * pwfx->wBitsPerSample / 8;
				pwfx->nAvgBytesPerSec = pwfx->nBlockAlign * pwfx->nSamplesPerSec;

                break;

            case WAVE_FORMAT_EXTENSIBLE:
                {
                    // naked scope for case-local variable
                    PWAVEFORMATEXTENSIBLE pEx = reinterpret_cast<PWAVEFORMATEXTENSIBLE>(pwfx);
                    if (IsEqualGUID(KSDATAFORMAT_SUBTYPE_IEEE_FLOAT, pEx->SubFormat)) {
						std::cout << "KSDATAFORMAT_SUBTYPE_IEEE_FLOAT  " << std::endl;
                        pEx->SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
                        pEx->Samples.wValidBitsPerSample = 16;
                        pwfx->wBitsPerSample = 16;
                        pwfx->nBlockAlign = pwfx->nChannels * pwfx->wBitsPerSample / 8;
                        pwfx->nAvgBytesPerSec = pwfx->nBlockAlign * pwfx->nSamplesPerSec;
                    } else {
                        ERR(L"%s", L"Don't know how to coerce mix format to int-16");
                        return E_UNEXPECTED;
                    }
                }
                break;

            default:
                ERR(L"Don't know how to coerce WAVEFORMATEX with wFormatTag = 0x%08x to int-16", pwfx->wFormatTag);
                return E_UNEXPECTED;
        }

		
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
    *pnFrames = 0;
    
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

    bool bFirstPacket = true;
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
				std::unique_lock<std::mutex> lck(mtx);
				for (int i = 0; i < nNumFramesToRead; i++) {
					unsigned char x = pData[i * 4];
					unsigned char x1 = pData[i * 4 + 1];
					short int sample = ((x1 & 0x000000FF) << 8) | (x & 0x000000FF);
					pcm_r[i] = (sample);
					x = pData[i * 4 + 2];
					x1 = pData[i * 4 + 3];
					sample = ((x1 & 0x000000FF) << 8) | (x & 0x000000FF);
					pcm_l[i] = (sample);
				}
				pcmLength = nNumFramesToRead;
				cv.notify_all();
			}

            if (FAILED(hr)) {
                ERR(L"IAudioCaptureClient::GetBuffer failed on pass %u after %u frames: hr = 0x%08x", nPasses, *pnFrames, hr);
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
                LOG(L"IAudioCaptureClient::GetBuffer set flags to 0x%08x on pass %u after %u frames", dwFlags, nPasses, *pnFrames);
                return E_UNEXPECTED;
            }

            if (0 == nNumFramesToRead) {
                ERR(L"IAudioCaptureClient::GetBuffer said to read 0 frames on pass %u after %u frames", nPasses, *pnFrames);
                return E_UNEXPECTED;
            }

#pragma prefast(suppress: __WARNING_INCORRECT_ANNOTATION, "IAudioCaptureClient::GetBuffer SAL annotation implies a 1-byte buffer")
           
            hr = pAudioCaptureClient->ReleaseBuffer(nNumFramesToRead);
            if (FAILED(hr)) {
                ERR(L"IAudioCaptureClient::ReleaseBuffer failed on pass %u after %u frames: hr = 0x%08x", nPasses, *pnFrames, hr);
                return hr;
            }

            bFirstPacket = false;
        }

        if (FAILED(hr)) {
            ERR(L"IAudioCaptureClient::GetNextPacketSize failed on pass %u after %u frames: hr = 0x%08x", nPasses, *pnFrames, hr);
            return hr;
        }
		
		WaitForSingleObject(
			hWakeUp,
			INFINITE
		);
    } // capture loop

    return hr;
}

namespace little_endian_io
{
	template <typename Word>
	std::ostream& write_word(std::ostream& outs, Word value, unsigned size = sizeof(Word))
	{
		for (; size; --size, value >>= 8)
			outs.put(static_cast <char> (value & 0xFF));
		return outs;
	}
}
using namespace little_endian_io;

void createWav(LPCWAVEFORMATEX pwfx, BYTE* pSoundData, LONG pSoundDataLength, UINT32 nFrames) {
	stringstream wave;
	// Write the file headers
	wave << "RIFF----WAVEfmt ";     // (chunk size to be filled in later)
								 // make a RIFF/WAVE chunk
	
	LONG lBytesInWfx = sizeof(WAVEFORMATEX) + pwfx->cbSize;
	write_word(wave, lBytesInWfx);
	wave.write(reinterpret_cast<PCHAR>(const_cast<LPWAVEFORMATEX>(pwfx)), lBytesInWfx);


	//write_word(f, 16, 4);  // no extension data
	//write_word(f, 1, 2);  // PCM - integer samples
	//write_word(f, 2, 2);  // two channels (stereo file)
	//write_word(f, 44100, 4);  // samples per second (Hz)
	//write_word(f, 176400, 4);  // (Sample Rate * BitsPerSample * Channels) / 8
	//write_word(f, 4, 2);  // data block size (size of two integer samples, one for each channel, in bytes)
	//write_word(f, 16, 2);  // number of bits per sample (use a multiple of 8)
	
	// make a 'fact' chunk whose data is (DWORD)0
	wave << "fact"; // (chunk size to be filled in later)
					 // write the correct data to the fact chunk
	write_word(wave, 4);
	wave.write(reinterpret_cast<PCHAR>(&nFrames), sizeof(nFrames));
	
	// Write the data chunk header
	size_t data_chunk_pos = wave.tellp();
	wave << "data----";  // (chunk size to be filled in later)

					  // Write the audio samples
	wave.write(reinterpret_cast<PCHAR>(pSoundData), pSoundDataLength);

	// Write the audio samples
	// (We'll generate a single C4 note with a sine wave, fading from left to right)
	//constexpr double two_pi = 6.283185307179586476925286766559;
	//constexpr double max_amplitude = 32760;  // "volume"
	//
	//double hz = 44100;    // samples per second
	//double frequency = 261.626;  // middle C
	//double seconds = 0.2;      // time
	//
	//int N = hz * seconds;  // total number of samples
	//for (int n = 0; n < N; n++)
	//{
	//	double amplitude = (double)n / N * max_amplitude;
	//	double value = sin((two_pi * n * frequency) / hz);
	//	write_word(f, (int)(amplitude  * value), 2);
	//	write_word(f, (int)((max_amplitude - amplitude) * value), 2);
	//}
	//
	// (We'll need the final file size to fix the chunk sizes above)
	size_t file_length = wave.tellp();

	// Fix the data chunk header to contain the data size
	wave.seekp(data_chunk_pos + 4);
	write_word(wave, file_length - data_chunk_pos + 8);

	// Fix the file header to contain the proper RIFF chunk size, which is (file size - 8) bytes
	wave.seekp(0 + 4);
	write_word(wave, file_length - 8, 4);
}