#ifndef FILEDEFLICKER_H
#define FILEDEFLICKER_H

# if __WORDSIZE == 64
#  define INT64_C(c)    c ## L
#  define UINT64_C(c)   c ## UL
# else
#  define INT64_C(c)    c ## LL
#  define UINT64_C(c)   c ## ULL
# endif

extern "C" {
#include <libavutil/log.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#include <libavutil/mathematics.h>
#include "libavcodec/avcodec.h"
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
}

#include <string>
#include <vector>
#include <memory>

using namespace std;

class FileDeflicker
{
public:
  FileDeflicker() = delete;
  FileDeflicker(FileDeflicker&&) = delete;

  FileDeflicker(const string &src_filename, const string &dst_filename);
  virtual ~FileDeflicker();

  void init();
  void process();
  void encode();
  void decode();

private:
  int decode_packet(int *got_frame, int cached);
  int open_codec_context(int *_stream_idx, AVFormatContext *_fmt_ctx, enum AVMediaType type);
  int get_format_from_sample_fmt(const char **fmt, enum AVSampleFormat sample_fmt);

private:
  const string _src_filename;
  const string _dst_filename;
  FILE *_dst_file;

  AVFormatContext *_fmt_ctx;
  AVCodecContext *_dec_ctx;
  AVStream *_stream;
  AVFrame *_frame;
  AVPacket _pkt;
  uint8_t *_dst_data[4];
  vector<shared_ptr<uint8_t*>> _img;
  int _dst_linesize[4];
  int _dst_bufsize;
  int _stream_idx;
  int _frame_count;
};

#endif // FILEDEFLICKER_H
