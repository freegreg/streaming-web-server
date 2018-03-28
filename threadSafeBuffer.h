#pragma once
#include <mutex>
#include <boost/asio/buffer.hpp>
#include "opus.h"

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

	const unsigned int pcmBufferLength = 192000;
	unsigned char *internalPcm;
	unsigned char *pcmBuff;
	unsigned int pcmLength;

	unsigned int frameSize = 441;
	unsigned int sampleRate = 44100;
	unsigned int channels = 2;
	unsigned int application = OPUS_APPLICATION_AUDIO;
	unsigned int bitRate = 64000;
	unsigned int maxFrameSize = 6 * 441;
	unsigned int maxPacketSize = (3 * 1276);
};