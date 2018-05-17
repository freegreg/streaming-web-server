#include "threadSafeBuffer.h"

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
	resampler2 = speex_resampler_init(channels, 44100, sampleRate, resamplerQuality, &err);
	resampler1 = speex_resampler_init(channels, 44100, sampleRate, resamplerQuality, &err);

	std::cout << speex_resampler_strerror(err);

	opusFile = fopen("example.opus", "wb");
	writeOggHeader(opusFile);
	std::cout << "Created and opened opusFile " << std::endl;

}

threadSafePcmBuffer::~threadSafePcmBuffer() {
	std::cout << "Close opusFile " << std::endl;
	fclose(opusFile);
	opus_encoder_destroy(encoder);
}

void threadSafePcmBuffer::CloseOpusFile() {
	std::cout << "Close opusFile " << std::endl;
	fclose(opusFile);
}

void threadSafePcmBuffer::write(unsigned char *data, unsigned int length) {
	if ((pcmLength + length) >= pcmBufferLength)
		pcmLength = 0;
	std::copy(data, data + length, internalPcm + pcmLength);
	pcmLength = pcmLength + length;
}

void threadSafePcmBuffer::writePcmFloatBuffer(float *data, unsigned int length)
{
	if (length != 0 && length <= pcmBufferLength) {
		if ((endPcmFloatBuffer + length) <= pcmBufferLength) {
			std::copy(data, data + length, pcmFloatBuffer + endPcmFloatBuffer);
			endPcmFloatBuffer += length;
			if ((endPcmFloatBuffer >= startPcmFloatBuffer) && ((endPcmFloatBuffer - length) < startPcmFloatBuffer))
				startPcmFloatBuffer = endPcmFloatBuffer + 1;
		}
		else if ((endPcmFloatBuffer + length) > pcmBufferLength) {
			std::copy(data, data + (pcmBufferLength - endPcmFloatBuffer), pcmFloatBuffer + endPcmFloatBuffer);
			std::copy(data + (pcmBufferLength - endPcmFloatBuffer), data + length, pcmFloatBuffer);
			endPcmFloatBuffer = length - (pcmBufferLength - endPcmFloatBuffer);
			if (endPcmFloatBuffer >= startPcmFloatBuffer)
				startPcmFloatBuffer = endPcmFloatBuffer + 1; 
		}
		if (startPcmFloatBuffer >= pcmBufferLength)
			startPcmFloatBuffer = 0;
	}
}
void threadSafePcmBuffer::readPcmFloatBuffer(float *data, unsigned int length)
{
	if (length != 0 && length < pcmBufferLength && length < getLengthPcmFloatBuffer()) {
		if ((startPcmFloatBuffer + length) <= pcmBufferLength) {
			std::copy(pcmFloatBuffer + startPcmFloatBuffer, pcmFloatBuffer + startPcmFloatBuffer + length, data);
			startPcmFloatBuffer += length;
		}
		else if ((startPcmFloatBuffer + length) >= pcmBufferLength) {
			std::copy(pcmFloatBuffer + startPcmFloatBuffer, pcmFloatBuffer + pcmBufferLength, data);
			std::copy(pcmFloatBuffer, pcmFloatBuffer + (length - (pcmBufferLength - startPcmFloatBuffer)), data + (pcmBufferLength - startPcmFloatBuffer));
			startPcmFloatBuffer = length - (pcmBufferLength - startPcmFloatBuffer);
		}
		if (startPcmFloatBuffer >= pcmBufferLength)
			startPcmFloatBuffer = 0;
	}
}
unsigned int threadSafePcmBuffer::getLengthPcmFloatBuffer()
{
	if (startPcmFloatBuffer <= endPcmFloatBuffer)
		return endPcmFloatBuffer - startPcmFloatBuffer; 
	else
		return endPcmFloatBuffer + (pcmBufferLength - startPcmFloatBuffer);
}

void threadSafePcmBuffer::encodePcmToOpusOgg(unsigned char *data, unsigned int length) {
	float *internalPcmFloat = reinterpret_cast<float*>(data);
	writePcmFloatBuffer(internalPcmFloat, length / 4);
	//unsigned int len = getLengthPcmFloatBuffer();
	while (getLengthPcmFloatBuffer() >= 882) {
		//std::copy(internalPcmFloat, internalPcmFloat + 882, pcmFloat);
		readPcmFloatBuffer(pcmFloat, 882);
		//unsigned int len = getLengthPcmFloatBuffer();
		//resample
		unsigned int sampleLength = 441;
		unsigned int resampleLength = 480;
		int err = speex_resampler_process_interleaved_float(resampler1, pcmFloat, &sampleLength, pcmFloatResampled, &resampleLength);

		//encode resampled into opus
		//unsigned char *opusRaw = new unsigned char[maxPacketSize];
		int opusLength = opus_encode_float(encoder, pcmFloatResampled, resampleLength, opusRaw, maxPacketSize);
		if (opusLength<0)
		{
			fprintf(stderr, "encode failed: %s\n", opus_strerror(opusLength));
		}
		else
			writeOgg(opusRaw, opusLength, opusFile);
	}
}

void threadSafePcmBuffer::encodePcmToOpusOggTest(unsigned char *data, unsigned int length) {
	float pcmFloat[882];
	float pcmFloatResampled[960];
	int nbBytes;

	//pcm.clear();
	//const float *internalPcmFloat = reinterpret_cast<const float*>(data);
	const float *internalPcmFloat = reinterpret_cast<const float*>(data);
	std::copy(internalPcmFloat, internalPcmFloat + length / 4, std::back_inserter(pcm));


	/* Encode the frame. */
	if (length >= 882){
		
		std::copy(internalPcmFloat, internalPcmFloat + 882, pcmFloat);
		
		//std::copy(pcm.begin(), pcm.begin() + 882, pcmFloat);
		
		//resample
		unsigned int sampleLength = 441;
		unsigned int resampleLength;
		int err = speex_resampler_process_interleaved_float(resampler2, pcmFloat, &sampleLength, pcmFloatResampled, &resampleLength);

		unsigned char *opusRaw = new unsigned char[maxPacketSize];
		int opusLength = opus_encode_float(encoder, pcmFloatResampled, resampleLength, opusRaw, maxPacketSize);
		if (opusLength<0)
		{
			fprintf(stderr, "encode failed: %s\n", opus_strerror(opusLength));
			length = 0;
		}
		else
			writeOgg(opusRaw, opusLength, opusFile);
		//pcm.erase(pcm.begin(), pcm.begin() + 882);
	}
}

unsigned char* threadSafePcmBuffer::getOpusEncodedBuffer(unsigned char *data, unsigned int &length) {
	float pcmFloat[882];
	float pcmFloatResampled[960];
	int nbBytes;
	/* Encode the frame. */

	const float *internalPcmFloat = reinterpret_cast<const float*>(data);
	std::copy(internalPcmFloat, internalPcmFloat + 882, pcmFloat);

	//resample
	int err;

	unsigned int sampleLength = 441;
	unsigned int resampleLength;
	err = speex_resampler_process_interleaved_float(resampler2, pcmFloat, &sampleLength, pcmFloatResampled, &resampleLength);

	unsigned char *cbits = new unsigned char[maxPacketSize];
	nbBytes = opus_encode_float(encoder, pcmFloatResampled, resampleLength, cbits, maxPacketSize);
	length = nbBytes;
	if (nbBytes<0)
	{
		fprintf(stderr, "encode failed: %s\n", opus_strerror(nbBytes));
		length = 0;
	}
	
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
