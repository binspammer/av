#include <cstdio>
#include <cstring>
#include <cmath>
#include <memory>
#include <iostream>
#include <iomanip>
#include <exception>
#include <stdexcept>

using namespace std;

#ifdef HAVE_AV_CONFIG_H
#undef HAVE_AV_CONFIG_H
#endif

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
//#include <libavutil/channel_layout.h>
#include <libavutil/mathematics.h>
#include "libavcodec/avcodec.h"
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
}

/* 5 seconds stream duration */
#define STREAM_DURATION 200.0
#define STREAM_FRAME_RATE 25 /* 25 images/s */
#define STREAM_NB_FRAMES ((int)(STREAM_DURATION * STREAM_FRAME_RATE))
#define STREAM_PIX_FMT PIX_FMT_YUV420P /* default pix_fmt */
static int sws_flags = SWS_BICUBIC;
/**************************************************************/
/* audio output */
float t, tincr, tincr2;
int16_t *samples;
int audio_input_frame_size;

/* Add an output stream. */
static AVStream *add_stream(AVFormatContext *oc, AVCodec **codec, enum CodecID codec_id)
{
  AVCodecContext *c;
  AVStream *st;
  /* find the encoder */
  *codec = avcodec_find_encoder(codec_id);
  if (!(*codec)) {
    fprintf(stderr, "Could not find encoder for '%s'\n",
            avcodec_get_type(codec_id));
    exit(1);
  }
  st = avformat_new_stream(oc, *codec);
  if (!st) {
    fprintf(stderr, "Could not allocate stream\n");
    exit(1);
  }
  st->id = oc->nb_streams-1;
  c = st->codec;
  switch ((*codec)->type) {
    case AVMEDIA_TYPE_AUDIO:
      st->id = 1;
      c->sample_fmt = AV_SAMPLE_FMT_S16;
      c->bit_rate = 64000;
      c->sample_rate = 44100;
      c->channels = 2;
      break;
    case AVMEDIA_TYPE_VIDEO:
      c->codec_id = codec_id;
      c->bit_rate = 400000;
      /* Resolution must be a multiple of two. */
      c->width = 352;
      c->height = 288;
      /* timebase: This is the fundamental unit of time (in seconds) in terms
* of which frame timestamps are represented. For fixed-fps content,
* timebase should be 1/framerate and timestamp increments should be
* identical to 1. */
      c->time_base.den = STREAM_FRAME_RATE;
      c->time_base.num = 1;
      c->gop_size = 12; /* emit one intra frame every twelve frames at most */
      c->pix_fmt = STREAM_PIX_FMT;
      if (c->codec_id == CODEC_ID_MPEG2VIDEO) {
        /* just for testing, we also add B frames */
        c->max_b_frames = 2;
      }
      if (c->codec_id == CODEC_ID_MPEG1VIDEO) {
        /* Needed to avoid using macroblocks in which some coeffs overflow.
* This does not happen with normal video, it just happens here as
* the motion of the chroma plane does not match the luma plane. */
        c->mb_decision = 2;
      }
      break;
    default:
      break;
  }
  /* Some formats want stream headers to be separate. */
  if (oc->oformat->flags & AVFMT_GLOBALHEADER)
    c->flags |= CODEC_FLAG_GLOBAL_HEADER;
  return st;
}

/**************************************************************/
/* video output */
static AVFrame *frame;
static AVPicture src_picture, dst_picture;
static int frame_count;
static void open_video(AVFormatContext *oc, AVCodec *codec, AVStream *st)
{
  int ret;
  AVCodecContext *c = st->codec;
  /* open the codec */
  ret = avcodec_open2(c, codec, NULL);
  if (ret < 0) {
    fprintf(stderr, "Could not open video codec: %s\n"/*, av_err2str(ret)*/);
    exit(1);
  }
  /* allocate and init a re-usable frame */
  frame = avcodec_alloc_frame();
  if (!frame) {
    fprintf(stderr, "Could not allocate video frame\n");
    exit(1);
  }
  /* Allocate the encoded raw picture. */
  ret = avpicture_alloc(&dst_picture, c->pix_fmt, c->width, c->height);
  if (ret < 0) {
    fprintf(stderr, "Could not allocate picture: %s\n"/*, av_err2str(ret)*/);
    exit(1);
  }
  /* If the output format is not YUV420P, then a temporary YUV420P
* picture is needed too. It is then converted to the required
* output format. */
  if (c->pix_fmt != PIX_FMT_YUV420P) {
    ret = avpicture_alloc(&src_picture, PIX_FMT_YUV420P, c->width, c->height);
    if (ret < 0) {
      fprintf(stderr, "Could not allocate temporary picture: %s\n"/*, av_err2str(ret)*/);
      exit(1);
    }
  }
  /* copy data and linesize picture pointers to frame */
  *((AVPicture *)frame) = dst_picture;
}

/* Prepare a dummy image. */
static void fill_yuv_image(AVPicture *pict, int frame_index,
                           int width, int height)
{
  int x, y, i;
  i = frame_index;
  /* Y */
  for (y = 0; y < height; y++)
    for (x = 0; x < width; x++)
      pict->data[0][y * pict->linesize[0] + x] = x + y + i * 3;
  /* Cb and Cr */
  for (y = 0; y < height / 2; y++) {
    for (x = 0; x < width / 2; x++) {
      pict->data[1][y * pict->linesize[1] + x] = 128 + y + i * 2;
      pict->data[2][y * pict->linesize[2] + x] = 64 + x + i * 5;
    }
  }
}

static void write_video_frame(AVFormatContext *oc, AVStream *st)
{
  int ret;
  static struct SwsContext *sws_ctx;
  AVCodecContext *c = st->codec;
  if (frame_count >= STREAM_NB_FRAMES) {
    /* No more frames to compress. The codec has a latency of a few
* frames if using B-frames, so we get the last frames by
* passing the same picture again. */
  } else {
    if (c->pix_fmt != PIX_FMT_YUV420P) {
      /* as we only generate a YUV420P picture, we must convert it
* to the codec pixel format if needed */
      if (!sws_ctx) {
        sws_ctx = sws_getContext(c->width, c->height, PIX_FMT_YUV420P,
                                 c->width, c->height, c->pix_fmt,
                                 sws_flags, NULL, NULL, NULL);
        if (!sws_ctx) {
          fprintf(stderr,
                  "Could not initialize the conversion context\n");
          exit(1);
        }
      }
      fill_yuv_image(&src_picture, frame_count, c->width, c->height);
      sws_scale(sws_ctx,
                (const uint8_t * const *)src_picture.data, src_picture.linesize,
                0, c->height, dst_picture.data, dst_picture.linesize);
    } else {
      fill_yuv_image(&dst_picture, frame_count, c->width, c->height);
    }
  }
  if (oc->oformat->flags & AVFMT_RAWPICTURE) {
    /* Raw video case - directly store the picture in the packet */
    AVPacket pkt;
    av_init_packet(&pkt);
    pkt.flags |= AV_PKT_FLAG_KEY;
    pkt.stream_index = st->index;
    pkt.data = dst_picture.data[0];
    pkt.size = sizeof(AVPicture);
    ret = av_interleaved_write_frame(oc, &pkt);
  } else {
    AVPacket pkt = { 0 };
    int got_packet;
    av_init_packet(&pkt);
    /* encode the image */
//    ret = avcodec_encode_video(c, &pkt, frame, &got_packet);
    ret = avcodec_encode_video(c, (uint8_t*)&pkt.data, pkt.size, frame);
    if (ret < 0) {
      fprintf(stderr, "Error encoding video frame: %s\n"/*, av_err2str(ret)*/);
      exit(1);
    }
    /* If size is zero, it means the image was buffered. */
    if (!ret && got_packet && pkt.size) {
      pkt.stream_index = st->index;
      /* Write the compressed frame to the media file. */
      ret = av_interleaved_write_frame(oc, &pkt);
    } else {
      ret = 0;
    }
  }
  if (ret != 0) {
    fprintf(stderr, "Error while writing video frame: %s\n"/*, av_err2str(ret)*/);
    exit(1);
  }
  frame_count++;
}

static void close_video(AVFormatContext *oc, AVStream *st)
{
  avcodec_close(st->codec);
  av_free(src_picture.data[0]);
  av_free(dst_picture.data[0]);
  av_free(frame);
}

/**************************************************************/
/* media file output */
int
main(int argc, char **argv)
try
{
  const char *filename;
  AVOutputFormat *fmt;
  AVFormatContext *oc;
  AVStream *video_st;
  AVCodec *video_codec;
  double video_pts;
  int ret;

  av_log_set_level(AV_LOG_DEBUG);

  /* Initialize libavcodec, and register all codecs and formats. */
  av_register_all();

  if (argc != 2) {
    printf("usage: %s output_file\n"
           "API example program to output a media file with libavformat.\n"
           "This program generates a video stream, encodes and muxes into a file named output_file.\n"
           "The output format is automatically guessed according to the file extension.\n"
           "Raw images can also be output by using '%%d' in the filename.\n"
           "\n", argv[0]);
    return 1;
  }

  filename = argv[1];

  // allocate the output media context
  oc = avformat_alloc_context();
  oc->oformat = av_guess_format(NULL, filename, NULL);
//  avformat_alloc_output_context2(&oc, NULL, NULL, filename);
  if (!oc->oformat) {
    printf("Could not deduce output format from file extension: using MPEG.\n");
    oc->oformat = av_guess_format("mpeg", NULL, NULL);
//    avformat_alloc_output_context2(&oc, NULL, "mpeg", filename);
  }
  if (!oc) {
    return 1;
  }

  fmt = oc->oformat;
  // Add the audio and video streams using the default format codecs * and initialize the codecs.
  video_st = NULL;
  if (fmt->video_codec != CODEC_ID_NONE) {
    video_st = add_stream(oc, &video_codec, fmt->video_codec);
  }

  // Now that all the parameters are set, we can open the audio and * video codecs and allocate the necessary encode buffers.
  if (video_st)
    open_video(oc, video_codec, video_st);

  av_dump_format(oc, 0, filename, 1);
  /* open the output file, if needed */
  if (!(fmt->flags & AVFMT_NOFILE)) {
    ret = avio_open(&oc->pb, filename, AVIO_FLAG_WRITE);
    if (ret < 0) {
      fprintf(stderr, "Could not open '%s': %s\n", filename/*, av_err2str(ret)*/);
      return 1;
    }
  }
  // Write the stream header, if any.
  ret = avformat_write_header(oc, NULL);
  if (ret < 0) {
    fprintf(stderr, "Error occurred when opening output file: %s\n"/*, av_err2str(ret)*/);
    return 1;
  }
  if (frame)
    frame->pts = 0;
  for (;;) {
    // Compute current audio and video time.
    if (video_st)
      video_pts = (double)video_st->pts.val * video_st->time_base.num / video_st->time_base.den;
    else
      video_pts = 0.0;
    if (!video_st || video_pts >= STREAM_DURATION)
      break;
    // write interleaved audio and video frames
    if (video_st ) {
      write_video_frame(oc, video_st);
      frame->pts += av_rescale_q(1, video_st->codec->time_base, video_st->time_base);
    }
  }
  /* Write the trailer, if any. The trailer must be written before you
* close the CodecContexts open when you wrote the header; otherwise
* av_write_trailer() may try to use memory that was freed on
* av_codec_close(). */
  av_write_trailer(oc);
  // Close each codec.
  if (video_st)
    close_video(oc, video_st);
  if (!(fmt->flags & AVFMT_NOFILE))
    // Close the output file.
    avio_close(oc->pb);
  // free the stream
  avformat_free_context(oc);
  return 0;
}
catch(exception& e)
{
  std::cerr<<e.what()<<std::endl;
}