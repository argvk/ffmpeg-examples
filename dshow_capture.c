/**
 * LOTS OF TODO & BUGS
 * 1. framerate is screwed
 * 2. more comments
 * 3. fix code HELL
 * compile using
 * > gcc dshow_capture.c -I"..\ffmpeg-dev\include" -L"..\ffmpeg-shared" -o dshow_capture -lavcodec-55 -lavdevice-55 -lavfilter-4 -lavformat-55 -lavutil-52 -lswscale-2
 */
#include <libavcodec/avcodec.h>
#include <libavdevice/avdevice.h>
#include <libavfilter/avfilter.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libswscale/swscale.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>

int main(int argc, char **argv)
{
  av_register_all(); 
  avcodec_register_all();
  avdevice_register_all();
  avfilter_register_all();

  AVCodec *pCodecOut;
  AVCodec *pCodecInCam;
  AVCodec *pCodecInScr;

  AVCodecContext *pCodecCtxOut= NULL;
  AVCodecContext *pCodecCtxInCam = NULL;
  AVCodecContext *pCodecCtxInScr = NULL;

  char args_cam[512],args_scr[512];

  int i, ret, got_output,video_stream_idx_cam,video_stream_idx_scr;

  AVFrame *cam_frame,*outFrame,*filt_frame,*scr_frame;
  AVPacket packet;

  const char *filename = "out.webm";

  AVOutputFormat *pOfmtOut = NULL;
  AVStream *strmVdoOut = NULL;

  AVInputFormat *inFrmt= av_find_input_format("dshow");

  AVDictionary *inOptions = NULL;

  AVFormatContext *pFormatCtxOut;
  AVFormatContext *pFormatCtxInCam;
  AVFormatContext *pFormatCtxInScr;

  AVRational millisecondbase = {1,1000};

  struct SwsContext *sws_ctx_scr, *sws_ctx_cam;

  AVFilter *buffersrc_cam  = avfilter_get_by_name("buffer");
  AVFilter *buffersrc_scr  = avfilter_get_by_name("buffer");
  AVFilter *buffersink = avfilter_get_by_name("buffersink");
  AVFilterInOut *outputs = avfilter_inout_alloc();
  AVFilterInOut *outputs_scr = avfilter_inout_alloc();
  AVFilterInOut *inputs  = avfilter_inout_alloc();
  AVFilterContext *buffersink_ctx;
  AVFilterContext *buffersrc_ctx_cam;
  AVFilterContext *buffersrc_ctx_scr;
  AVFilterGraph *filter_graph;

  
  AVBufferSinkParams *buffersink_params;

  const char *filters_descr_padding = "scale='w=-1:h=715',pad='1280:720:(max(0,(1280-iw)))/2:(max(0,(720-ih)))/2:black',format='yuv420p'"; // add padding for 16:9 and then scale to 1280x720

  /////////////////////////// decoder 

  pFormatCtxInScr = avformat_alloc_context();
  pFormatCtxInCam = avformat_alloc_context();

  // set le resolution
  av_dict_set(&inOptions, "video_size", "640x480", 0);
  av_dict_set(&inOptions, "r", "10", 0);

  ret = avformat_open_input(&pFormatCtxInCam, "video=ManyCam Virtual Webcam", inFrmt, &inOptions); // replacement for my webcam
  ret = avformat_open_input(&pFormatCtxInScr, "video=UScreenCapture", inFrmt, NULL);

  if(avformat_find_stream_info(pFormatCtxInCam,NULL)<0)
    return -1;

  if(avformat_find_stream_info(pFormatCtxInScr,NULL)<0)
    return -1;

  video_stream_idx_cam = video_stream_idx_scr = -1;

  for(i=0; i<pFormatCtxInCam->nb_streams; i++)
    if(pFormatCtxInCam->streams[i]->codec->codec_type==AVMEDIA_TYPE_VIDEO)
      video_stream_idx_cam=i;

  for(i=0; i<pFormatCtxInScr->nb_streams; i++)
    if(pFormatCtxInScr->streams[i]->codec->codec_type==AVMEDIA_TYPE_VIDEO)
      video_stream_idx_scr=i;

  if(video_stream_idx_cam == -1 ||
      video_stream_idx_scr == -1)
    return -1; 

  pCodecCtxInCam = pFormatCtxInCam->streams[video_stream_idx_cam]->codec;

  pCodecCtxInScr = pFormatCtxInScr->streams[video_stream_idx_scr]->codec;

  pCodecInCam = avcodec_find_decoder(pCodecCtxInCam->codec_id);

  pCodecInScr = avcodec_find_decoder(pCodecCtxInScr->codec_id);

  if(pCodecInCam == NULL || pCodecInScr == NULL){
    fprintf(stderr,"decoder not fiund");
    exit(1);
  }

  if (avcodec_open2(pCodecCtxInCam, pCodecInCam, NULL) < 0) {
    av_log(NULL, AV_LOG_ERROR, "Cannot open video decoder for webcam\n");
    exit(1);
  }

  if (avcodec_open2(pCodecCtxInScr,pCodecInScr, NULL) < 0) {
    av_log(NULL, AV_LOG_ERROR, "Cannot open video decoder for screen\n");
    exit(1);
  }

  if(pCodecCtxInCam->time_base.num>1000 && pCodecCtxInCam->time_base.den==1)
    pCodecCtxInCam->time_base.den=1000;

  if(pCodecCtxInScr->time_base.num>1000 && pCodecCtxInScr->time_base.den==1)
    pCodecCtxInScr->time_base.den=1000;

  //////////////////////// encoder

  pOfmtOut = av_guess_format(NULL, filename,NULL);

  pFormatCtxOut = avformat_alloc_context();

  pOfmtOut->video_codec = AV_CODEC_ID_VP8;

  pFormatCtxOut->oformat = pOfmtOut;  

  snprintf(pFormatCtxOut->filename,sizeof(pFormatCtxOut->filename),"%s",filename);

  strmVdoOut = avformat_new_stream(pFormatCtxOut,NULL);
  if (!strmVdoOut ) {
    fprintf(stderr, "Could not alloc stream\n");
    exit(1);
  }

  pCodecCtxOut = strmVdoOut->codec;
  if (!pCodecCtxOut) {
    fprintf(stderr, "Could not allocate video codec context\n");
    exit(1);
  }

  pCodecCtxOut->codec_id = pOfmtOut->video_codec;
  pCodecCtxOut->codec_type = AVMEDIA_TYPE_VIDEO;

  pCodecCtxOut->bit_rate = 1200000;
  pCodecCtxOut->pix_fmt = AV_PIX_FMT_YUV420P;
  pCodecCtxOut->width = 1280;
  pCodecCtxOut->height = 720;
  pCodecCtxOut->time_base = (AVRational){1,10};

  if(pFormatCtxOut->oformat->flags & AVFMT_GLOBALHEADER)
    pCodecCtxOut->flags |= CODEC_FLAG_GLOBAL_HEADER;

  pCodecOut = avcodec_find_encoder(pCodecCtxOut->codec_id);
  if (!pCodecOut) {
    fprintf(stderr, "Codec not found\n");
    exit(1);
  }

  if (avcodec_open2(pCodecCtxOut, pCodecOut,NULL) < 0) {
    fprintf(stderr, "Could not open codec\n");
    exit(1);
  }

  if (avio_open(&pFormatCtxOut->pb, filename, AVIO_FLAG_WRITE) <0) {
    fprintf(stderr, "Could not open '%s'\n", filename);
    exit(1);
  }

  ret = avformat_write_header(pFormatCtxOut, NULL);
  if(ret < 0 ){
    fprintf(stderr, "Could not write header '%d'\n", ret);
    exit(1);
  }

  ////////////////// create frame 

  cam_frame = avcodec_alloc_frame();
  scr_frame = avcodec_alloc_frame();
  outFrame = avcodec_alloc_frame();
  filt_frame = avcodec_alloc_frame();

  if (!cam_frame || !scr_frame || !outFrame || !filt_frame) {
    fprintf(stderr, "Could not allocate video frame\n");
    exit(1);
  }
  cam_frame->format = pCodecCtxInCam->pix_fmt;
  cam_frame->width  = pCodecCtxInCam->width;
  cam_frame->height = pCodecCtxInCam->height;

  scr_frame->format = pCodecCtxInScr->pix_fmt;
  scr_frame->width  = pCodecCtxInScr->width;
  scr_frame->height = pCodecCtxInScr->height;

  outFrame->format = pCodecCtxOut->pix_fmt;
  outFrame->width  = pCodecCtxOut->width;
  outFrame->height = pCodecCtxOut->height;

  filt_frame->format = pCodecCtxInCam->pix_fmt;
  filt_frame->width  = pCodecCtxOut->width;
  filt_frame->height = pCodecCtxOut->height;

  ret = av_image_alloc(cam_frame->data, cam_frame->linesize, pCodecCtxInCam->width, pCodecCtxInCam->height,
      pCodecCtxInCam->pix_fmt, 32);
  if (ret < 0) {
    fprintf(stderr, "Could not allocate raw picture buffer input \n");
    exit(1);
  }

  ret = av_image_alloc(scr_frame->data, scr_frame->linesize, pCodecCtxInScr->width, pCodecCtxInScr->height,
      pCodecCtxInScr->pix_fmt, 32);
  if (ret < 0) {
    fprintf(stderr, "Could not allocate raw picture buffer input \n");
    exit(1);
  }

  ret = av_image_alloc(outFrame->data, outFrame->linesize, pCodecCtxOut->width, pCodecCtxOut->height,
      pCodecCtxOut->pix_fmt, 32);
  if (ret < 0) {
    fprintf(stderr, "Could not allocate raw picture buffer output outframe\n");
    exit(1);
  }

  ret = av_image_alloc(filt_frame->data, filt_frame->linesize, pCodecCtxOut->width, pCodecCtxOut->height,
      pCodecCtxInCam->pix_fmt, 32);
  if (ret < 0) {
    fprintf(stderr, "Could not allocate raw picture buffer output filtframe\n");
    exit(1);
  }
  //////////////////////////// software scaling

  ////////////////////////// fix pix fmt
  enum AVPixelFormat pix_fmts[] = { pCodecCtxInCam->pix_fmt,  AV_PIX_FMT_NONE};

  // to convert color space
  sws_ctx_scr = sws_getContext(pCodecCtxInScr->width, pCodecCtxInScr->height, pCodecCtxInScr->pix_fmt, pCodecCtxOut->width, pCodecCtxOut->height, 
      pCodecCtxOut->pix_fmt, SWS_BILINEAR, NULL, NULL, NULL);

  sws_ctx_cam = sws_getContext(pCodecCtxOut->width, pCodecCtxOut->height, AV_PIX_FMT_BGR24, pCodecCtxOut->width, pCodecCtxOut->height, 
      pCodecCtxOut->pix_fmt, SWS_BILINEAR, NULL, NULL, NULL);

  /////////////////////////// FILTER GRAPH

  filter_graph = avfilter_graph_alloc();

  snprintf(args_cam, sizeof(args_cam),
      "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d",
      pCodecCtxInCam->width, pCodecCtxInCam->height, pCodecCtxInCam->pix_fmt,
      pCodecCtxInCam->time_base.num, pCodecCtxInCam->time_base.den,
      pCodecCtxInCam->sample_aspect_ratio.num, pCodecCtxInCam->sample_aspect_ratio.den);

  ret = avfilter_graph_create_filter(&buffersrc_ctx_cam, buffersrc_cam, "in",
      args_cam, NULL, filter_graph);
  if (ret < 0) {
    av_log(NULL, AV_LOG_ERROR, "Cannot create buffer source\n");
    exit(ret);
  }

  buffersink_params = av_buffersink_params_alloc();
  buffersink_params->pixel_fmts = pix_fmts;

  ret = avfilter_graph_create_filter(&buffersink_ctx, buffersink, "out",
      NULL, buffersink_params, filter_graph);
  av_free(buffersink_params);
  if (ret < 0) {
    av_log(NULL, AV_LOG_ERROR, "Cannot create buffer sink\n");
    exit(ret);
  }

  outputs->name       = av_strdup("in");
  outputs->filter_ctx = buffersrc_ctx_cam;
  outputs->pad_idx    = 0;
  outputs->next       = NULL;

  inputs->name       = av_strdup("out");
  inputs->filter_ctx = buffersink_ctx;
  inputs->pad_idx    = 0;
  inputs->next       = NULL;

  if ((ret = avfilter_graph_parse(filter_graph, filters_descr_padding,
          &inputs, &outputs, NULL)) < 0){
    printf("error in graph parse");
    exit(ret);
  }

  if ((ret = avfilter_graph_config(filter_graph, NULL)) < 0){
    printf("error in graph config");
    exit(ret);
  }
  //////////////////////////////DUMP
  
  av_dump_format(pFormatCtxInCam,0,"cam",0);
  av_dump_format(pFormatCtxInScr,0,"scr",0);
  av_dump_format(pFormatCtxOut,0,filename,1);

  ////////////////////////// TRANSCODE

  i = 0;
  //  while(++i) {
  for(i=0;i<100;i++){
    av_init_packet(&packet);

    if(i>50)
      ret = av_read_frame(pFormatCtxInScr,&packet);
    else
      ret = av_read_frame(pFormatCtxInCam,&packet);

    if(ret < 0){
      fprintf(stderr,"Error reading frame\n");
      break;
    }

    if(packet.stream_index == video_stream_idx_cam){

      if(i>50)
        ret = avcodec_decode_video2(pCodecCtxInScr,scr_frame,&got_output,&packet);
      else
        ret = avcodec_decode_video2(pCodecCtxInCam,cam_frame,&got_output,&packet);

      if (got_output) {

        av_free_packet(&packet);
        av_init_packet(&packet);

        // pass through filter graph
        if(i>50){
          sws_scale(sws_ctx_scr, (const uint8_t * const*)scr_frame->data,scr_frame->linesize, 0, pCodecCtxInScr->height,outFrame->data,outFrame->linesize);
        }
        else{
          ret = av_buffersrc_add_frame_flags(buffersrc_ctx_cam, cam_frame, AV_BUFFERSRC_FLAG_KEEP_REF);

          if ( ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Error while feeding the filtergraph\n");
            break;
          }

          ret = av_buffersink_get_frame(buffersink_ctx, filt_frame);
          if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            break;
          if (ret < 0)
            break;

          outFrame = filt_frame;
//          sws_scale(sws_ctx_cam, (const uint8_t * const*)filt_frame->data,filt_frame->linesize, 0, filt_frame->height,outFrame->data,outFrame->linesize);
        }

        outFrame->pts=i;

        ret = avcodec_encode_video2(pCodecCtxOut, &packet, outFrame, &got_output);

        if (got_output) {

          if (pCodecCtxOut->coded_frame->pts != (0x8000000000000000LL)){
            packet.pts = av_rescale_q(pCodecCtxOut->coded_frame->pts, pCodecCtxOut->time_base, strmVdoOut->time_base);
          }

          if(pCodecCtxOut->coded_frame->key_frame)
            packet.flags |= AV_PKT_FLAG_KEY;

          if(av_interleaved_write_frame(pFormatCtxOut,&packet) < 0){
            fprintf(stderr,"error writing frame");
            exit(1);
          }
        }
      }
    }
    av_free_packet(&packet);
  }

  // all delayed frames 
  for (got_output = 1; got_output; i++) {
    printf("here");
    ret = avcodec_encode_video2(pCodecCtxOut, &packet, NULL, &got_output);
    if (ret < 0) {
      fprintf(stderr, "Error encoding frame\n");
      exit(1);
    }

    if (got_output) {
      printf("Write frame %3d (size=%5d)\n", i, packet.size);
      packet.pts = av_rescale_q(pCodecCtxOut->coded_frame->pts, pCodecCtxOut->time_base,strmVdoOut->time_base);

      if(pCodecCtxOut->coded_frame->key_frame)
        packet.flags |= AV_PKT_FLAG_KEY;

      ret = av_interleaved_write_frame(pFormatCtxOut,&packet);
      if(ret < 0 ){
        fprintf(stderr,"error writing interleaved frame");
        exit(1);
      }       
      av_free_packet(&packet);
    }
  }

  ret = av_write_trailer(pFormatCtxOut);
  if(ret < 0 ){
    fprintf(stderr,"error writing trailer");
    exit(1);   
  }

  avcodec_close(strmVdoOut->codec);
  avcodec_close(pCodecCtxInCam);
  avcodec_close(pCodecCtxInScr);
  avcodec_close(pCodecCtxOut);
  for(i = 0; i < pFormatCtxOut->nb_streams; i++) {
    av_freep(&pFormatCtxOut->streams[i]->codec);
    av_freep(&pFormatCtxOut->streams[i]);
  }
  avio_close(pFormatCtxOut->pb);
  av_free(pFormatCtxOut);
  av_free(pFormatCtxInScr);
  av_free(pFormatCtxInCam);
  av_dict_free(&inOptions);
}

