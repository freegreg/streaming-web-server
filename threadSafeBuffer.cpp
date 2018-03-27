#include "threadSafeBuffer.h"
#include <mutex>
#include <boost/asio/buffer.hpp>

threadSafePcmBuffer::threadSafePcmBuffer() {
	internalPcm = new unsigned char[pcmBufferLength];
	pcmBuff = new unsigned char[pcmBufferLength];
	pcmLength = 0;
}

void threadSafePcmBuffer::write(unsigned char *data, unsigned int length) {
	if ((pcmLength + length) >= pcmBufferLength)
		pcmLength = 0;
	std::copy(data, data + length, internalPcm + pcmLength);
	pcmLength = pcmLength + length;
}

unsigned char* threadSafePcmBuffer::getBuffer(unsigned int &length) {
	std::copy(internalPcm, internalPcm + pcmLength, pcmBuff);
	length = pcmLength;
	pcmLength = 0;
	return pcmBuff;
}

unsigned int threadSafePcmBuffer::GetPcmLength() {
	return pcmLength;
}
