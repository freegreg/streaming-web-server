#pragma once
#include <string>
#include <mutex>
#include <map>
#define PCMBUFFERLENGTH 192000
using namespace std;

extern std::mutex mtx;
extern std::condition_variable cv;
extern unsigned char pcm[PCMBUFFERLENGTH];
extern unsigned int pcmLength;



int startBeastServer(int maxNumberOfThreads, unsigned short listeningPort, std::string const doc_root, map<string, string> contentMap);