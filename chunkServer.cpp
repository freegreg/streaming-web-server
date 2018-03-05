#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include <boost/array.hpp>
#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include "lame.h"

#include <stdio.h>
#include <windows.h>
#include "loopback-capture_lib\common.h"
#include "loopback-capture_lib\loopback-capture.h"

#include "getIPs.h"

#include <iostream>
#include <fstream>  
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
short int * pcm;
extern short int pcm_l[8000];
extern short int pcm_r[8000];
bool bKeepWaiting = true;
extern int pcmLength;

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

class chunk_connection
{
public:

	chunk_connection(boost::asio::io_service& io_service): socket_(io_service), acceptor_(io_service, tcp::endpoint(tcp::v4(), 8080))
	{
		// Asynchronous accept connection.
		acceptor_.async_accept(socket(), boost::bind(&chunk_connection::start, this, boost::asio::placeholders::error));

		
	}

	/// Get the socket associated with the connection
	tcp::socket& socket() { return socket_; }

	/// Start asynchronous http chunk coding.
	void start(const boost::system::error_code& error)
	{
		// On error, return early.
		if (error)
		{
			close();
			return;
		}
		std::cout << "Local IP Adress: " << socket_.local_endpoint().address().to_string() << std::endl;
		std::cout << "Remote IP Adress: " << socket_.remote_endpoint().address().to_string() << std::endl;

		// Start writing the header.
		read_request();
	}

private:
	boost::asio::streambuf buf;
	// Write http header.
	void read_request()
	{
		//boost::shared_ptr<boost::asio::streambuf> buf(new boost::asio::streambuf);
		
		boost::asio::async_read_until(socket_,
			buf,
			"\r\n\r\n",
			boost::bind(&chunk_connection::handle_read_request, this,
				boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred));

	}
	/// Handle read of http request.
	void handle_read_request(const boost::system::error_code& error, std::size_t bytes_recieved)
	{
		// On error, return early.
		if (!error)
		{
			std::string s = buffer_to_string(buf);
			std::string delimiter = "\n";
			std::string req = s.substr(0, s.find(delimiter));

			std::cout << "Message length: " << bytes_recieved << "; Req: " << req  << std::endl;
		}
		else
		{
			std::cout << "Error occurred." << std::endl;
			close();
			return;
		}
		write_header();
	}

	// Write http header.
	void write_header()
	{
		std::cout << "Writing http header." << std::endl;

		// Start chunked transfer coding.  Write http headers:
		boost::asio::async_write(socket_,
			boost::asio::buffer(http_chunk_header),
			boost::bind(&chunk_connection::handle_write_header, this,
				boost::asio::placeholders::error));
	}

	/// Handle writing of http header.
	void handle_write_header(const boost::system::error_code& error)
	{
		// On error, return early.
		if (error)
		{
			close();
			return;
		}

		handle_read_chunk(error);
	}

	// Handle reading a file chunk.
	void handle_read_chunk(const boost::system::error_code& error)
	{
		// On non-eof error, return early.
		if (error)
		{
			std::cout << "Error reading chunk..." << error << std::endl;
			close();
			return;
		}

		std::size_t bytes_transferred;
		bool eof;
		int mp3length = 0;
		int j = 0;
		while (mp3length == 0) {
			j++;
			std::unique_lock<std::mutex> lck(mtx);
			while (pcmLength == 0) cv.wait(lck);
			//std::cout << pcmLength << std::endl;
			if (pcmLength > 0) {
				
				mp3length = lame_encode_buffer(gfp, pcm_l, pcm_r, pcmLength, mp3buffer, MAXMP3BUFFER);
				//body_stream_.write(reinterpret_cast<const char*>(mp3buffer), mp3length);
				if (mp3length > 0) {
					//body_stream_.write(reinterpret_cast<const char*>(mp3buffer), mp3length);
					j == 0;
				}
				
				pcmLength = 0;
			}

		}

		if (!bKeepWaiting) {
			int finalFrames = lame_encode_flush_nogap(gfp, mp3buffer, MAXMP3BUFFER);
			if (finalFrames > 0) {
				body_stream_.write(reinterpret_cast<const char*>(mp3buffer), finalFrames);
			}
			eof = true;
		}
		
		//bytes_transferred = body_stream_.rdbuf()->in_avail();
		//std::cout << bytes_transferred << std::endl;
		//std::cout << stream_size << " - stream size." << std::endl;
		if (mp3length >= bufSize) {
			body_stream_.read(chunk_data_, bufSize);
			bytes_transferred = bufSize;
			std::copy(mp3buffer, mp3buffer + bufSize, chunk_data_);
			eof = false;
		}
		else {
			bytes_transferred = mp3length;
			std::copy(mp3buffer, mp3buffer + mp3length, chunk_data_);
			eof = false;
		}
		
		write_chunk(bytes_transferred, eof);
	}

	// Prepare chunk and write to socket.
	void write_chunk(std::size_t bytes_transferred, bool eof)
	{
		std::vector<boost::asio::const_buffer> buffers;

		// If data was read, create a chunk-body.
		if (bytes_transferred)
		{
			buffers.push_back(boost::asio::buffer(chunk_data_, bytes_transferred));
		}

		// If eof, append last-chunk to outbound data.
		if (eof)
		{
			//std::cout << "Writing last-chunk..." << std::endl;
			eof = true;
		}

		//std::cout << "Writing chunk..." << std::endl;

		// Write to chunk to socket.
		boost::asio::async_write(socket_, buffers,
			boost::bind(&chunk_connection::handle_write_chunk, this,
				boost::asio::placeholders::error,
				eof));
	}

	// Handle writing a chunk.
	void handle_write_chunk(const boost::system::error_code& error,
		bool eof)
	{
		//std::cout << "Writing chunk..." << std::endl;
		// If eof or error, then shutdown socket and return.
		if (eof || error)
		{
			if (error) {
				std::cout << "Error writing chunk..." << error << std::endl;
			}
			// Initiate graceful connection closure.
			boost::system::error_code ignored_ec;
			socket_.shutdown(tcp::socket::shutdown_both, ignored_ec);
			close();
			return;
		}

		// Otherwise, body_stream_ still has data.
		handle_read_chunk(error);
	}

	// Close the socket and body_stream.
	void close()
	{
		std::cout << "Close...Wait for anaother request" << std::endl;
		boost::system::error_code ignored_ec;
		socket_.close(ignored_ec);
		// Asynchronous accept connection.
		//body_stream_.write(reinterpret_cast<const char*>(mp3buffer), mp3length);
		body_stream_.str(std::string());
		acceptor_.async_accept(socket(), boost::bind(&chunk_connection::start, this, boost::asio::placeholders::error));
		//body_st ream_.close(ignored_ec);
	}

private:

	// Socket for the connection.
	tcp::socket socket_;
	tcp::acceptor acceptor_;
	tcp::endpoint endpoint_;
	// Stream being chunked.
	//std::stringstream body_stream_;
	
	// Buffer to read part of the file into.
	//boost::array<char, 100000> chunk_data_;
	char chunk_data_[bufSize];

	// Buffer holds hex encoded value of chunk_data_'s valid size.
	std::string chunk_size_;

	// Name of pipe.
	//std::string pipe_name_;
};
void createWave();
long int createWavePCM(short int ** pcm_arr);

int main(int argc, LPCWSTR argv[])
{
	printLocalIP(4);
	gfp = lame_init(); /* initialize libmp3lame */

	lame_set_num_channels(gfp, 2);
	lame_set_in_samplerate(gfp, 44100);
	lame_set_brate(gfp, 320);
	lame_set_mode(gfp, STEREO);
	lame_set_quality(gfp, 2);   /* 2=high  5 = medium  7=low */

	int ret_code = lame_init_params(gfp);

	if (ret_code < 0) {
		//if (ret == -1) {
		//display_bitrates(stderr);
		//}
		body_stream_ << "fatal error during initialization\n";
	}
	HRESULT hr = S_OK;

	hr = CoInitialize(NULL);
	if (FAILED(hr)) {
		ERR(L"CoInitialize failed: hr = 0x%08x", hr);
	}
	CoUninitializeOnExit cuoe;

	// parse command line
	CPrefs prefs(argc, argv, hr);
	if (FAILED(hr)) {
		ERR(L"CPrefs::CPrefs constructor failed: hr = 0x%08x", hr);
	}
	if (S_FALSE == hr) {
		// nothing to do
	}

	// create arguments for loopback capture thread
	LoopbackCaptureThreadFunctionArguments threadArgs;
	threadArgs.hr = E_UNEXPECTED; // thread will overwrite this
	threadArgs.pMMDevice = prefs.m_pMMDevice;
	threadArgs.bInt16 = prefs.m_bInt16;
	threadArgs.hFile = prefs.m_hFile;
	threadArgs.nFrames = 0;
	//std::thread t([&](viewWindow* view){ view->refreshWindow(render, playerRect, backTexture, playerTexture); }, &window);
	thread start_capture_thread([&]()
	{
		LoopbackCaptureThreadFunction(&threadArgs, &bKeepWaiting);
	});
	
	//while (pcmLength == 0);
	boost::asio::io_service io_service;
	chunk_connection connection(io_service);
	// Run the service.
	io_service.run();
	bKeepWaiting = false;
	start_capture_thread.join();
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



void enocdePCM() {
	mtx.lock();

	if (nNumFramesToRead > 0) {

		long int MAXMP3BUFFER = 1.25*(float)nNumFramesToRead + 720;
		mp3buffer = new unsigned char[MAXMP3BUFFER];

		pcm = new short int[nNumFramesToRead];
		for (int i = 0; i < nNumFramesToRead; i++) {
			short int x = pData[i * 2];
			short int y = pData[i * 2 + 1];
			pcm[i] = (x) | (y << 8);
		}
		//pcm = reinterpret_cast<short int*>(pData);

//		mp3length = lame_encode_buffer(gfp, pcm, pcm, nNumFramesToRead, mp3buffer, MAXMP3BUFFER);
		//std::cout << "mp3length..." << mp3length << std::endl;
//		body_stream_.write(reinterpret_cast<const char*>(mp3buffer), mp3length);
		delete pcm;
		delete mp3buffer;
		nNumFramesToRead = 0;
	}
	mtx.unlock();
	//return &start_capture_thread;
}