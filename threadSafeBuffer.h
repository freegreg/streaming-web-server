#pragma once
#include <mutex>
#include <boost/asio/buffer.hpp>
#include "ogg.h"
#include "opus.h"
#include "speex_resampler.h"
#include "opus_ogg.h"
#include <iostream>
#define pcmBufferLength 2000

class threadSafePcmBuffer {
public:
	std::mutex pcmMtx;
	std::condition_variable pcmCv;

	threadSafePcmBuffer();
	~threadSafePcmBuffer();

	void write(unsigned char *data, unsigned int length);
	unsigned char *getBuffer(unsigned int &length);
	unsigned char *getOpusEncodedBuffer(unsigned char *data, unsigned int &length);
	void encodePcmToOpusOgg(unsigned char *data, unsigned int length);
	void encodePcmToOpusOggTest(unsigned char *data, unsigned int length);

	void writePcmFloatBuffer(float *data, unsigned int length);
	void readPcmFloatBuffer(float *data, unsigned int length);
	unsigned int getLengthPcmFloatBuffer();

	unsigned int GetPcmLength();
	void CloseOpusFile();
private:
	OpusEncoder *encoder;
	SpeexResamplerState *resampler1;
	SpeexResamplerState *resampler2;

	std::vector<float> pcm;

	FILE * opusFile;

	float pcmFloatBuffer[pcmBufferLength];
	unsigned int startPcmFloatBuffer = 0;
	unsigned int endPcmFloatBuffer = 1;

	float pcmFloat[882];
	float pcmFloatResampled[960];
	unsigned char opusRaw[9600];

	unsigned char *internalPcm;
	unsigned char *pcmBuff;
	unsigned int pcmLength;


	unsigned int frameSize = 480;
	unsigned int sampleRate = 48000;
	unsigned int channels = 2;
	unsigned int application = OPUS_APPLICATION_AUDIO;
	unsigned int bitRate = 128000;
	unsigned int maxFrameSize = 6 * 480;
	unsigned int maxPacketSize = (3 * 1276);

	unsigned int resamplerQuality = 10;


};