#include "server_content.h"

string res_index1 = R"(<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <title>PCM Streamer</title>
</head>
<body>
<div id="container" style="width: 400px; margin: 0 auto;">
    <h2>It should play audio if everying went well!</h2>
</div>
<script>
 window.onload = function() {
   var socketURL =  'ws://' + location.host;
   var player = new PCMPlayer({)";

//encoding: '32bitFloat',
//channels: 2,
//sampleRate: 44100,
//flushingTime: 20
string res_index2 = R"(   });
   var ws = new WebSocket(socketURL);
       ws.binaryType = 'arraybuffer';
       ws.addEventListener('message',function(event) {
			var data = new Uint8Array(event.data);
			if (data.length > 0){
				player.feed(data);
			}
       });
 }   
</script>
<script type="text/javascript" src="../pcm-player.js"></script>
</body>
</html>)";

string GetIndexPlayer(
	int encoding = WAVE_FORMAT_IEEE_FLOAT,
	int bitsPerSample = 4,
	int channels = 2,
	int sampleRate = 44100,
	int flushingTime = 20) {
	string strEncoding;
	string bits = to_string(bitsPerSample);

	if (bitsPerSample == 32 || bitsPerSample == 16 || bitsPerSample == 8)
	switch (encoding) {
	case WAVE_FORMAT_ADPCM:
		strEncoding = "'" + bits + "bitInt'";
		break;
	case WAVE_FORMAT_IEEE_FLOAT:
		strEncoding = "'" + bits + "bitFloat'";
		break;
	default:
		strEncoding = "'32bitFloat'";
		break;
	}
	
	return res_index1 +
		"encoding: " + strEncoding + ",\n" +
		"channels: " + to_string(channels) + ",\n" +
		"sampleRate: " + to_string(sampleRate) + ",\n" +
		"flushingTime: " + to_string(flushingTime) + "\n" +
		res_index2;
}