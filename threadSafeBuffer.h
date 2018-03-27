#pragma once
#include <mutex>
#include <boost/asio/buffer.hpp>

class threadSafePcmBuffer {
public:
	std::mutex pcmMtx;
	std::condition_variable pcmCv;

	threadSafePcmBuffer();

	void write(unsigned char *data, unsigned int length);
	unsigned char *getBuffer(unsigned int &length);
	unsigned int GetPcmLength();
private:
	const unsigned int pcmBufferLength = 192000;
	unsigned char *internalPcm;
	unsigned char *pcmBuff;
	unsigned int pcmLength;
};