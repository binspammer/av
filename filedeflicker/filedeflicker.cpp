#include "filedeflicker.h"

#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <iostream>
#include <iomanip>
#include <exception>
#include <stdexcept>

FileDeflicker::FileDeflicker(const string& src_filename, const string& dst_filename)
: _src_filename{src_filename}
, _dst_filename(dst_filename)
, _dst_file(nullptr)
, _fmt_ctx(nullptr)
, _dec_ctx(nullptr)
, _stream(nullptr)
, _frame(nullptr)
, _dst_bufsize(0)
, _stream_idx(-1)
, _frame_count(0)
{}

FileDeflicker::~FileDeflicker()
{
  avcodec_close(_dec_ctx);
  avformat_close_input(&_fmt_ctx);
  fclose(_dst_file);
  av_free(_frame);
  av_free(_dst_data[0]);
}

void FileDeflicker::init()
{
  av_log_set_level(AV_LOG_DEBUG);
  
  // register all formats and codecs 
  av_register_all();
  
  // open input file, and allocate format context 
  if (avformat_open_input(&_fmt_ctx, _src_filename.c_str(), NULL, NULL) < 0)
    throw std::runtime_error("Could not open source file " + _src_filename);
  
  // retrieve stream information 
  if (avformat_find_stream_info(_fmt_ctx, NULL) < 0) 
    throw std::runtime_error("Could not find stream information");
  
  if (open_codec_context(&_stream_idx, _fmt_ctx, AVMEDIA_TYPE_VIDEO) >= 0) {
    _stream = _fmt_ctx->streams[_stream_idx];
    _dec_ctx = _stream->codec;
    _dst_file = fopen(_dst_filename.c_str(), "wb");
    if (!_dst_file) 
      throw std::runtime_error("Could not open destination file " + _dst_filename);
    
    // allocate image where the decoded image will be put 
    int ret = av_image_alloc(_dst_data, _dst_linesize, _dec_ctx->width, _dec_ctx->height,
                             _dec_ctx->pix_fmt, 1);
    if (ret < 0) 
      throw std::runtime_error("Could not allocate raw video buffer");
    _dst_bufsize = ret;
  }
  
  // dump input information to stderr 
  av_dump_format(_fmt_ctx, 0, _src_filename.c_str(), 0);
  if (!_stream) 
    throw std::runtime_error("Could not find audio or video stream in the input, aborting");
  _frame = avcodec_alloc_frame();
  if (!_frame) 
    throw std::runtime_error("Could not allocate frame");
  
  // initialize packet, set data to NULL, let the demuxer fill it 
  av_init_packet(&_pkt);
  _pkt.data = NULL;
  _pkt.size = 0;
}

void FileDeflicker::process()
{
  decode();
  encode();
}

void FileDeflicker::decode()
{
  //  if (_stream)
  //    std::cout <<"Demuxing video from file " <<_src_filename <<" into " <<_dst_filename <<std::endl<<std::flush;
  //  throw std::runtime_error("Demuxing video from file '" + src_filename + "' into '" + dst_filename);
  int got_frame;
  
  // read frames from the file
  while (av_read_frame(_fmt_ctx, &_pkt) >= 0) {
    decode_packet(&got_frame, 0);
    av_free_packet(&_pkt);
  }
  // flush cached frames 
  _pkt.data = NULL;
  _pkt.size = 0;
  do {
    decode_packet(&got_frame, 1);
  } while (got_frame);
  std::cout<<"Demuxing succeeded."<<std::endl;
  
  if (_stream) {
    printf("Play the output video file with the command:\nffplay -f rawvideo -pix_fmt %s -video_size %dx%d %s\n",
           av_get_pix_fmt_name(_dec_ctx->pix_fmt), _dec_ctx->width, _dec_ctx->height, _dst_filename.c_str());
  }
}

int FileDeflicker::decode_packet(int *got_frame, int cached)
{
  if (_pkt.stream_index == _stream_idx) {
    // decode video frame
    int len = avcodec_decode_video2(_dec_ctx, _frame, got_frame, &_pkt);
    if (len < 0) 
      throw std::runtime_error("Error decoding video frame");
    if (*got_frame) {
      std::cout << "video_frame:"<< (cached ? "(cached)" : "")  <<" #"    << _frame_count++ 
                << " coded_n: "  << _frame->coded_picture_number <<" pts:" << _frame->pts << std::endl;
      
      // copy decoded frame to destination buffer: 
      // this is required since rawvideo expects non aligned data
      av_image_copy(_dst_data, _dst_linesize, (const uint8_t **)(_frame->data), _frame->linesize,
                    _dec_ctx->pix_fmt, _dec_ctx->width, _dec_ctx->height);
      // write to rawvideo file
      fwrite(_dst_data[0], 1, _dst_bufsize, _dst_file);
      _img.push_back(make_shared<uint8_t*>(_dst_data[0]));
    }
  }
  
  return 0;
}

void FileDeflicker::encode()
{

}

int FileDeflicker::open_codec_context(int *stream_idx, AVFormatContext *fmt_ctx, enum AVMediaType type)
{
  int ret = av_find_best_stream(fmt_ctx, type, -1, -1, NULL, 0);
  if (ret < 0) 
    throw std::runtime_error("Could not find stream in input file " + _src_filename);
  
  *stream_idx = ret;
  AVStream *st = fmt_ctx->streams[*stream_idx];
  
  // find decoder for the stream 
  AVCodecContext *dec_ctx = st->codec;
  AVCodec *dec = avcodec_find_decoder(dec_ctx->codec_id);
  if (!dec) 
    throw std::runtime_error("Failed to find  codec");
  if ((ret = avcodec_open2(dec_ctx, dec, NULL)) < 0) 
    throw std::runtime_error("Failed to open  codec");
  
  return 0;
}

int FileDeflicker::get_format_from_sample_fmt(const char **fmt, enum AVSampleFormat sample_fmt)
{
  struct sample_fmt_entry 
  {
    enum AVSampleFormat sample_fmt; 
    const char *fmt_be, *fmt_le;
  };
  
  sample_fmt_entry sample_fmt_entries[] = {
    { AV_SAMPLE_FMT_U8, "u8", "u8" },
    { AV_SAMPLE_FMT_S16, "s16be", "s16le" },
    { AV_SAMPLE_FMT_S32, "s32be", "s32le" },
    { AV_SAMPLE_FMT_FLT, "f32be", "f32le" },
    { AV_SAMPLE_FMT_DBL, "f64be", "f64le" },
  };
  
  *fmt = NULL;
  for (auto i(0U); i < FF_ARRAY_ELEMS(sample_fmt_entries); ++i) 
  {
    struct sample_fmt_entry *entry = &sample_fmt_entries[i];
    if (sample_fmt == entry->sample_fmt) {
      *fmt = AV_NE(entry->fmt_be, entry->fmt_le);
      return 0;
    }
  }
  throw std::runtime_error("sample format " + std::string(av_get_sample_fmt_name(sample_fmt)) 
                           + "is not supported as output format");
}


