#include <iostream>
#include <sstream>
#include <string>
#include <map>
#include <vector>

#include <boost/array.hpp>
#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include "lame.h"

#include <stdio.h>
#include <windows.h>
#include "loopback-capture_lib\common.h"
#include "loopback-capture_lib\loopback-capture.h"

#include "server_content.h"
#include "getIPs.h"

#include <iostream>
#include <fstream>  

#include <boost/beast.hpp>

#include "beast_server.h"
#define bufSize 100000

using boost::asio::ip::tcp;
using namespace std;
std::stringstream body_stream_;

std::string html_player =
"<audio controls>"
"<source src = \"localhost:8080/cpatured.wav\" type = \"audio/wav\">"
"< / audio>";

const std::string http_chunk_header =
"HTTP/1.1 200 OK\r\n"
"Cache-Control:no-cache, no-store\r\n"
"Content-Type:audio/mpeg\r\n"
"\r\n";

const char crlf[] = { '\r', '\n' };
const char last_chunk[] = { '0', '\r', '\n' };
std::stringstream wav;
std::stringstream wav_pcm;


extern BYTE *pData;
extern UINT32 nNumFramesToRead;
extern DWORD dwFlags;
extern std::mutex mtx;
extern std::condition_variable cv;

lame_t  gfp;

bool bKeepWaiting = true;


long int MAXMP3BUFFER = 32000;//1.25*(float)nNumFramesToRead + 720;
unsigned char *mp3buffer = new unsigned char[MAXMP3BUFFER];

std::string to_hex_string(std::size_t value)
{
	std::ostringstream stream;
	stream << std::hex << value;
	return stream.str();
}

std::string buffer_to_string(const boost::asio::streambuf &buffer)
{
	using boost::asio::buffers_begin;

	auto bufs = buffer.data();
	std::string result(buffers_begin(bufs), buffers_begin(bufs) + buffer.size());
	return result;
}

int main(int argc, LPCWSTR argv[])
{	
	printLocalIP(4);
	
	thread start_capture_thread([&]()
	{
		LoopbackCaptureThreadFunction(&bKeepWaiting);
	});

	while (!LCInitCompeted());

	map<string, string> contentMap;
	contentMap.insert(std::pair<string, string>("index.html", 
		GetIndexPlayer(LCGetFormat(),
			LCGetBitsPerSample(),
			LCGetNChannels(),
			LCGetSampleRate(),
			20)
		));
	std::cout << contentMap["index.html"];
	startBeastServer(100, 8080, R"(G:\projects\chunkServer\ConsoleApplication1\web_pcm_player)", contentMap);
	start_capture_thread.join();
	
	//gfp = lame_init(); /* initialize libmp3lame */
	//int ret_code = lame_init_params(gfp);

	//if (ret_code < 0) {
	//	//if (ret == -1) {
	//	//display_bitrates(stderr);
	//	//}
	//	body_stream_ << "fatal error during initialization\n";
	//}

	//
	//lame_set_num_channels(gfp, LoopbackCaptureGetNChannels());
	//lame_set_in_samplerate(gfp, LoopbackCaptureGetSampleRate());
	//lame_set_brate(gfp, 320);
	//lame_set_mode(gfp, STEREO);
	//lame_set_quality(gfp, 2);   /* 2=high  5 = medium  7=low */

	
	std::getchar();
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

void createWave()
{
	//wav.str(std::string());
	
	// Write the file headers
	wav << "RIFF----WAVEfmt ";     // (chunk size to be filled in later)
	write_word(wav, 16, 4);  // no extension data
	write_word(wav, 1, 2);  // PCM - integer samples
	write_word(wav, 2, 2);  // two channels (stereo file)
	write_word(wav, 44100, 4);  // samples per second (Hz)
	write_word(wav, 176400, 4);  // (Sample Rate * BitsPerSample * Channels) / 8
	write_word(wav, 4, 2);  // data block size (size of two integer samples, one for each channel, in bytes)
	write_word(wav, 16, 2);  // number of bits per sample (use a multiple of 8)

						   // Write the data chunk header
	size_t data_chunk_pos = wav.tellp();
	wav << "data----";  // (chunk size to be filled in later)

					  // Write the audio samples
					  // (We'll generate a single C4 note with a sine wave, fading from left to right)
	constexpr double two_pi = 6.283185307179586476925286766559;
	constexpr double max_amplitude = 32760;  // "volume"

	double hz = 44100;    // samples per second
	double frequency = 261.626;  // middle C
	double seconds = 2.5;      // time

	long int N = hz * seconds;  // total number of samples
	for (int n = 0; n < N; n++)
	{
		double amplitude = (double)n / N * max_amplitude;
		double value = sin((two_pi * n * frequency) / hz);
		write_word(wav, (int)(amplitude  * value), 2);
		write_word(wav, (int)((max_amplitude - amplitude) * value), 2);
	}

	// (We'll need the final file size to fix the chunk sizes above)
	size_t file_length = wav.tellp();

	// Fix the data chunk header to contain the data size
	wav.seekp(data_chunk_pos + 4);
	write_word(wav, file_length - data_chunk_pos + 8);

	// Fix the file header to contain the proper RIFF chunk size, which is (file size - 8) bytes
	wav.seekp(0 + 4);
	write_word(wav, file_length - 8, 4);
}

long int createWavePCM(short int ** pcm_arr)
{
	//wav.str(std::string());

						// Write the audio samples
						// (We'll generate a single C4 note with a sine wave, fading from left to right)
	constexpr double two_pi = 6.283185307179586476925286766559;
	constexpr double max_amplitude = 32760;  // "volume"

	double hz = 44100;    // samples per second
	double frequency = 261.626;  // middle C
	double seconds = 25;      // time

	long int N = hz * seconds;  // total number of samples
	*pcm_arr = new short int[N*2];
	for (int n = 0; n < N; n++)
	{
		double amplitude = (double)n / N * max_amplitude;
		frequency += 0.1;
		double value = sin((two_pi * n * frequency) / hz);
		(*pcm_arr)[2 * n] = (short int)(amplitude  * value);
		(*pcm_arr)[2 * n + 1] = (short int)((max_amplitude - amplitude) * value);
		//write_word(wav_pcm, (int)(amplitude  * value), 2);
		//write_word(wav_pcm, (int)((max_amplitude - amplitude) * value), 2);
	}
	std::cout << "pcm..." << (*pcm_arr)[0] << std::endl << (*pcm_arr)[1] << std::endl << (*pcm_arr)[2] << std::endl << (*pcm_arr)[3] << std::endl << (*pcm_arr)[4] << std::endl;
	return N;
}