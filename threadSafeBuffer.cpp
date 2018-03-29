#include "threadSafeBuffer.h"
#include <mutex>
#include <boost/asio/buffer.hpp>
#include "opus.h"
#include "speex_resampler.h"

threadSafePcmBuffer::threadSafePcmBuffer() {
	internalPcm = new unsigned char[pcmBufferLength];
	pcmBuff = new unsigned char[pcmBufferLength];
	pcmLength = 0;

	/*Create a new encoder state */
	int err;
	encoder = opus_encoder_create(sampleRate, channels, application, &err);
	if (err<0)
	{
		fprintf(stderr, "failed to create an encoder: %s\n", opus_strerror(err));
	}
	/* Set the desired bit-rate. You can also set other parameters if needed.
	The Opus library is designed to have good defaults, so only set
	parameters you know you need. Doing otherwise is likely to result
	in worse quality, but better. */
	err = opus_encoder_ctl(encoder, OPUS_SET_BITRATE(bitRate));
	if (err<0)
	{
		fprintf(stderr, "failed to set bitrate: %s\n", opus_strerror(err));
	}
	
	resampler = speex_resampler_init(channels, 44100, sampleRate, resamplerQuality, &err);

}

void threadSafePcmBuffer::write(unsigned char *data, unsigned int length) {
	if ((pcmLength + length) >= pcmBufferLength)
		pcmLength = 0;
	std::copy(data, data + length, internalPcm + pcmLength);
	pcmLength = pcmLength + length;
}

unsigned char* threadSafePcmBuffer::getOpusEncodedBuffer(unsigned int &length) {
	int nbBytes;
	/* Encode the frame. */
	
	float *pcmFloat = new float[882];
	const float *internalPcmFloat = reinterpret_cast<const float*>(internalPcm);
	std::copy(internalPcmFloat, internalPcmFloat + 882, pcmFloat);

	//resample
	int err;
	float *pcmFloatResampled = new float[960*2];
	unsigned int sampleLength = 882;
	unsigned int resampleLength;
	err = speex_resampler_process_interleaved_float(resampler, pcmFloat, &sampleLength, pcmFloatResampled, &resampleLength);

	unsigned char *cbits = new unsigned char[maxPacketSize];
	nbBytes = opus_encode_float(encoder, pcmFloatResampled, resampleLength, cbits, maxPacketSize);
	if (nbBytes<0)
	{
		fprintf(stderr, "encode failed: %s\n", opus_strerror(nbBytes));
	}
	length = nbBytes;
	//pcmLength = 0;
	return cbits;
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
