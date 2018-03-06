#pragma once
#include <string>
#include <mutex>  
using namespace std;

extern std::mutex mtx;
extern std::condition_variable cv;
extern unsigned char pcm[32000];
extern int pcmLength;

int startBeastServer(int maxNumberOfThreads, unsigned short listeningPort, std::string const doc_root);