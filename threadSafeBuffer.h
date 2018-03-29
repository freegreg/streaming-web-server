#pragma once
#include <mutex>
#include <boost/asio/buffer.hpp>
#include "opus.h"
#include "speex_resampler.h"

class threadSafePcmBuffer {
public:
	std::mutex pcmMtx;
	std::condition_variable pcmCv;

	threadSafePcmBuffer();

	void write(unsigned char *data, unsigned int length);
	unsigned char *getBuffer(unsigned int &length);
	unsigned char *getOpusEncodedBuffer(unsigned int &length);
	unsigned int GetPcmLength();
private:
	OpusEncoder *encoder;
	SpeexResamplerState *resampler;

	const unsigned int pcmBufferLength = 192000;
	unsigned char *internalPcm;
	unsigned char *pcmBuff;
	unsigned int pcmLength;


	unsigned int frameSize = 480;
	unsigned int sampleRate = 48000;
	unsigned int channels = 2;
	unsigned int application = OPUS_APPLICATION_AUDIO;
	unsigned int bitRate = 64000;
	unsigned int maxFrameSize = 6 * 480;
	unsigned int maxPacketSize = (3 * 1276);

	unsigned int resamplerQuality = 10;
};