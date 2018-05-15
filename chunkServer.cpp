#include <iostream>
#include <sstream>
#include <string>
#include <map>
#include <vector>

#include <boost/array.hpp>
#include <boost/asio.hpp>
#include <boost/bind.hpp>

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
#include "threadSafeBuffer.h"

#include "opus.h"
#include "opus_ogg.h"

using boost::asio::ip::tcp;
using namespace std;

bool bKeepWaiting = true;



int main(int argc, LPCWSTR argv[]){	

	//threadsafe comunication
	threadSafePcmBuffer threadSafePcmBuffer_;

	//print all available ip addresses from available interfaces
	printLocalIP(4);

	thread captureKey([&]()
	{
		std::getchar();
		bKeepWaiting = false;
	});
	
	thread start_capture_thread([&]()
	{
		LoopbackCaptureThreadFunction(&bKeepWaiting, threadSafePcmBuffer_);
	});

	//wait loopback capture initialization
	while (!LCInitCompeted()) std::this_thread::sleep_for(std::chrono::milliseconds(100));

	//create server content map [resource] = resource_content
	map<string, string> contentMap;

	//create server player content (construct html index.html file with javascript with parameters from loopback capture - bir rate, number of channels...)
	string indexHtml = GetIndexPlayer(LCGetFormat(),
		LCGetBitsPerSample(),
		LCGetNChannels(),
		LCGetSampleRate(),
		20);

	contentMap.insert(std::pair<string, string>("/index.html", indexHtml));
	
	//start beast server on port 8088
	startBeastServer(100, 8088, R"(e:\docs\projects\c++projects\ConsoleApplication1\ConsoleApplication1\web_pcm_player\)", contentMap, threadSafePcmBuffer_);
	
	start_capture_thread.join();
	
	std::getchar();
}