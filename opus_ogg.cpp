#include "opus_ogg.h"

#define PACKAGE_NAME "stream_server"
#define PACKAGE_VERSION "v0.1"

typedef struct
{
	void *readdata;
	opus_int64 total_samples_per_channel;
	int rawmode;
	int channels;
	long rate;
	int gain;
	int samplesize;
	int endianness;
	char *infilename;
	int ignorelength;
	int skip;
	int extraout;
	char *comments;
	int comments_length;
	int copy_comments;
	int copy_pictures;
} oe_enc_opt;

/*
Comments will be stored in the Vorbis style.
It is described in the "Structure" section of
http://www.xiph.org/ogg/vorbis/doc/v-comment.html

However, Opus and other non-vorbis formats omit the "framing_bit".

The comment header is decoded as follows:
1) [vendor_length] = read an unsigned integer of 32 bits
2) [vendor_string] = read a UTF-8 vector as [vendor_length] octets
3) [user_comment_list_length] = read an unsigned integer of 32 bits
4) iterate [user_comment_list_length] times {
5) [length] = read an unsigned integer of 32 bits
6) this iteration's user comment = read a UTF-8 vector as [length] octets
}
7) done.
*/

#define readint(buf, base) (((buf[base+3]<<24)&0xff000000)| \
                           ((buf[base+2]<<16)&0xff0000)| \
                           ((buf[base+1]<<8)&0xff00)| \
                           (buf[base]&0xff))
#define writeint(buf, base, val) do{ buf[base+3]=((val)>>24)&0xff; \
                                     buf[base+2]=((val)>>16)&0xff; \
                                     buf[base+1]=((val)>>8)&0xff; \
                                     buf[base]=(val)&0xff; \
                                 }while(0)

static void comment_init(char **comments, int* length, const char *vendor_string)
{
	/*The 'vendor' field should be the actual encoding library used.*/
	int vendor_length = strlen(vendor_string);
	int user_comment_list_length = 0;
	int len = 8 + 4 + vendor_length + 4;
	char *p = (char*)malloc(len);
	if (p == NULL) {
		fprintf(stderr, "malloc failed in comment_init()\n");
		exit(1);
	}
	memcpy(p, "OpusTags", 8);
	writeint(p, 8, vendor_length);
	memcpy(p + 12, vendor_string, vendor_length);
	writeint(p, 12 + vendor_length, user_comment_list_length);
	*length = len;
	*comments = p;
}

void comment_add(char **comments, int* length, char *tag, char *val)
{
	char* p = *comments;
	int vendor_length = readint(p, 8);
	int user_comment_list_length = readint(p, 8 + 4 + vendor_length);
	int tag_len = (tag ? strlen(tag) + 1 : 0);
	int val_len = strlen(val);
	int len = (*length) + 4 + tag_len + val_len;

	p = (char*)realloc(p, len);
	if (p == NULL) {
		fprintf(stderr, "realloc failed in comment_add()\n");
		exit(1);
	}

	writeint(p, *length, tag_len + val_len);      /* length of comment */
	if (tag) {
		memcpy(p + *length + 4, tag, tag_len);        /* comment tag */
		(p + *length + 4)[tag_len - 1] = '=';           /* separator */
	}
	memcpy(p + *length + 4 + tag_len, val, val_len);  /* comment */
	writeint(p, 8 + 4 + vendor_length, user_comment_list_length + 1);
	*comments = p;
	*length = len;
}

static void comment_pad(char **comments, int* length, int amount)
{
	if (amount>0) {
		int i;
		int newlen;
		char* p = *comments;
		/*Make sure there is at least amount worth of padding free, and
		round up to the maximum that fits in the current ogg segments.*/
		newlen = (*length + amount + 255) / 255 * 255 - 1;
		p = (char*)realloc(p, newlen);
		if (p == NULL) {
			fprintf(stderr, "realloc failed in comment_pad()\n");
			exit(1);
		}
		for (i = *length; i<newlen; i++)p[i] = 0;
		*comments = p;
		*length = newlen;
	}
}
#undef readint
#undef writeint

/*Write an Ogg page to a file pointer*/
static inline int oe_write_page(ogg_page *page, FILE *fp)
{
	int written;
	written = fwrite(page->header, 1, page->header_len, fp);
	written += fwrite(page->body, 1, page->body_len, fp);
	return written;
}
ogg_stream_state   os;
/*ogg file*/
void writeOggHeader(FILE *fout) {
	/*Write header*/
	OpusHeader         header;
	ogg_packet         op;
	//ogg_stream_state   os;
	ogg_page           og;
	oe_enc_opt         inopt;
	opus_int64         bytes_written = 0;
	opus_int64         pages_out = 0;
	int                comment_padding = 512;
	int				   ret;
	int                serialno;

	/*OggOpus headers*/ /*FIXME: broke forcemono*/
	unsigned int chan = 2;
	unsigned int rate = 48000;
	header.channels = chan;
	header.channel_mapping = header.channels>8 ? 255 : chan>2;
	header.input_sample_rate = rate;
	header.gain = 0;
	/*Regardless of the rate we're coding at the ogg timestamping/skip is
	always timed at 48000.*/

	/*Initialize Ogg stream struct*/
	if (ogg_stream_init(&os, 0) == -1) {
		fprintf(stderr, "Error: stream init failed\n");
		exit(1);
	}

	header.preskip = 0;
	{
		/*The Identification Header is 19 bytes, plus a Channel Mapping Table for
		mapping families other than 0. The Channel Mapping Table is 2 bytes +
		1 byte per channel. Because the maximum number of channels is 255, the
		maximum size of this header is 19 + 2 + 255 = 276 bytes.*/
		unsigned char header_data[276];
		int packet_size = opus_header_to_packet(&header, header_data, sizeof(header_data));
		op.packet = header_data;
		op.bytes = packet_size;
		op.b_o_s = 1;
		op.e_o_s = 0;
		op.granulepos = 0;
		op.packetno = 0;
		ogg_stream_packetin(&os, &op);

		while ((ret = ogg_stream_flush(&os, &og))) {
			if (!ret)break;
			ret = oe_write_page(&og, fout);
			if (ret != og.header_len + og.body_len) {
				fprintf(stderr, "Error: failed writing header to output stream\n");
				exit(1);
			}
		}


		const char* opus_version = opus_get_version_string();
		/*Vendor string should just be the encoder library,
		the ENCODER comment specifies the tool used.*/
		comment_init(&inopt.comments, &inopt.comments_length, opus_version);

		char               ENCODER_string[1024];
		snprintf(ENCODER_string, sizeof(ENCODER_string), "opusenc from %s %s", PACKAGE_NAME, PACKAGE_VERSION);
		comment_add(&inopt.comments, &inopt.comments_length, "ENCODER", ENCODER_string);

		comment_pad(&inopt.comments, &inopt.comments_length, comment_padding);
		op.packet = (unsigned char *)inopt.comments;
		op.bytes = inopt.comments_length;
		op.b_o_s = 0;
		op.e_o_s = 0;
		op.granulepos = 0;
		op.packetno = 1;
		ogg_stream_packetin(&os, &op);
	}

	/* writing the rest of the Opus header packets */
	while ((ret = ogg_stream_flush(&os, &og))) {
		if (!ret)break;
		ret = oe_write_page(&og, fout);
		if (ret != og.header_len + og.body_len) {
			fprintf(stderr, "Error: failed writing header to output stream\n");
			exit(1);
		}

	}
}

/*ogg file*/
void writeOgg(unsigned char *cbits, int length, FILE *fout) {
	int ret;
	int                max_ogg_delay = 48000; /*48kHz samples*/
	ogg_packet         op;
	static ogg_int64_t        enc_granulepos = 0;
	opus_int32         frame_size = 480;
	
	ogg_page           og;
	static unsigned int id = -1;
	unsigned coding_rate = 48000;
	enc_granulepos += frame_size * 48000 / coding_rate;
	op.packet = (unsigned char *)cbits;
	op.bytes = length;
	op.b_o_s = 0;
	op.e_o_s = 0;
	op.granulepos = enc_granulepos;
	//if (op.e_o_s) {
	//	/*We compute the final GP as ceil(len*48k/input_rate)+preskip. When a
	//	resampling decoder does the matching floor((len-preskip)*input_rate/48k)
	//	conversion, the resulting output length will exactly equal the original
	//	input length when 0<input_rate<=48000.*/
	//	op.granulepos = ((original_samples * 48000 + coding_rate - 1) / rate) + header.preskip;
	//}
	id++;
	op.packetno = 2 + id;
	///*Initialize Ogg stream struct*/
	//if (ogg_stream_init(&os, 0) == -1) {
	//	fprintf(stderr, "Error: stream init failed\n");
	//	exit(1);
	//}

	ogg_stream_packetin(&os, &op);
	while ((ret = ogg_stream_flush(&os, &og))) {
		if (!ret)break;
		ret = oe_write_page(&og, fout);
		if (ret != og.header_len + og.body_len) {
			fprintf(stderr, "Error: failed writing header to output stream\n");
			exit(1);
		}
	}
}