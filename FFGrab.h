#pragma once

#include <vector>
#include <string>
#include <iostream>
#include <fstream> 
#include "CImg.h"
#include <algorithm>

template<typename T>
void readVideo(char* filename, double scale, size_t &W, size_t &H, size_t &nb_frames, std::vector<T> &video, int max_frames);
template<typename T>
void writeVideo(const char* filename, double scale, size_t W, size_t H, size_t nb_frames, std::vector<T> &video, int codec = 2, bool swapRedBlue = false); // 1 = mpeg1
std::string codec_id_to_str(int id);
bool increment_file_number(std::string &path);

using namespace std;
using namespace cimg_library;

//#define DEBUG 0

#ifdef __cplusplus
 #define __STDC_CONSTANT_MACROS
 #ifdef _STDINT_H
  #undef _STDINT_H
 #endif
 #include "stdint.h"
#endif

#include <map>

extern "C" {
	#include "avbin.h"
	#include <libavformat/avformat.h>
    #include <libavcodec/avcodec.h>	
	#include <libavformat/avio.h>
	#include <libswscale/swscale.h>

	struct _AVbinFile {
	    AVFormatContext *context;
	    AVPacket *packet;
	};

	struct _AVbinStream {
		int type;
		AVFormatContext *format_context;
		AVCodecContext *codec_context;
		AVFrame *frame;
	};
}

void display_format(AVPixelFormat p);
bool file_exists(const char* filename);

class FFGrabber;

class Grabber
{
public:
	Grabber(FFGrabber* ffg, bool isAudio, AVbinStream* stream, bool trySeeking, double rate, int bytesPerWORD, AVbinStreamInfo info, AVbinTimestamp start_time);

	int Grab(AVbinPacket* packet);

	~Grabber();

	AVbinStream* stream;
	AVbinStreamInfo info;
	AVbinTimestamp start_time;

	std::vector<uint8_t*> frames;
	std::vector<unsigned int> frameBytes;
	std::vector<double> frameTimes;

	std::vector<unsigned int> frameNrs;

	unsigned int frameNr;
	unsigned int packetNr;
	bool done;
	bool isAudio;
	bool trySeeking;

	int bytesPerWORD;
	double rate;
	double startTime, stopTime;
	FFGrabber* ff;

};

typedef map<int,Grabber*> streammap;

class FFGrabber
{
public:
	FFGrabber();

	int build(const char* filename, char* format, bool disableVideo, bool disableAudio, bool tryseeking);
	int doCapture();

	int getVideoInfo(unsigned int id, int* width, int* height, double* rate, int* nrFramesCaptured, int* nrFramesTotal, double* totalDuration);
	int getAudioInfo(unsigned int id, int* nrChannels, double* rate, int* bits, int* nrFramesCaptured, int* nrFramesTotal, int* subtype, double* totalDuration);
	void getCaptureInfo(int* nrVideo, int* nrAudio);
	// data must be freed by caller
	int getVideoFrame(unsigned int id, unsigned int frameNr, uint8_t** data, unsigned int* nrBytes, double* time);
	// data must be freed by caller
	int getAudioFrame(unsigned int id, unsigned int frameNr, uint8_t** data, unsigned int* nrBytes, double* time);
	void setFrames(unsigned int* frameNrs, int nrFrames);
	void setTime(double startTime, double stopTime);
	void disableVideo();
	void disableAudio();
	void cleanUp(); // must be called at the end, in order to render anything afterward.

#ifdef MATLAB_MEX_FILE
	void setMatlabCommand(char * matlabCommand);
	void setMatlabCommandHandle(mxArray * matlabCommandHandle);
	void runMatlabCommand(Grabber* G);
#endif
private:
	streammap streams;
	std::vector<Grabber*> videos;
	std::vector<Grabber*> audios;

	AVbinFile* file;
	AVbinFileInfo fileinfo;

	bool stopForced;
	bool tryseeking;
	std::vector<unsigned int> frameNrs;
	double startTime, stopTime;

	char* filename;
	struct stat filestat;
public:
	map<unsigned int, double> keyframes;
	unsigned int startDecodingAt;


#ifdef MATLAB_MEX_FILE
	char* matlabCommand;
	mxArray* matlabCommandHandle;
	mxArray* prhs[6];
#endif
};


template<typename T>
class VideoStreamer {
public:
	VideoStreamer() {};
	virtual bool get_next_frame(T* frame) = 0;

	int W, H, nbframes;
	int cur_frame;
};



template<typename T>
class VideoStreamerImage: public VideoStreamer<T> {
public:

	VideoStreamerImage(const std::string &filename) {
		cur_frame = 0;
		nbframes = 10000;
		cimg_library::CImg<unsigned char> cimg(filename.c_str());		
		W = cimg.width();
		H = cimg.height();
		this->filename = filename;
	}

	bool get_next_frame(T* frame) {

		cimg_library::CImg<unsigned char> cimg(filename.c_str());
		for (int j=0; j<W*H; j++) {
			frame[j*3 + 0] = cimg.data()[j]/255.;
			frame[j*3 + 1] = cimg.data()[j+W*H]/255.;
			frame[j*3 + 2] = cimg.data()[j+W*H*2]/255.;
		}
		cur_frame++;
		bool success = increment_file_number(filename);		
		success &= file_exists(filename.c_str());
		return success;
	}
	std::string filename;
};

template<typename T>
class VideoStreamerYUV: public VideoStreamer<T> {
public:
	VideoStreamerYUV(const std::string &filename, int W, int H) {
		cur_frame = 0;
		nbframes = 10000;
		this->W = W;
		this->H = H;		

		FFG = cimg::fopen(filename.c_str(), "rb");
	}


	bool get_next_frame(T* frame) {

		CImg<unsigned char> tmp(W, H, 1, 3), UV(W/2, H/2, 1, 2);
		tmp.fill(0);
		bool stopflag = false;

		// *TRY* to read the luminance part, do not replace by cimg::fread !
		int err = (int)std::fread((void*)(tmp._data), 1, (size_t)(tmp._width*tmp._height), FFG);
		if (err!=(int)(tmp._width*tmp._height)) {
			stopflag = true;
			if (err>0) {
				std::cout<<"size bug in YUV luminance"<<std::endl; 
			}
			
		} else {
			UV.fill(0);
			// *TRY* to read the luminance part, do not replace by cimg::fread !
			err = (int)std::fread((void*)(UV._data), 1, (size_t)(UV.size()), FFG);
			if (err!=(int)(UV.size())) {
				stopflag = true;
				if (err>0) {
					std::cout<<"size bug in YUV chrominance"<<std::endl;
				}
			} else {
				cimg_forXY(UV, x, y) {
					const int x2 = x*2, y2 = y*2;
					tmp(x2, y2, 1) = tmp(x2+1, y2, 1) = tmp(x2, y2+1, 1) = tmp(x2+1, y2+1, 1) = UV(x, y, 0);
					tmp(x2, y2, 2) = tmp(x2+1, y2, 2) = tmp(x2, y2+1, 2) = tmp(x2+1, y2+1, 2) = UV(x, y, 1);
				}
				tmp.YCbCrtoRGB();

				for (int j=0; j<W*H; j++) {
					frame[j*3+0] = tmp[j]/255.;
					frame[j*3+1] = tmp[j+W*H]/255.;
					frame[j*3+2] = tmp[j+2*W*H]/255.;
				}
			}
		}		
		cur_frame++;
		return !stopflag;
	}

	~VideoStreamerYUV() {	
		cimg::fclose(FFG);
		
	}

	std::FILE* FFG;

};

template<typename T>
class VideoStreamerMPG: public VideoStreamer<T> {
public:
	VideoStreamerMPG(const std::string &filename) { // each filename is a video
		cur_frame = 0;
		int max_frames = -1;
		FFG = new FFGrabber();
		printf("%s\n", filename.c_str());
		FFG->build(filename.c_str(), NULL, false, true, true);
		double test1;
		FFG->doCapture();
		int nrVideoStreams, nrAudioStreams;
		FFG->getCaptureInfo(&nrVideoStreams, &nrAudioStreams);

		double rate;
		int nframes_total;
		double duration;
		int wdummy, hdummy, fdummy;
		FFG->getVideoInfo(0, &wdummy, &hdummy, &rate, &fdummy, &nframes_total, &duration);
		W = wdummy;
		H = hdummy;
		if (max_frames>0)
			nbframes = min(fdummy, max_frames);
		else
			nbframes = fdummy;
		std::cout << " frames captured : "<<nbframes<<std::endl;
		std::cout << " frames total : "<<nframes_total<<std::endl;
		std::cout << " duration : "<<duration<<std::endl;
		std::cout << " framerate : "<<rate<<std::endl;

	}

	bool get_next_frame(T* frame) { // frames of size W*H*3

		unsigned char* tmp;
		unsigned int nrb;
		double time;
		FFG->getVideoFrame(0, cur_frame, &tmp, &nrb, &time);
		for (int k=0; k<W*H*3; k++) {
			frame[k] = (T)tmp[k] / 255.;
		}
		delete[] tmp;
		
		cur_frame++;
		return (cur_frame<nbframes);
	}

	~VideoStreamerMPG() {
		delete FFG[i];
	}

	FFGrabber* FFG;
};



template<typename T>
void readVideo(char* filename, double scale, size_t &W, size_t &H, size_t &nb_frames, std::vector<T> &video, int max_frames)
{

	FFGrabber* FFG = new FFGrabber();
	printf("%s\n",filename);
	FFG->build(filename,NULL,false,true,true);

	double test1;
	FFG->doCapture();
	int nrVideoStreams, nrAudioStreams;
	FFG->getCaptureInfo(&nrVideoStreams, &nrAudioStreams);

	printf("there are %d video streams, and %d audio.\n",nrVideoStreams,nrAudioStreams);

	for (int i=0; i<nrVideoStreams; i++) {
		double rate;
		int nframes_total;
		double duration;
		int wdummy, hdummy, fdummy;
		FFG->getVideoInfo(i, &wdummy, &hdummy, &rate, &fdummy, &nframes_total, &duration);
		W = wdummy;
		H = hdummy;
		if (max_frames>0)
			nb_frames = std::min(fdummy, max_frames);
		else 
			nb_frames = fdummy;
		std::cout << " frames captured : "<<nb_frames<<std::endl;
		std::cout << " frames total : "<<nframes_total<<std::endl;
		std::cout << " duration : "<<duration<<std::endl;
		std::cout << " framerate : "<<rate<<std::endl;

		unsigned char* tmp;
		video.resize(W*H*nb_frames*3);
		for (int j=0; j<nb_frames; j++) {
			
			unsigned int nrb;
			double time;
			FFG->getVideoFrame(i,j, &tmp, &nrb, &time);
			for (int k=0; k<W*H*3; k++) {
				video[j*W*H*3+k] = (T)tmp[k];
			}
			delete[] tmp;
		}
	}

	delete FFG;
}


template<typename T>
void stretch(const T* src_img, int Wsrc, int Hsrc, T* dst_img, int Wdst, int Hdst) {

	std::vector<T> deinterleaved1(Wsrc*Hsrc*3);
	for (int i=0; i<Wsrc*Hsrc; i++) {
		deinterleaved1[i] = src_img[i*3+0];
		deinterleaved1[i+Wsrc*Hsrc] = src_img[i*3+1];
		deinterleaved1[i+2*Wsrc*Hsrc] = src_img[i*3+2];
	}
	cimg_library::CImg<T> cimg1(&deinterleaved1[0], Wsrc, Hsrc, 1, 3);
	cimg1.resize(Wdst, Hdst, -100, -100, 3);
	for (int i=0; i<Wdst*Hdst; i++) {
		dst_img[i*3+0] = cimg1.data()[i];
		dst_img[i*3+1] = cimg1.data()[i+Wdst*Hdst];
		dst_img[i*3+2] = cimg1.data()[i+2*Wdst*Hdst];
	}
}

template<typename T>
void resize(std::vector<T> &video, size_t &W, size_t &H, int nb_frames, int* avail_W, int* avail_H, bool transparent=false) {

	std::cout<<"entered here"<<std::endl;
	int ki=0;
	size_t closest_H = 0, closest_W = 0;
	while(avail_W[ki]!=0) {
		if (avail_W[ki]==W && avail_H[ki]==H) return;
					
		closest_W = avail_W[ki];
		closest_H = avail_H[ki];
		if (avail_W[ki]>=W) {
			break;
		}
		ki++;
	}
	size_t resized_W = closest_W;
	size_t resized_H = (closest_W*H)/W;
	size_t offsetX = 0;
	size_t offsetY = (closest_H-resized_H)/2;

	if ( resized_H > closest_H) {
		ki=0;
		while(avail_W[ki]!=0) {	
			closest_W = avail_W[ki];
			closest_H = avail_H[ki];
			if (avail_H[ki]>=H) {
				break;
			}
			ki++;
		}
		resized_W = (closest_H*W)/H;
		resized_H = closest_H;
		offsetX = (closest_W-resized_W)/2;
		offsetY = 0;
	}

	if (!transparent) {
		std::vector<T> tmpvideo(video);
		video.resize(closest_W*closest_H * 3 * nb_frames);
		std::fill(video.begin(), video.end(), 0);

		std::vector<T> tmpimg(resized_W*resized_H * 3);
		for (size_t i = 0; i < nb_frames; i++) {
			stretch(&tmpvideo[i*W*H * 3], W, H, &tmpimg[0], resized_W, resized_H);
			T* v = &video[i*closest_W*closest_H * 3 + offsetX * 3];
			T* im = &tmpimg[0];
			for (size_t j = 0; j < resized_H; j++) {
				T* v = &video[i*closest_W*closest_H * 3 + (j + offsetY)*closest_W * 3 + offsetX * 3];
				for (size_t k = 0; k < resized_W * 3; k++) {
					*v = *im;
					v++; im++;
				}
			}
		}
	}

	W = closest_W;
	H = closest_H;
	std::cout <<"WH: "<<W<<" "<<H<<std::endl;

}


template<typename T>
void writeVideo(const char* filename, double scaleValues, size_t W, size_t H, size_t nb_frames, std::vector<T> &video, int codec_id, bool swapRedBlue)
{

	/*if (codec_id==4) {		
		writeVideo2(filename, W, H, nb_frames, video, codec_id);
		return;
	}*/
	 std::cout<<"using codec "<<codec_id<<std::endl;

     avcodec_register_all();
      av_register_all();


	int avail_W[255] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
	int avail_H[255] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
	switch (codec_id) {
	case CODEC_ID_H261: // 176x144, 352x288
		avail_W[0]= 171; avail_H[0]= 144;
		avail_W[1]= 352; avail_H[1]= 288;
		resize(video, W, H, nb_frames, avail_W, avail_H);
		break;
	case AV_CODEC_ID_H263:  //128x96, 176x144,  352x288, 704x576, and 1408x1152
		avail_W[0]= 128; avail_H[0]= 96;
		avail_W[1]= 176; avail_H[1]= 144;
		avail_W[2]= 352; avail_H[2]= 288;
		avail_W[3]= 704; avail_H[3]= 576;
		avail_W[4]= 1408; avail_H[4]= 1152;
		resize(video, W, H, nb_frames, avail_W, avail_H);
		break;
	}
	/*int avail_W[255] = {640,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
	int avail_H[255] = {360,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
	resize(video, W, H, nb_frames, avail_W, avail_H);*/
	

      const int
        frame_dimx = W,
        frame_dimy = H,
        frame_dimv = 3;

      PixelFormat dest_pxl_fmt = AV_PIX_FMT_YUV420P;
      PixelFormat src_pxl_fmt  = (frame_dimv==3)?AV_PIX_FMT_BGR24:AV_PIX_FMT_GRAY8;

      int sws_flags = SWS_FAST_BILINEAR; // Interpolation method (keeping same size images for now).
      AVOutputFormat *fmt = 0;
#if defined(AV_VERSION_INT)
#if LIBAVFORMAT_VERSION_INT<AV_VERSION_INT(52,45,0)
      fmt = guess_format(0,filename,0);
      if (!fmt) fmt = guess_format("mpeg",0,0); // Default format "mpeg".
#else
      //fmt = av_guess_format(0,filename,0);
    fmt = NULL;    
	for (int i=2; i>=-2; i--) {
		while ((fmt = av_oformat_next(fmt))) {			
			if (avformat_query_codec(fmt, CodecID(codec_id), i)==1) { // i==2 : FF_COMPLIANCE_VERY_STRICT
				break;
			}
		}    
		if (fmt)
			break;
	}
      if (!fmt) fmt = av_guess_format("mpeg",0,0); // Default format "mpeg".
	  fmt->video_codec = CodecID(codec_id);	
	  
#endif
#else
      fmt = guess_format(0,filename,0);
      if (!fmt) fmt = guess_format("mpeg",0,0); // Default format "mpeg".
#endif

      if (!fmt) {
		  std::cout<< "save_ffmpeg() : Unable to determine codec for file "<< filename <<std::endl;
		  return;
	  }


      AVFormatContext *oc = 0;
#if defined(AV_VERSION_INT)
#if LIBAVFORMAT_VERSION_INT<AV_VERSION_INT(52,36,0)
      oc = av_alloc_format_context();
#else
      oc = avformat_alloc_context();
#endif
#else
      oc = av_alloc_format_context();
#endif
      if (!oc) { // Failed to allocate format context.
		  std::cout<< "save_ffmpeg() : Failed to allocate FFMPEG structure for format context, for file "<< filename <<std::endl;
		  return;
	  }

      AVCodec *codec = 0;
      AVFrame *picture = 0;
      AVFrame *tmp_pict = 0;
      oc->oformat = fmt;
      std::sprintf(oc->filename,"%s",filename);

      // Add video stream.
      int stream_index = 0;
      AVStream *video_str = 0;
      if (fmt->video_codec!=CODEC_ID_NONE) {
        video_str = av_new_stream(oc,stream_index);
        if (!video_str) { // Failed to allocate stream.
          av_free(oc);
		  std::cout<< "save_ffmpeg() : Failed to allocate FFMPEG structure for video stream, for file "<< filename <<std::endl;
		  return;
        }
      } else { // No codec identified.
        av_free(oc);
		std::cout<< "save_ffmpeg() : Failed to identify proper codec, for file "<< filename <<std::endl;
		return;
      }

      AVCodecContext *c = video_str->codec;
      c->codec_id = fmt->video_codec;
      c->codec_type = AVMEDIA_TYPE_VIDEO;
      c->bit_rate = 1024*8000;// 1024*bitrate;
      c->width = W;
      c->height = H;
      c->time_base.num = 1;
      c->time_base.den = 25;
	  //c->sample_aspect_ratio.num=0;
	  //c->sample_aspect_ratio.den=0;
      c->gop_size = 12;
      c->pix_fmt = dest_pxl_fmt;
      if (c->codec_id==AV_CODEC_ID_MPEG2VIDEO) c->max_b_frames = 2;
      if (c->codec_id==AV_CODEC_ID_MPEG1VIDEO) c->mb_decision = 2;

	  c->qmin = 2;
	  c->qmax = 10;
	  c->flags|=CODEC_FLAG_GLOBAL_HEADER;

	  if (c->codec_id==AV_CODEC_ID_H264) {		  
		  c->bit_rate_tolerance = 0;
		  c->rc_max_rate = 0;
		  c->rc_buffer_size = 0;

		  c->max_b_frames = 0;
		  c->b_frame_strategy = 1;
		  c->coder_type = 1;
		  c->me_cmp = 1;
		  c->me_range = 16;
		  c->scenechange_threshold = 1;
		  c->flags |= CODEC_FLAG_LOOP_FILTER;
		  c->me_method = ME_HEX;
		  c->me_subpel_quality = 5;
		  c->i_quant_factor = 0.71;
		  c->qcompress = 0.6;
		  c->max_qdiff = 4;
		  c->prediction_method = 1;
		  c->flags2 |= CODEC_FLAG2_SKIP_RD;
	  }


	  
      /*if (av_set_parameters(oc,0)<0) { // Parameters not properly set.
        av_free(oc);
		std::cout<< "save_ffmpeg() : Invalid parameters set for avcodec, for file "<< filename <<std::endl;
		return;
      }*/

      // Open codecs and alloc buffers.
      codec = avcodec_find_encoder(c->codec_id);
      if (!codec) { // Failed to find codec.
        av_free(oc);
        std::cout<< "save_ffmpeg() : No valid codec found for file "<< filename <<std::endl;
		return;
      }

	  int ki=0;
	AVPixelFormat list[255];
	bool found = false;
	while (codec->pix_fmts[ki]!=AV_PIX_FMT_NONE) {
		list[ki] = codec->pix_fmts[ki];		
		if (list[ki]==AV_PIX_FMT_YUV420P) {
			found = true;
		}
		ki++;
	}
	list[ki] = codec->pix_fmts[ki];
	if (found) {
		c->pix_fmt = AV_PIX_FMT_YUV420P;		
	} else {
		c->pix_fmt = avcodec_find_best_pix_fmt_of_list(list, AV_PIX_FMT_RGB24, false, NULL); // PIX_FMT_YUV420P;// | AV_PIX_FMT_RGB24;
	}
	if (c->codec_id == AV_CODEC_ID_TIFF)
		c->pix_fmt = AV_PIX_FMT_RGB24;

	display_format(c->pix_fmt);

	//c->pix_fmt = AV_PIX_FMT_RGB24;

	if (codec->supported_framerates) {
		double bestfr = 1000;
		ki=0;
		while (codec->supported_framerates[ki].den!=0) {			
			double curfr = codec->supported_framerates[ki].den/(double)codec->supported_framerates[ki].num;
			if (abs(curfr-1./25.)<abs(bestfr-1./25.)) {
				bestfr = curfr;
				c->time_base.num = codec->supported_framerates[ki].den; // yes, there is a bug in ffmpeg
				c->time_base.den = codec->supported_framerates[ki].num;
			}
			ki++;
		}
	}
	else {
		c->time_base.num = 1;
		c->time_base.den = 25;
	}
	std::cout<<c->time_base.num<<"  "<<c->time_base.den<<std::endl;

      if (avcodec_open2(c,codec,NULL)<0) { // Failed to open codec. 
        std::cout<< "save_ffmpeg() : Failed to open codec for file " << filename <<std::endl;
		return;
	  }

      tmp_pict = avcodec_alloc_frame();
      if (!tmp_pict) { // Failed to allocate memory for tmp_pict frame.
        avcodec_close(video_str->codec);
        av_free(oc);
        std::cout<< "save_ffmpeg() : Failed to allocate memory for file " << filename <<std::endl;
        return;
      }
      tmp_pict->linesize[0] = (src_pxl_fmt==PIX_FMT_BGR24)?3*frame_dimx:frame_dimx;
      tmp_pict->type = FF_BUFFER_TYPE_USER;
      int tmp_size = avpicture_get_size(src_pxl_fmt,frame_dimx,frame_dimy);
      uint8_t *tmp_buffer = (uint8_t*)av_malloc(tmp_size);
      if (!tmp_buffer) { // Failed to allocate memory for tmp buffer.
        av_free(tmp_pict);
        avcodec_close(video_str->codec);
        av_free(oc);
        std::cout<< "save_ffmpeg() : Failed to allocate memory for file " << filename <<std::endl;
		return;
      }

      // Associate buffer with tmp_pict.
      avpicture_fill((AVPicture*)tmp_pict,tmp_buffer,src_pxl_fmt,frame_dimx,frame_dimy);
      picture = avcodec_alloc_frame();
      if (!picture) { // Failed to allocate picture frame.
        av_free(tmp_pict->data[0]);
        av_free(tmp_pict);
        avcodec_close(video_str->codec);
        av_free(oc);
        std::cout<< "save_ffmpeg() : Failed to allocate memory for file " << filename <<std::endl;
		return;
      }

      int size = avpicture_get_size(c->pix_fmt,frame_dimx,frame_dimy);
      uint8_t *buffer = (uint8_t*)av_malloc(size);
      if (!buffer) { // Failed to allocate picture frame buffer.
        av_free(picture);
        av_free(tmp_pict->data[0]);
        av_free(tmp_pict);
        avcodec_close(video_str->codec);
        av_free(oc);
        std::cout<< "save_ffmpeg() : Failed to allocate memory for file " << filename <<std::endl;
		return;
      }

      // Associate the buffer with picture.
      avpicture_fill((AVPicture*)picture,buffer,c->pix_fmt,frame_dimx,frame_dimy);

      // Open file.
      if (!(fmt->flags&AVFMT_NOFILE)) {
        if (avio_open(&oc->pb,filename, AVIO_FLAG_WRITE)<0) {
			std::cout<< "save_ffmpeg() : Failed to open file " << filename <<std::endl;
			return;
		}
      }


      if (avformat_write_header(oc, NULL)<0) {
		std::cout<< "save_ffmpeg() : Failed to write header in file " << filename <<std::endl;
        return;
	  }

      double video_pts;
      SwsContext *img_convert_context = 0;
      img_convert_context = sws_getContext(frame_dimx,frame_dimy,src_pxl_fmt,
                                           c->width,c->height,c->pix_fmt,sws_flags,0,0,0);
      if (!img_convert_context) { // Failed to get swscale context.
        // if (!(fmt->flags & AVFMT_NOFILE)) url_fclose(&oc->pb);
        av_free(picture->data);
        av_free(picture);
        av_free(tmp_pict->data[0]);
        av_free(tmp_pict);
        avcodec_close(video_str->codec);
        av_free(oc);
		std::cout<< "save_ffmpeg() : Failed to get conversion context for file " << filename <<std::endl;
        return;
      }
      int ret = 0, out_size;
      uint8_t *video_outbuf = 0;
      int video_outbuf_size = 1000000;
      video_outbuf = (uint8_t*)av_malloc(video_outbuf_size);
      if (!video_outbuf) {
        // if (!(fmt->flags & AVFMT_NOFILE)) url_fclose(&oc->pb);
        av_free(picture->data);
        av_free(picture);
        av_free(tmp_pict->data[0]);
        av_free(tmp_pict);
        avcodec_close(video_str->codec);
        av_free(oc);
		std::cout<< "save_ffmpeg() : Failed to allocate memory for file " << filename <<std::endl;
		return;
      }

	  std::vector<unsigned char> frameuc(W*H*3);
      // Loop through each desired image in list.
      for (size_t i = 0; i<nb_frames; ++i) {
        /*CImg<uint8_t> currentIm = _data[i], red, green, blue, gray;
        if (src_pxl_fmt==PIX_FMT_RGB24) {
          red = currentIm.get_shared_channel(0);
          green = currentIm.get_shared_channel(1);
          blue = currentIm.get_shared_channel(2);
          cimg_forXY(currentIm,X,Y) { // Assign pizel values to data buffer in interlaced RGBRGB ... format.
            tmp_pict->data[0][Y*tmp_pict->linesize[0] + 3*X] = red(X,Y);
            tmp_pict->data[0][Y*tmp_pict->linesize[0] + 3*X + 1] = green(X,Y);
            tmp_pict->data[0][Y*tmp_pict->linesize[0] + 3*X + 2] = blue(X,Y);
          }
        } else {
          gray = currentIm.get_shared_channel(0);
          cimg_forXY(currentIm,X,Y) tmp_pict->data[0][Y*tmp_pict->linesize[0] + X] = gray(X,Y);
        }*/
		  for (int j=0; j<W*H*3; j++) {
			  frameuc[j] = (unsigned char) std::min(255., std::max(0., video[i*W*H*3+j]*scaleValues));
		  }
		  if (swapRedBlue) {
			  for (int j=0; j<W*H; j++) {
				  std::swap(frameuc[j*3], frameuc[j*3+2]);
			  }
		  }
		 tmp_pict->data[0] = &frameuc[0];

        if (video_str) video_pts = (video_str->pts.val * video_str->time_base.num)/(video_str->time_base.den);
        else video_pts = 0.0;
        if (!video_str) break;
			
        if (sws_scale(img_convert_context,tmp_pict->data,tmp_pict->linesize,0,c->height,picture->data,picture->linesize)<0) break;
        out_size = avcodec_encode_video(c,video_outbuf,video_outbuf_size,picture);
        if (out_size>0) {
          AVPacket pkt;
          av_init_packet(&pkt);
          pkt.pts = av_rescale_q(c->coded_frame->pts,c->time_base,video_str->time_base);
		  
          if (c->coded_frame->key_frame) pkt.flags|=AV_PKT_FLAG_KEY;
          pkt.stream_index = video_str->index;
          pkt.data = video_outbuf;
          pkt.size = out_size;
          ret = av_write_frame(oc,&pkt);
        } else if (out_size<0) break;
        if (ret) break; // Error occured in writing frame.
      }

      // Close codec.
      if (video_str) {
        avcodec_close(video_str->codec);
       /* av_free(picture->data[0]);
		if (picture)
			av_free(picture);*/
        //av_free(tmp_pict->data[0]);
		/*if (tmp_pict)
			av_free(tmp_pict);*/
      }
      if (av_write_trailer(oc)<0) {
		std::cout<< "save_ffmpeg() : Failed to write trailer for file " << filename <<std::endl;
		return;
	  }

      /*av_freep(&oc->streams[stream_index]->codec);
      av_freep(&oc->streams[stream_index]);*/
      if (!(fmt->flags&AVFMT_NOFILE)) {
        /*if (url_fclose(oc->pb)<0)
          throw CImgIOException(_cimglist_instance
                                "save_ffmpeg() : File '%s', failed to close file.",
                                cimglist_instance,
                                filename);
        */
      }
      av_free(oc);
	  if (video_outbuf)
		 av_free(video_outbuf);
}

template<typename T>
class VideoRecorder {
public:
	VideoRecorder() {};
	virtual void addFrame(const T* frame) = 0;
	virtual void finalize_video() = 0;
};

template<typename T>
class VideoRecorderImage: public VideoRecorder<T> {
public:

	VideoRecorderImage(const char* filename, size_t W, size_t H) {
		this->W = W;
		this->H = H;
		this->filename = std::string(filename);
	}
	void addFrame(const T* frame) {

		std::vector<unsigned char> deinterleaved(W*H * 3);
		for (int i = 0; i < W*H; i++) {
			deinterleaved[i] = min(255., max(0., frame[i*3]*255.));
			deinterleaved[i + W*H] = min(255., max(0., frame[i*3+1]*255.));
			deinterleaved[i + 2 * W*H] = min(255., max(0., frame[i*3+2]*255.));
		}
		cimg_library::CImg<unsigned char> cimg(&deinterleaved[0], W, H, 1, 3);
		cimg.save(filename.c_str());
		increment_file_number(filename);
	}
	void finalize_video() {

	}
	~VideoRecorderImage() {
	}

	size_t W, H;
	std::string filename;
};

template<typename T>
class VideoRecorderYUV: public VideoRecorder<T> {
public:

	VideoRecorderYUV(const char* filename, size_t W, size_t H) {
		this->W = W;
		this->H = H;
		this->filename = std::string(filename);
		f = cimg::fopen(filename, "wb");
		fclose(f);
	}
	void addFrame(const T* frame) {

		CImg<unsigned char> YCbCr(W, H, 1, 3);
		for (int i=0; i<W*H; i++) {
			YCbCr[i] = min(255., max(0., frame[i*3]*255.));
			YCbCr[i+W*H] = min(255., max(0., frame[i*3+1]*255.));
			YCbCr[i+W*H*2] = min(255., max(0., frame[i*3+2]*255.));
		}
		YCbCr.RGBtoYCbCr();
		f = cimg::fopen(filename.c_str(), "a+b");
		cimg::fwrite(YCbCr._data, YCbCr._width*YCbCr._height, f);
		cimg::fwrite(YCbCr.get_resize(YCbCr._width/2, YCbCr._height/2, 1, 3, 3).data(0, 0, 0, 1), YCbCr._width*YCbCr._height/2, f);
		cimg::fclose(f);
	}
	void finalize_video() {
		
	}
	~VideoRecorderYUV() {
	}

	size_t W, H;
	std::FILE* f;
	std::string filename;
};


template<typename T>
class VideoRecorderMPG: public VideoRecorder<T> {
public:


	int nb_recorded_frames;
	int frame_dimx, frame_dimy, frame_dimv;
	size_t initial_W, initial_H, new_W, new_H;

	AVCodecContext *c;
	PixelFormat dest_pxl_fmt, src_pxl_fmt;
	int sws_flags;
	AVOutputFormat *fmt;
	AVCodec *codec;
	AVFrame *picture;
	AVFrame *tmp_pict;
	int stream_index;
	AVStream *video_str;
	int codec_id;
	AVFormatContext *oc;

	VideoRecorderMPG(const char* filename, size_t W, size_t H, int pcodec_id = 2) {
		codec_id = pcodec_id;
		nb_recorded_frames = 0;
		std::cout << "using codec " << codec_id << std::endl;

		avcodec_register_all();
		av_register_all();

		initial_W = W;
		initial_H = H;
		new_W = W;
		new_H = H;

		std::vector<unsigned char> tmpvid(1);
		int avail_W[255] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
		int avail_H[255] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
		switch (codec_id) {
		case CODEC_ID_H261: // 176x144, 352x288
			avail_W[0] = 171; avail_H[0] = 144;
			avail_W[1] = 352; avail_H[1] = 288;
			resize(tmpvid, new_W, new_H, 1, avail_W, avail_H, true);
			break;
		case AV_CODEC_ID_H263:  //128x96, 176x144,  352x288, 704x576, and 1408x1152
			avail_W[0] = 128; avail_H[0] = 96;
			avail_W[1] = 176; avail_H[1] = 144;
			avail_W[2] = 352; avail_H[2] = 288;
			avail_W[3] = 704; avail_H[3] = 576;
			avail_W[4] = 1408; avail_H[4] = 1152;
			resize(tmpvid, new_W, new_H, 1, avail_W, avail_H, true);
			break;
		}

		frame_dimx = new_W;
		frame_dimy = new_H;
		frame_dimv = 3;

		dest_pxl_fmt = AV_PIX_FMT_YUV420P;
		src_pxl_fmt = (frame_dimv == 3) ? AV_PIX_FMT_BGR24 : AV_PIX_FMT_GRAY8;

		sws_flags = SWS_FAST_BILINEAR; // Interpolation method (keeping same size images for now).
		fmt = 0;
#if defined(AV_VERSION_INT)
#if LIBAVFORMAT_VERSION_INT<AV_VERSION_INT(52,45,0)
		fmt = guess_format(0, filename, 0);
		if (!fmt) fmt = guess_format("mpeg", 0, 0); // Default format "mpeg".
#else
		//fmt = av_guess_format(0,filename,0);
		fmt = NULL;
		for (int i = 2; i >= -2; i--) {
			while ((fmt = av_oformat_next(fmt))) {
				if (avformat_query_codec(fmt, CodecID(codec_id), i) == 1) { // i==2 : FF_COMPLIANCE_VERY_STRICT
					break;
				}
			}
			if (fmt)
				break;
		}
		if (!fmt) fmt = av_guess_format("mpeg", 0, 0); // Default format "mpeg".
		fmt->video_codec = CodecID(codec_id);

#endif
#else
		fmt = guess_format(0, filename, 0);
		if (!fmt) fmt = guess_format("mpeg", 0, 0); // Default format "mpeg".
#endif

		if (!fmt) {
			std::cout << "save_ffmpeg() : Unable to determine codec for file " << filename << std::endl;
			return;
		}


		oc = 0;
#if defined(AV_VERSION_INT)
#if LIBAVFORMAT_VERSION_INT<AV_VERSION_INT(52,36,0)
		oc = av_alloc_format_context();
#else
		oc = avformat_alloc_context();
#endif
#else
		oc = av_alloc_format_context();
#endif
		if (!oc) { // Failed to allocate format context.
			std::cout << "save_ffmpeg() : Failed to allocate FFMPEG structure for format context, for file " << filename << std::endl;
			return;
		}

		codec = 0;
		picture = 0;
		tmp_pict = 0;
		oc->oformat = fmt;
		std::sprintf(oc->filename, "%s", filename);

		// Add video stream.
		stream_index = 0;
		video_str = 0;
		if (fmt->video_codec != CODEC_ID_NONE) {
			video_str = av_new_stream(oc, stream_index);
			if (!video_str) { // Failed to allocate stream.
				av_free(oc);
				std::cout << "save_ffmpeg() : Failed to allocate FFMPEG structure for video stream, for file " << filename << std::endl;
				return;
			}
		}
		else { // No codec identified.
			av_free(oc);
			std::cout << "save_ffmpeg() : Failed to identify proper codec, for file " << filename << std::endl;
			return;
		}

		c = video_str->codec;
		c->codec_id = fmt->video_codec;
		c->codec_type = AVMEDIA_TYPE_VIDEO;
		c->bit_rate = 1024 * 8000;// 1024*bitrate;
		c->width = new_W;
		c->height = new_H;
		c->time_base.num = 1;
		c->time_base.den = 25;
		//c->sample_aspect_ratio.num=0;
		//c->sample_aspect_ratio.den=0;
		c->gop_size = 12;
		c->pix_fmt = dest_pxl_fmt;
		if (c->codec_id == AV_CODEC_ID_MPEG2VIDEO) c->max_b_frames = 2;
		if (c->codec_id == AV_CODEC_ID_MPEG1VIDEO) c->mb_decision = 2;

		c->qmin = 2;
		c->qmax = 10;
		c->flags |= CODEC_FLAG_GLOBAL_HEADER;

		if (c->codec_id == AV_CODEC_ID_H264) {
			c->bit_rate_tolerance = 0;
			c->rc_max_rate = 0;
			c->rc_buffer_size = 0;

			c->max_b_frames = 0;
			c->b_frame_strategy = 1;
			c->coder_type = 1;
			c->me_cmp = 1;
			c->me_range = 16;
			c->scenechange_threshold = 1;
			c->flags |= CODEC_FLAG_LOOP_FILTER;
			c->me_method = ME_HEX;
			c->me_subpel_quality = 5;
			c->i_quant_factor = 0.71;
			c->qcompress = 0.6;
			c->max_qdiff = 4;
			c->prediction_method = 1;
			c->flags2 |= CODEC_FLAG2_SKIP_RD;
		}



		/*if (av_set_parameters(oc,0)<0) { // Parameters not properly set.
		av_free(oc);
		std::cout<< "save_ffmpeg() : Invalid parameters set for avcodec, for file "<< filename <<std::endl;
		return;
		}*/

		// Open codecs and alloc buffers.
		codec = avcodec_find_encoder(c->codec_id);
		if (!codec) { // Failed to find codec.
			av_free(oc);
			std::cout << "save_ffmpeg() : No valid codec found for file " << filename << std::endl;
			return;
		}

		int ki = 0;
		AVPixelFormat list[255];
		bool found = false;
		while (codec->pix_fmts[ki] != AV_PIX_FMT_NONE) {
			list[ki] = codec->pix_fmts[ki];
			if (list[ki] == AV_PIX_FMT_YUV420P) {
				found = true;
			}
			ki++;
		}
		list[ki] = codec->pix_fmts[ki];
		if (found) {
			c->pix_fmt = AV_PIX_FMT_YUV420P;
		}
		else {
			c->pix_fmt = avcodec_find_best_pix_fmt_of_list(list, AV_PIX_FMT_RGB24, false, NULL); // PIX_FMT_YUV420P;// | AV_PIX_FMT_RGB24;
		}
		if (c->codec_id == AV_CODEC_ID_TIFF)
			c->pix_fmt = AV_PIX_FMT_RGB24;

		display_format(c->pix_fmt);

		//c->pix_fmt = AV_PIX_FMT_RGB24;

		if (codec->supported_framerates) {
			double bestfr = 1000;
			ki = 0;
			while (codec->supported_framerates[ki].den != 0) {
				double curfr = codec->supported_framerates[ki].den / (double)codec->supported_framerates[ki].num;
				if (abs(curfr - 1. / 25.) < abs(bestfr - 1. / 25.)) {
					bestfr = curfr;
					c->time_base.num = codec->supported_framerates[ki].den; // yes, there is a bug in ffmpeg
					c->time_base.den = codec->supported_framerates[ki].num;
				}
				ki++;
			}
		}
		else {
			c->time_base.num = 1;
			c->time_base.den = 25;
		}
		std::cout << c->time_base.num << "  " << c->time_base.den << std::endl;

		if (avcodec_open2(c, codec, NULL) < 0) { // Failed to open codec. 
			std::cout << "save_ffmpeg() : Failed to open codec for file " << filename << std::endl;
			return;
		}

		tmp_pict = avcodec_alloc_frame();
		if (!tmp_pict) { // Failed to allocate memory for tmp_pict frame.
			avcodec_close(video_str->codec);
			av_free(oc);
			std::cout << "save_ffmpeg() : Failed to allocate memory for file " << filename << std::endl;
			return;
		}
		tmp_pict->linesize[0] = (src_pxl_fmt == PIX_FMT_BGR24) ? 3 * frame_dimx : frame_dimx;
		tmp_pict->type = FF_BUFFER_TYPE_USER;
		int tmp_size = avpicture_get_size(src_pxl_fmt, frame_dimx, frame_dimy);
		uint8_t *tmp_buffer = (uint8_t*)av_malloc(tmp_size);
		if (!tmp_buffer) { // Failed to allocate memory for tmp buffer.
			av_free(tmp_pict);
			avcodec_close(video_str->codec);
			av_free(oc);
			std::cout << "save_ffmpeg() : Failed to allocate memory for file " << filename << std::endl;
			return;
		}

		// Associate buffer with tmp_pict.
		avpicture_fill((AVPicture*)tmp_pict, tmp_buffer, src_pxl_fmt, frame_dimx, frame_dimy);
		picture = avcodec_alloc_frame();
		if (!picture) { // Failed to allocate picture frame.
			av_free(tmp_pict->data[0]);
			av_free(tmp_pict);
			avcodec_close(video_str->codec);
			av_free(oc);
			std::cout << "save_ffmpeg() : Failed to allocate memory for file " << filename << std::endl;
			return;
		}

		int size = avpicture_get_size(c->pix_fmt, frame_dimx, frame_dimy);
		uint8_t *buffer = (uint8_t*)av_malloc(size);
		if (!buffer) { // Failed to allocate picture frame buffer.
			av_free(picture);
			av_free(tmp_pict->data[0]);
			av_free(tmp_pict);
			avcodec_close(video_str->codec);
			av_free(oc);
			std::cout << "save_ffmpeg() : Failed to allocate memory for file " << filename << std::endl;
			return;
		}

		// Associate the buffer with picture.
		avpicture_fill((AVPicture*)picture, buffer, c->pix_fmt, frame_dimx, frame_dimy);

		// Open file.
		if (!(fmt->flags&AVFMT_NOFILE)) {
			if (avio_open(&oc->pb, filename, AVIO_FLAG_WRITE) < 0) {
				std::cout << "save_ffmpeg() : Failed to open file " << filename << std::endl;
				return;
			}
		}


		if (avformat_write_header(oc, NULL) < 0) {
			std::cout << "save_ffmpeg() : Failed to write header in file " << filename << std::endl;
			return;
		}
		
		img_convert_context = 0;
		img_convert_context = sws_getContext(frame_dimx, frame_dimy, src_pxl_fmt,
			c->width, c->height, c->pix_fmt, sws_flags, 0, 0, 0);
		if (!img_convert_context) { // Failed to get swscale context.
			// if (!(fmt->flags & AVFMT_NOFILE)) url_fclose(&oc->pb);
			av_free(picture->data);
			av_free(picture);
			av_free(tmp_pict->data[0]);
			av_free(tmp_pict);
			avcodec_close(video_str->codec);
			av_free(oc);
			std::cout << "save_ffmpeg() : Failed to get conversion context for file " << filename << std::endl;
			return;
		}

		video_outbuf = 0;
		video_outbuf_size = 1000000;
		video_outbuf = (uint8_t*)av_malloc(video_outbuf_size);
		if (!video_outbuf) {
			// if (!(fmt->flags & AVFMT_NOFILE)) url_fclose(&oc->pb);
			av_free(picture->data);
			av_free(picture);
			av_free(tmp_pict->data[0]);
			av_free(tmp_pict);
			avcodec_close(video_str->codec);
			av_free(oc);
			std::cout << "save_ffmpeg() : Failed to allocate memory for file " << filename << std::endl;
			return;
		}
	}

	SwsContext *img_convert_context;
	uint8_t *video_outbuf;
	int video_outbuf_size;

	void addFrame(const T* frame) {

		double video_pts;
		std::vector<unsigned char> resized_frame(initial_W* initial_H * 3);
		for (int i=0; i<initial_W*initial_H*3; i++) {
			resized_frame[i] = min(255., max(0., frame[i]*255.));
		}
		for (int i=0; i<initial_W*initial_H; i++) {
			std::swap(resized_frame[i*3], resized_frame[i*3+2]);
		}
		
		new_W = initial_W;
		new_H = initial_H;

		int avail_W[255] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
		int avail_H[255] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
		switch (codec_id) {
		case CODEC_ID_H261: // 176x144, 352x288
			avail_W[0] = 171; avail_H[0] = 144;
			avail_W[1] = 352; avail_H[1] = 288;
			resize(resized_frame, new_W, new_H, 1, avail_W, avail_H);
			break;
		case AV_CODEC_ID_H263:  //128x96, 176x144,  352x288, 704x576, and 1408x1152
			avail_W[0] = 128; avail_H[0] = 96;
			avail_W[1] = 176; avail_H[1] = 144;
			avail_W[2] = 352; avail_H[2] = 288;
			avail_W[3] = 704; avail_H[3] = 576;
			avail_W[4] = 1408; avail_H[4] = 1152;
			resize(resized_frame, new_W, new_H, 1, avail_W, avail_H);
			break;
		}

		tmp_pict->data[0] = &resized_frame[0];

		if (video_str) video_pts = (video_str->pts.val * video_str->time_base.num) / (video_str->time_base.den);
		else video_pts = 0.0;
		if (!video_str) return;

		if (sws_scale(img_convert_context, tmp_pict->data, tmp_pict->linesize, 0, c->height, picture->data, picture->linesize)<0) return;
		int out_size = avcodec_encode_video(c, video_outbuf, video_outbuf_size, picture);
		int ret = 0;
		if (out_size>0) {
			AVPacket pkt;
			av_init_packet(&pkt);
			pkt.pts = av_rescale_q(c->coded_frame->pts, c->time_base, video_str->time_base);

			if (c->coded_frame->key_frame) pkt.flags |= AV_PKT_FLAG_KEY;
			pkt.stream_index = video_str->index;
			pkt.data = video_outbuf;
			pkt.size = out_size;
			ret = av_write_frame(oc, &pkt);
		}
		else if (out_size < 0) return;
		if (ret) return; // Error occured in writing frame.

		nb_recorded_frames++;

	}


	void finalize_video() {
#if 0   // should work, but it practice, makes everything crash ; so our files will probably be corrupted.
		// Close codec.
		if (video_str) {
			avcodec_close(video_str->codec);
			/* av_free(picture->data[0]);
			if (picture)
			av_free(picture);*/
			//av_free(tmp_pict->data[0]);
			/*if (tmp_pict)
			av_free(tmp_pict);*/
		}
		if (av_write_trailer(oc) < 0) {
			std::cout << "save_ffmpeg() : Failed to write trailer for file " << std::endl;// << filename << std::endl;
			return;
		}

		/*av_freep(&oc->streams[stream_index]->codec);
		av_freep(&oc->streams[stream_index]);*/
		if (!(fmt->flags&AVFMT_NOFILE)) {
			/*if (url_fclose(oc->pb)<0)
			throw CImgIOException(_cimglist_instance
			"save_ffmpeg() : File '%s', failed to close file.",
			cimglist_instance,
			filename);
			*/
		}

		if (oc)
			av_free(oc);

		/*if (video_outbuf)
			av_free(video_outbuf);*/
#endif
	}

};