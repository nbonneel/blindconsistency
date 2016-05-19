/***************************************************
This is the main Grabber code.  It uses AVbin and ffmpeg
to capture video and audio from video and audio files.

The code supports any number of audio or video streams and
is a cross platform solution to replace DDGrab.cpp.

This code was intended to be used inside of a matlab interface,
but can be used as a generic grabber class for anyone who needs
one.

Copyright 2008 Micah Richert

This file is part of mmread.
**************************************************/

#include "FFGrab.h"
#include <iostream>


#ifdef MATLAB_MEX_FILE
#include "mex.h"
#define FFprintf(...) mexPrintf(__VA_ARGS__)
#else
#define FFprintf(...) printf(__VA_ARGS__)
#endif

//#ifndef mwSize
//#define mwSize int
//#endif





#include <stdlib.h>
#include <string.h>
#include <vector>
#include <map>

#include <sys/types.h>
#include <sys/stat.h>
//#include <unistd.h>



template<typename T1, typename T2>
static inline T1 max(T1 a, T2 b) {
	return a < b ? b : a;
}
template<typename T1, typename T2>
static inline T1 min(T1 a, T2 b) {
	return a < b ? a : b;
}

bool file_exists(const char* filename) {
	std::ifstream ifile(filename);
	return ifile.good();
}

bool increment_file_number(std::string &path) {

	int i;
	for (i = path.length() - 1; i >= 0; i--) {

		if (isdigit(path[i])) {

			if (path[i] != '9') {
				path[i]++;
				return true;
			} else
				path[i] = '0';
		}
		if (path[i] == '\\' || path[i] == '/') {
			return false;
		}
	}
	return (i > 0);
}

Grabber::Grabber(FFGrabber* ffg, bool isAudio, AVbinStream* stream, bool trySeeking, double rate, int bytesPerWORD, AVbinStreamInfo info, AVbinTimestamp start_time)
{
	this->stream = stream;
	frameNr = 0;
	packetNr = 0;
	done = false;
	this->bytesPerWORD = bytesPerWORD;
	this->rate = rate;
	startTime = 0;
	stopTime = 0;
	this->isAudio = isAudio;
	this->info = info;
	this->trySeeking = trySeeking;
	this->start_time = start_time>0?start_time:0;
	this->ff = ffg;
};

int Grabber::Grab(AVbinPacket* packet)
{
	if (done) return 0;
	if (!packet->data) return 1;

	frameNr++;
	packetNr++;
#ifdef DEBUG
	FFprintf("frameNr %d %d %d\n",frameNr,packetNr,packet->size);
#endif
	int offset=0, len=0;
	double timestamp = (packet->timestamp-start_time)/1000.0/1000.0;
#ifdef DEBUG
	FFprintf("time %lld %lld %lf\n",packet->timestamp,start_time,timestamp);
#endif


	// either no frames are specified (capture all), or we have time specified
	if (stopTime)
	{
		if (isAudio)
		{
			// time is being used...
			if (timestamp >= startTime)
			{
				// if we've reached the start...
				offset = max(0,((int)((startTime-timestamp)*rate))*bytesPerWORD);
				len = ((int)((stopTime-timestamp)*rate))*bytesPerWORD;
				// if we have gone past our stop time...

				done = len < 0;
			}
		} else {
			done = stopTime <= timestamp;
			len = (startTime <= timestamp)?0x7FFFFFFF:0;
#ifdef DEBUG
			FFprintf("startTime: %lf, stopTime: %lf, current: %lf, done: %d, len: %d\n",startTime,stopTime,timestamp,done,len);
#endif
		}
	} else {
		// capture everything... video or audio
		len = 0x7FFFFFFF;
	}

	if (isAudio)
	{
		if (trySeeking && (len<=0 || done)) return 0;

		uint8_t audiobuf[1024*1024];
		int uint8_tsleft = sizeof(audiobuf);
		int uint8_tsout = uint8_tsleft;
		int uint8_tsread;
		uint8_t* audiodata = audiobuf;
#ifdef DEBUG
		FFprintf("avbin_decode_audio\n");
#endif
		while ((uint8_tsread = avbin_decode_audio(stream, packet->data, packet->size, audiodata, &uint8_tsout)) > 0)
		{
			packet->data += uint8_tsread;
			packet->size -= uint8_tsread;
			audiodata += uint8_tsout;
			uint8_tsleft -= uint8_tsout;
			uint8_tsout = uint8_tsleft;
		}

		int nrBytes = audiodata-audiobuf;
		len = min(len,nrBytes);
		offset = min(offset,nrBytes);

		uint8_t* tmp = new uint8_t[len];
		if (!tmp) return 2;

		memcpy(tmp,audiobuf+offset,len);

		frames.push_back(tmp);
		frameBytes.push_back(len);
		frameTimes.push_back(timestamp);

	} else {
		bool skip = false;
		if (frameNrs.size() > 0)
		{
			//frames are being specified
			// check to see if the frame is in our list
			bool foundNr = false;
			unsigned int lastFrameNr = 0;
			for (unsigned int i=0;i<frameNrs.size();i++)
			{
				if (frameNrs.at(i) == frameNr) foundNr = true;
				if (frameNrs.at(i) > lastFrameNr) lastFrameNr = frameNrs.at(i);
			}

			done = frameNr > lastFrameNr;
			if (!foundNr) {
#ifdef DEBUG
				FFprintf("Skipping frame %d\n",frameNr);
#endif
				skip = true;
			}
		}
		if ((trySeeking && skip && packetNr < ff->startDecodingAt && packetNr != 1) || done) return 0;

#ifdef DEBUG
		FFprintf("allocate frame %d\n",frames.size());
#endif
		uint8_t* videobuf = new uint8_t[bytesPerWORD];
		if (!videobuf) return 2;
#ifdef DEBUG
		FFprintf("avbin_decode_video\n");
#endif

		if (avbin_decode_video(stream, packet->data, packet->size,videobuf)<=0)
		{
#ifdef DEBUG
			FFprintf("avbin_decode_video FAILED!!!\n");
#endif
			// silently ignore decode errors
			frameNr--;
			delete[] videobuf;
			return 3;
		}

		if (stream->frame->key_frame)
		{
			ff->keyframes[packetNr] = timestamp;
		}

		if (skip || len==0)
		{
			delete[] videobuf;
			return 0;
		}
		frames.push_back(videobuf);
		frameBytes.push_back(min(len,bytesPerWORD));
		frameTimes.push_back(timestamp);
	}

	return 0;
}


Grabber::~Grabber()
{
	// clean up any remaining memory...
#ifdef DEBUG
	FFprintf("freeing frame data...\n");
#endif
	for (vector<uint8_t*>::iterator i=frames.begin();i != frames.end(); i++) free(*i);
}

FFGrabber::FFGrabber()
{
	stopForced = false;
	tryseeking = true;
	file = NULL;
	filename = NULL;

#ifdef DEBUG
	FFprintf("avbin_init\n");
#endif
 	if (avbin_init()) FFprintf("avbin_init init failed!!!\n");

	av_log_set_level(AV_LOG_QUIET);
}

void FFGrabber::cleanUp()
{
	if (!file) return; // nothing to cleanup.

	for (streammap::iterator i = streams.begin(); i != streams.end(); i++)
	{
		avbin_close_stream(i->second->stream);
		delete i->second;
	}

 	streams.clear();
 	videos.clear();
 	audios.clear();

 	avbin_close_file(file);
 	file = NULL;

#ifdef MATLAB_MEX_FILE
	if (matlabCommand) free(matlabCommand);
	matlabCommand = NULL;
#endif
}

int FFGrabber::getVideoInfo(unsigned int id, int* width, int* height, double* rate, int* nrFramesCaptured, int* nrFramesTotal, double* totalDuration)
{
	if (!width || !height || !nrFramesCaptured || !nrFramesTotal) return -1;

	if (id >= videos.size()) return -2;
	Grabber* CB = videos.at(id);

	if (!CB) return -1;

	*width  = CB->info.video.width;
	*height = CB->info.video.height;
	*rate = CB->rate;
	*nrFramesCaptured = CB->frames.size();
	*nrFramesTotal = CB->frameNr;

	if (fileinfo.duration > 0)
	{
		*totalDuration = fileinfo.duration/1000.0/1000.0;
		if (stopForced) *nrFramesTotal = (int)(-(*rate)*(*totalDuration));
	} else {
		*totalDuration = (*nrFramesCaptured) * (*rate);
	}

	return 0;
}

int FFGrabber::getAudioInfo(unsigned int id, int* nrChannels, double* rate, int* bits, int* nrFramesCaptured, int* nrFramesTotal, int* subtype, double* totalDuration)
{
	if (!nrChannels || !rate || !bits || !nrFramesCaptured || !nrFramesTotal) return -1;

	if (id >= audios.size()) return -2;
	Grabber* CB = audios.at(id);

	if (!CB) return -1;

	*nrChannels = CB->info.audio.channels;
	*rate = CB->info.audio.sample_rate;
	*bits = CB->info.audio.sample_bits;
	*subtype = CB->info.audio.sample_format;
	*nrFramesCaptured = CB->frames.size();
	*nrFramesTotal = CB->frameNr;

	*totalDuration = fileinfo.duration/1000.0/1000.0;

	return 0;
}

void FFGrabber::getCaptureInfo(int* nrVideo, int* nrAudio)
{
	if (!nrVideo || !nrAudio) return;

	*nrVideo = (int) videos.size();
	*nrAudio = (int) audios.size();
}

// data must be freed by caller
int FFGrabber::getVideoFrame(unsigned int id, unsigned int frameNr, uint8_t** data, unsigned int* nrBytes, double* time)
{
#ifdef DEBUG
	FFprintf("getting Video frame %d\n",frameNr);
#endif

	if (!data || !nrBytes) return -1;

	if (id >= videos.size()) return -2;
	Grabber* CB = videos[id];
	if (!CB) return -1;
	if (CB->frameNr == 0) return -2;
	if (frameNr < 0 || frameNr >= CB->frames.size()) return -2;

	uint8_t* tmp = CB->frames[frameNr];
	if (!tmp) return -2;

	*nrBytes = CB->frameBytes[frameNr];
	*time = CB->frameTimes[frameNr];

	*data = tmp;
	CB->frames[frameNr] = NULL;

	return 0;
}

// data must be freed by caller
int FFGrabber::getAudioFrame(unsigned int id, unsigned int frameNr, uint8_t** data, unsigned int* nrBytes, double* time)
{
	if (!data || !nrBytes) return -1;

	if (id >= audios.size()) return -2;
	Grabber* CB = audios[id];
	if (!CB) return -1;
	if (CB->frameNr == 0) return -2;
	if (frameNr < 0 || frameNr >= CB->frames.size()) return -2;

	uint8_t* tmp = CB->frames[frameNr];
	if (!tmp) return -2;

	*nrBytes = CB->frameBytes[frameNr];
	*time = CB->frameTimes[frameNr];

	*data = tmp;
	CB->frames[frameNr] = NULL;

	return 0;
}

void FFGrabber::setFrames(unsigned int* frameNrs, int nrFrames)
{
	if (!frameNrs) return;

	unsigned int minFrame=nrFrames>0?frameNrs[0]:0;

	this->frameNrs.clear();
	for (int i=0; i<nrFrames; i++) this->frameNrs.push_back(frameNrs[i]);

	for (unsigned int j=0; j < videos.size(); j++)
	{
		Grabber* CB = videos.at(j);
		if (CB)
		{
			CB->frames.clear();
			CB->frameNrs.clear();
			for (int i=0; i<nrFrames; i++)
			{
				CB->frameNrs.push_back(frameNrs[i]);
				minFrame=frameNrs[i]<minFrame?frameNrs[i]:minFrame;
			}
			CB->frameNr = 0;
			CB->packetNr = 0;
		}
	}

	if (tryseeking && nrFrames > 0)
	{
		startDecodingAt = 0;
		for (map<unsigned int,double>::const_iterator it=keyframes.begin();it != keyframes.end();it++)
		{
			if (it->first <= minFrame && it->first > startDecodingAt) startDecodingAt = it->first;
#ifdef DEBUG
			FFprintf("%d %d\n",it->first,startDecodingAt);
#endif
		}
	}


	// the meaning of frames doesn't make much sense for audio...
}


void FFGrabber::setTime(double startTime, double stopTime)
{
	this->startTime = startTime;
	this->stopTime = stopTime;
	frameNrs.clear();

	for (unsigned int i=0; i < videos.size(); i++)
	{
		Grabber* CB = videos.at(i);
		if (CB)
		{
			CB->frames.clear();
			CB->frameNrs.clear();
			CB->frameNr = 0;
			CB->packetNr = 0;
			CB->startTime = startTime;
			CB->stopTime = stopTime;
		}
	}

	for (unsigned int i=0; i < audios.size(); i++)
	{
		Grabber* CB = audios.at(i);
		if (CB)
		{
			CB->frames.clear();
			CB->frameNrs.clear();
			CB->startTime = startTime;
			CB->stopTime = stopTime;
		}
	}
}

#ifdef MATLAB_MEX_FILE
void FFGrabber::setMatlabCommand(char * matlabCommand)
{
	delete this->matlabCommand;
	this->matlabCommand = matlabCommand;
	mxDestroyArray(matlabCommandHandle);
	matlabCommandHandle=NULL;
}

void FFGrabber::setMatlabCommandHandle(mxArray* matlabCommandHandle)
{
	mxDestroyArray(this->matlabCommandHandle);
if (!mxIsClass(matlabCommandHandle,"function_handle")) mexErrMsgTxt("blah");
	this->matlabCommandHandle = matlabCommandHandle;
	mexMakeArrayPersistent(matlabCommandHandle);
	delete matlabCommand;
	matlabCommand=NULL;
}

void FFGrabber::runMatlabCommand(Grabber* G)
{
	if (matlabCommand || matlabCommandHandle)
	{
		mwSize dims[2];
		dims[1]=1;
		int width=G->info.video.width, height = G->info.video.height;
		mxArray* plhs[] = {NULL};
		int ExitCode;

		mexSetTrapFlag(0);

		if (G->frames.size() == 0) return;
		vector<uint8_t*>::iterator lastframe = --(G->frames.end());

		if (*lastframe == NULL) return;

		dims[0] = G->frameBytes.back();

		if (prhs[0] == NULL)
		{
			//make matrices to pass to the matlab function
			prhs[0] = mxCreateNumericArray(2, dims, mxUINT8_CLASS, mxREAL); // empty 2d matrix
			prhs[1] = mxCreateDoubleMatrix(1,1,mxREAL); mxGetPr(prhs[1])[0] = width;
			prhs[2] = mxCreateDoubleMatrix(1,1,mxREAL); mxGetPr(prhs[2])[0] = height;
			prhs[3] = mxCreateDoubleMatrix(1,1,mxREAL);
			prhs[4] = mxCreateDoubleMatrix(1,1,mxREAL);
			mexMakeArrayPersistent(prhs[0]);
			mexMakeArrayPersistent(prhs[1]);
			mexMakeArrayPersistent(prhs[2]);
			mexMakeArrayPersistent(prhs[3]);
			mexMakeArrayPersistent(prhs[4]);
		}
		mxGetPr(prhs[3])[0] = G->frameNrs.size()==0?G->frameTimes.size():G->frameNrs[G->frameTimes.size()-1];
		mxGetPr(prhs[4])[0] = G->frameTimes.back();

		memcpy(mxGetPr(prhs[0]),*lastframe,dims[0]);

		//free the frame memory
		free(*lastframe);
		*lastframe = NULL;

		//call Matlab
		if (matlabCommand) ExitCode = mexCallMATLAB(0,plhs,5,prhs,matlabCommand);
		else {
			for (int i=4;i>=0;i--) prhs[i+1]=prhs[i];
			prhs[0]=matlabCommandHandle;
			ExitCode = mexCallMATLAB(0,plhs,6,prhs,"feval");
			for (int i=0;i<=4;i++) prhs[i]=prhs[i+1];
		}
	}
}
#endif

int FFGrabber::build(const char* filename, char* format, bool disableVideo, bool disableAudio, bool tryseeking)
{
#ifdef DEBUG
	FFprintf("avbin_open_filename\n");
#endif
 	if (format && strlen(format) > 0) file = avbin_open_filename_with_format(filename,format);
	else file = avbin_open_filename(filename);
 	if (!file) return -4;

	//detect if the file has changed
	struct stat fstat;
	stat(filename,&fstat);

	if (!this->filename || strcmp(this->filename,filename)!=0 || filestat.st_mtime != fstat.st_mtime)
	{
		free(this->filename);
		this->filename=strdup(filename);
		memcpy(&filestat,&fstat,sizeof(fstat));

		keyframes.clear();
		startDecodingAt = 0xFFFFFFFF;
	}

	fileinfo.structure_size = sizeof(fileinfo);

#ifdef DEBUG
	FFprintf("avbin_file_info\n");
#endif
	if (avbin_file_info(file, &fileinfo)) return -1;

	// ugly hack but it is sometimes very wrong...
	if (fileinfo.start_time <-1000) fileinfo.start_time = 0;
	if (fileinfo.duration <-1000) fileinfo.duration = 0;

	for (int stream_index=0; stream_index<fileinfo.n_streams; stream_index++)
	{
		AVbinStreamInfo streaminfo;
		streaminfo.structure_size = sizeof(streaminfo);

#ifdef DEBUG
		FFprintf("avbin_stream_info\n");
#endif
		avbin_stream_info(file, stream_index, &streaminfo);

#ifdef DEBUG
		FFprintf("start time: %lld\n",streaminfo,fileinfo.start_time);
#endif

		if (avbin_get_version() < 8)
		{
	                AVRational frame_rate = file->context->streams[stream_index]->r_frame_rate;
	                streaminfo.video.frame_rate_num = frame_rate.num;
	                streaminfo.video.frame_rate_den = frame_rate.den;
		}

#ifdef DEBUG
		FFprintf("stream id: %d is %s %d\n",stream_index, (streaminfo.type == AVBIN_STREAM_TYPE_VIDEO?"Video":(streaminfo.type == AVBIN_STREAM_TYPE_AUDIO?"Audio":"Unknown")),disableVideo);
#endif

		if (streaminfo.type == AVBIN_STREAM_TYPE_VIDEO && !disableVideo)
		{
			AVbinStream * tmp = avbin_open_stream(file, stream_index);
			if (tmp)
			{
				double rate = streaminfo.video.frame_rate_num/(0.00001+streaminfo.video.frame_rate_den);

#ifdef DEBUG
				FFprintf("Inserting video stream %d\n",stream_index);
#endif
				streams[stream_index]=new Grabber(this, false,tmp,tryseeking,rate,streaminfo.video.height*streaminfo.video.width*3*sizeof(uint8_t),streaminfo,fileinfo.start_time);
				videos.push_back(streams[stream_index]);
			} else {
				FFprintf("Could not open video stream\n");
			}
		}
		if (streaminfo.type == AVBIN_STREAM_TYPE_AUDIO && !disableAudio)
		{
			AVbinStream * tmp = avbin_open_stream(file, stream_index);
			if (tmp)
			{
#ifdef DEBUG
				FFprintf("Inserting audio stream %d\n",stream_index);
#endif
				streams[stream_index]=new Grabber(this, true,tmp,tryseeking,streaminfo.audio.sample_rate,streaminfo.audio.sample_bits*streaminfo.audio.channels,streaminfo,fileinfo.start_time);
				audios.push_back(streams[stream_index]);
			} else {
				FFprintf("Could not open audio stream\n");
			}
		}
	}
	this->tryseeking = tryseeking;
	stopForced = false;

	if (streams.size() == 0) return -10;

	return 0;
}

int FFGrabber::doCapture()
{
	AVbinPacket packet;
	packet.structure_size = sizeof(packet);
	streammap::iterator tmp;
	int needseek=1;

	startTime = 0;
	bool allDone = false;	
	while (!avbin_read(file, &packet))
	{		
		if ((tmp = streams.find(packet.stream_index)) != streams.end())
		{			
			Grabber* G = tmp->second;
			G->Grab(&packet);

			if (G->done)
			{
				allDone = true;
				for (streammap::iterator i = streams.begin(); i != streams.end() && allDone; i++)
				{
					allDone = allDone && i->second->done;
				}
			}

#ifdef MATLAB_MEX_FILE
			if (!G->isAudio) runMatlabCommand(G);
#endif
		} else
#ifdef DEBUG
			FFprintf("Unknown packet %d\n",packet.stream_index);
#endif

		if (tryseeking && needseek)
		{
			if (stopTime && startTime > 0) {
#ifdef DEBUG
				FFprintf("try seeking to %lf\n",startTime);
#endif
				av_seek_frame(file->context, -1, (AVbinTimestamp)(startTime*1000*1000), AVSEEK_FLAG_BACKWARD);
			}
			needseek = 0;
		}

		if (allDone)
		{
#ifdef DEBUG
			FFprintf("stopForced\n");
#endif
			stopForced = true;
			break;
		}
	}

#ifdef MATLAB_MEX_FILE
	if (prhs[0])
	{
		mxDestroyArray(prhs[0]);
		if (prhs[1]) mxDestroyArray(prhs[1]);
		if (prhs[2]) mxDestroyArray(prhs[2]);
		if (prhs[3]) mxDestroyArray(prhs[3]);
		if (prhs[4]) mxDestroyArray(prhs[4]);
	}
	prhs[0] = NULL;
#endif

	return 0;
}

#ifdef MATLAB_MEX_FILE
FFGrabber FFG;

char* message(int err)
{
	switch (err)
	{
		case 0: return "";
		case -1: return "Unable to initialize";
		case -2: return "Invalid interface";
		case -4: return "Unable to open file";
		case -5: return "AVbin version 8 or greater is required!";
		case -10: return "No input streams available.  Make sure you are not disabling audio or video.";
		default: return "Unknown error";
	}
}

void mexFunction(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[])
{
	if (nrhs < 1 || !mxIsChar(prhs[0])) mexErrMsgTxt("First parameter must be the command (a string)");

	char cmd[100];
	mxGetString(prhs[0],cmd,100);

	if (!strcmp("build",cmd))
	{
		if (nrhs < 6 || !mxIsChar(prhs[1])) mexErrMsgTxt("build: parameters must be the filename (as a string), disableVideo, disableAudio, trySeeking");
		if (nlhs > 0) mexErrMsgTxt("build: there are no outputs");
		int filenamelen = mxGetN(prhs[1])+1;
		char* filename = new char[filenamelen];
		if (!filename) mexErrMsgTxt("build: out of memory");
		int formatlen = mxGetN(prhs[2])+1;
		char * format = new char[formatlen];
		mxGetString(prhs[1],filename,filenamelen);
		mxGetString(prhs[2],format,formatlen);

		if (strlen(format)==0)
		{
			delete[] format;
			format = NULL;
		}

		char* errmsg =  message(FFG.build(filename, format, mxGetScalar(prhs[3]), mxGetScalar(prhs[4]), mxGetScalar(prhs[5])));
		delete[] format;
		delete[] filename;

		if (strcmp("",errmsg)) mexErrMsgTxt(errmsg);
	} else if (!strcmp("doCapture",cmd)) {
		if (nlhs > 0) mexErrMsgTxt("doCapture: there are no outputs");
		char* errmsg =  message(FFG.doCapture());
		if (strcmp("",errmsg)) mexErrMsgTxt(errmsg);
	} else if (!strcmp("getVideoInfo",cmd)) {
		if (nrhs < 2 || !mxIsNumeric(prhs[1])) mexErrMsgTxt("getVideoInfo: second parameter must be the video stream id (as a number)");
		if (nlhs > 6) mexErrMsgTxt("getVideoInfo: there are only 5 output values: width, height, rate, nrFramesCaptured, nrFramesTotal");

		unsigned int id = (unsigned int)mxGetScalar(prhs[1]);
		int width,height,nrFramesCaptured,nrFramesTotal;
		double rate, totalDuration;
		char* errmsg =  message(FFG.getVideoInfo(id, &width, &height,&rate, &nrFramesCaptured, &nrFramesTotal, &totalDuration));

		if (strcmp("",errmsg)) mexErrMsgTxt(errmsg);

		if (nlhs >= 1) {plhs[0] = mxCreateDoubleMatrix(1,1,mxREAL); mxGetPr(plhs[0])[0] = width; }
		if (nlhs >= 2) {plhs[1] = mxCreateDoubleMatrix(1,1,mxREAL); mxGetPr(plhs[1])[0] = height; }
		if (nlhs >= 3) {plhs[2] = mxCreateDoubleMatrix(1,1,mxREAL); mxGetPr(plhs[2])[0] = rate; }
		if (nlhs >= 4) {plhs[3] = mxCreateDoubleMatrix(1,1,mxREAL); mxGetPr(plhs[3])[0] = nrFramesCaptured; }
		if (nlhs >= 5) {plhs[4] = mxCreateDoubleMatrix(1,1,mxREAL); mxGetPr(plhs[4])[0] = nrFramesTotal; }
		if (nlhs >= 6) {plhs[5] = mxCreateDoubleMatrix(1,1,mxREAL); mxGetPr(plhs[5])[0] = totalDuration; }
	} else if (!strcmp("getAudioInfo",cmd)) {
		if (nrhs < 2 || !mxIsNumeric(prhs[1])) mexErrMsgTxt("getAudioInfo: second parameter must be the audio stream id (as a number)");
		if (nlhs > 7) mexErrMsgTxt("getAudioInfo: there are only 6 output values: nrChannels, rate, bits, nrFramesCaptured, nrFramesTotal, subtype");

		unsigned int id = (unsigned int)mxGetScalar(prhs[1]);
		int nrChannels,bits,nrFramesCaptured,nrFramesTotal,subtype;
		double rate, totalDuration;
		char* errmsg =  message(FFG.getAudioInfo(id, &nrChannels, &rate, &bits, &nrFramesCaptured, &nrFramesTotal, &subtype, &totalDuration));

		if (strcmp("",errmsg)) mexErrMsgTxt(errmsg);

		if (nlhs >= 1) {plhs[0] = mxCreateDoubleMatrix(1,1,mxREAL); mxGetPr(plhs[0])[0] = nrChannels; }
		if (nlhs >= 2) {plhs[1] = mxCreateDoubleMatrix(1,1,mxREAL); mxGetPr(plhs[1])[0] = rate; }
		if (nlhs >= 3) {plhs[2] = mxCreateDoubleMatrix(1,1,mxREAL); mxGetPr(plhs[2])[0] = bits; }
		if (nlhs >= 4) {plhs[3] = mxCreateDoubleMatrix(1,1,mxREAL); mxGetPr(plhs[3])[0] = nrFramesCaptured; }
		if (nlhs >= 5) {plhs[4] = mxCreateDoubleMatrix(1,1,mxREAL); mxGetPr(plhs[4])[0] = nrFramesTotal; }
		if (nlhs >= 6) {plhs[5] = mxCreateDoubleMatrix(1,1,mxREAL); mxGetPr(plhs[5])[0] = subtype==AVBIN_SAMPLE_FORMAT_FLOAT?1:0; }
		if (nlhs >= 7) {plhs[6] = mxCreateDoubleMatrix(1,1,mxREAL); mxGetPr(plhs[6])[0] = totalDuration; }
	} else if (!strcmp("getCaptureInfo",cmd)) {
		if (nlhs > 2) mexErrMsgTxt("getCaptureInfo: there are only 2 output values: nrVideo, nrAudio");

		int nrVideo, nrAudio;
		FFG.getCaptureInfo(&nrVideo, &nrAudio);

		if (nlhs >= 1) {plhs[0] = mxCreateDoubleMatrix(1,1,mxREAL); mxGetPr(plhs[0])[0] = nrVideo; }
		if (nlhs >= 2) {plhs[1] = mxCreateDoubleMatrix(1,1,mxREAL); mxGetPr(plhs[1])[0] = nrAudio; }
	} else if (!strcmp("getVideoFrame",cmd)) {
		if (nrhs < 3 || !mxIsNumeric(prhs[1]) || !mxIsNumeric(prhs[2])) mexErrMsgTxt("getVideoFrame: second parameter must be the audio stream id (as a number) and third parameter must be the frame number");
		if (nlhs > 2) mexErrMsgTxt("getVideoFrame: there are only 2 output value: data");

		unsigned int id = (unsigned int)mxGetScalar(prhs[1]);
		unsigned int frameNr = (unsigned int)mxGetScalar(prhs[2]);
		uint8_t* data;
		unsigned int nrBytes;
		double time;
		mwSize dims[2];
		dims[1]=1;
		char* errmsg =  message(FFG.getVideoFrame(id, frameNr, &data, &nrBytes, &time));

		if (strcmp("",errmsg)) mexErrMsgTxt(errmsg);

		dims[0] = nrBytes;
		plhs[0] = mxCreateNumericArray(2, dims, mxUINT8_CLASS, mxREAL); // empty 2d matrix
		memcpy(mxGetPr(plhs[0]),data,nrBytes);
		free(data);
		if (nlhs >= 2) {plhs[1] = mxCreateDoubleMatrix(1,1,mxREAL); mxGetPr(plhs[1])[0] = time; }
	} else if (!strcmp("getAudioFrame",cmd)) {
		if (nrhs < 3 || !mxIsNumeric(prhs[1]) || !mxIsNumeric(prhs[2])) mexErrMsgTxt("getAudioFrame: second parameter must be the audio stream id (as a number) and third parameter must be the frame number");
		if (nlhs > 2) mexErrMsgTxt("getAudioFrame: there are only 2 output value: data");

		unsigned int id = (unsigned int)mxGetScalar(prhs[1]);
		unsigned int frameNr = (unsigned int)mxGetScalar(prhs[2]);
		uint8_t* data;
		unsigned int nrBytes;
		double time;
		mwSize dims[2];
		dims[1]=1;
		mxClassID mxClass;
		char* errmsg =  message(FFG.getAudioFrame(id, frameNr, &data, &nrBytes, &time));

		if (strcmp("",errmsg)) mexErrMsgTxt(errmsg);

		int nrChannels,bits,nrFramesCaptured,nrFramesTotal,subtype;
		double rate, totalDuration;
		FFG.getAudioInfo(id, &nrChannels, &rate, &bits, &nrFramesCaptured, &nrFramesTotal, &subtype, &totalDuration);

		switch (bits)
		{
			case 8:
			{
		 		dims[0] = nrBytes;
				mxClass = mxUINT8_CLASS;
				break;
			}
			case 16:
			{
				mxClass = mxINT16_CLASS;
				dims[0] = nrBytes/2;
				break;
			}
			case 24:
			{
				int* tmpdata = (int*)malloc(nrBytes/3*4);
				int i;

				//I don't know how 24bit float data is organized...
				for (i=0;i<nrBytes/3;i++)
				{
					tmpdata[i] = (((0x80&data[i*3+2])?-1:0)&0xFF000000) | ((data[i*3+2]<<16)+(data[i*3+1]<<8)+data[i*3]);
				}

				free(data);
				data = (uint8_t*)tmpdata;

				mxClass = mxINT32_CLASS;
				dims[0] = nrBytes/3;
				nrBytes = nrBytes/3*4;
				break;
			}
			case 32:
			{
				mxClass = subtype==AVBIN_SAMPLE_FORMAT_S32?mxINT32_CLASS:subtype==AVBIN_SAMPLE_FORMAT_FLOAT?mxSINGLE_CLASS:mxUINT32_CLASS;
				dims[0] = nrBytes/4;
				break;
			}
			default:
			{
		 		dims[0] = nrBytes;
				mxClass = mxUINT8_CLASS;
				break;
			}
		}

		plhs[0] = mxCreateNumericArray(2, dims, mxClass, mxREAL); // empty 2d matrix
		memcpy(mxGetPr(plhs[0]),data,nrBytes);
		free(data);
		if (nlhs >= 2) {plhs[1] = mxCreateDoubleMatrix(1,1,mxREAL); mxGetPr(plhs[1])[0] = time; }
	} else if (!strcmp("setFrames",cmd)) {
		if (nrhs < 2 || !mxIsDouble(prhs[1])) mexErrMsgTxt("setFrames: second parameter must be the frame numbers (as doubles)");
		if (nlhs > 0) mexErrMsgTxt("setFrames: has no outputs");
		int nrFrames = mxGetN(prhs[1]) * mxGetM(prhs[1]);
		unsigned int* frameNrs = new unsigned int[nrFrames];
		if (!frameNrs) mexErrMsgTxt("setFrames: out of memory");
		double* data = mxGetPr(prhs[1]);
		for (int i=0; i<nrFrames; i++) frameNrs[i] = (unsigned int)data[i];

		FFG.setFrames(frameNrs, nrFrames);

		delete[] frameNrs;
	} else if (!strcmp("setTime",cmd)) {
		if (nrhs < 3 || !mxIsDouble(prhs[1]) || !mxIsDouble(prhs[2])) mexErrMsgTxt("setTime: start and stop time are required (as doubles)");
		if (nlhs > 0) mexErrMsgTxt("setTime: has no outputs");

		FFG.setTime(mxGetScalar(prhs[1]), mxGetScalar(prhs[2]));
	} else if (!strcmp("setMatlabCommand",cmd)) {
		if (nrhs < 2 || !(mxIsChar(prhs[1]) || mxIsClass(prhs[1],"function_handle"))) mexErrMsgTxt("setMatlabCommand: the command must be passed as a string or function handle");
		if (nlhs > 0) mexErrMsgTxt("setMatlabCommand: has no outputs");

		if (mxIsChar(prhs[1])) {
			int len = mxGetN(prhs[1])+1;
			char * matlabCommand = new char[len];;
			mxGetString(prhs[1],matlabCommand,len);

			if (strlen(matlabCommand)==0)
			{
				FFG.setMatlabCommand(NULL);
				free(matlabCommand);
			} else FFG.setMatlabCommand(matlabCommand);
		} else {
			FFG.setMatlabCommandHandle(mxDuplicateArray(prhs[1]));
		}

	} else if (!strcmp("cleanUp",cmd)) {
		if (nlhs > 0) mexErrMsgTxt("cleanUp: there are no outputs");
		FFG.cleanUp();
	}
}
#endif

#ifdef TEST_FFGRAB
int main(int argc, char** argv)
{
	FFGrabber FFG;
printf("%s\n",argv[1]);
	FFG.build(argv[1],NULL,false,false,true);
	FFG.doCapture();
	int nrVideo, nrAudio;
	FFG.getCaptureInfo(&nrVideo, &nrAudio);

	printf("there are %d video streams, and %d audio.\n",nrVideo,nrAudio);
}
#endif




void log_callback(void *ptr, int level, const char *fmt, va_list vargs)
{
   static char message[8192];   
   const char *module = NULL;

   // Comment back in to filter only "important" messages
   /*if (level > AV_LOG_WARNING)
      return;*/

   // Get module name
    if (ptr)
    {
        AVClass *avc = *(AVClass**) ptr;
        module = avc->item_name(ptr);
    }

   // Create the actual message
   vsnprintf_s(message, sizeof(message), fmt, vargs);

   // Append the message to the logfile
   if (module)
   {
      cout  << module << " ********************" << endl;
   }
   cout << "lvl: " << level << endl << "msg: " << message << endl;
}


void writeVideo2(const char* filename, size_t W, size_t H, int nb_frames, std::vector<unsigned char> &video, int codec_id)
{

	av_register_all();
	av_log_set_callback(&log_callback);
	//avcodec_init();
	avcodec_register_all();


	AVCodec *codec;
	AVCodecContext *c; //= pCodecCtx;
	int i, out_size, size, outbuf_size;
	FILE *f;
	AVFrame *picture;
	uint8_t *outbuf, *picture_buf;
	printf("Video encoding...\n");

	/* find the mpeg1 video encoder */
	//codec = avcodec_find_encoder(CODEC_ID_MPEG1VIDEO); 
	codec = avcodec_find_encoder(CodecID(codec_id));	
	if (!codec) {
		fprintf(stderr, "codec not found\n");
		exit(1);
	}
	//codec->type = AVMEDIA_TYPE_VIDEO;
	codec->type = AVMEDIA_TYPE_VIDEO;
	

	c= avcodec_alloc_context3(codec);
	avcodec_get_context_defaults3(c, codec);
	c->codec_type = AVMEDIA_TYPE_VIDEO;
	picture= avcodec_alloc_frame();

	/* put sample parameters */
	std::cout<<"encoding with: "<<codec->name<<std::endl;
	c->bit_rate = 4000000;
	/* resolution must be a multiple of two */

	int avail_W[255] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
	int avail_H[255] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
	switch (codec->id) {
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
	

	c->width = W;
	c->height = H;
	/* frames per second */
	//c->time_base= (AVRational){1,25};
	//c->gop_size = 10; /* emit one intra frame every ten frames */
	//c->max_b_frames=1;
	AVPixelFormat list[255];
	int ki=0;
	while (codec->pix_fmts[ki]!=AV_PIX_FMT_NONE) {
		list[ki] = codec->pix_fmts[ki];
		ki++;
	}
	list[ki] = codec->pix_fmts[ki];
	c->pix_fmt = avcodec_find_best_pix_fmt_of_list(list, AV_PIX_FMT_RGB24, false, NULL); // PIX_FMT_YUV420P;// | AV_PIX_FMT_RGB24;
	//c->pix_fmt = AV_PIX_FMT_RGB24;

	if (codec->supported_framerates) {
		double bestfr = 1000;
		ki=0;
		while (codec->supported_framerates[ki].den!=0) {			
			double curfr = codec->supported_framerates[ki].den/(double)codec->supported_framerates[ki].num;
			if (abs(curfr-1./25)<abs(bestfr-1./25.)) {
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
	
	
	if (codec->id==CODEC_ID_H263) { //grrrr... doesn't work. 
		c->coder_type = AVMEDIA_TYPE_VIDEO;
		//c->partitions|=X264_PART_I4X4|X264_PART_I8X8|X264_PART_P8X8|X264_PART_B8X8;
		c->flags|=CODEC_FLAG_LOOP_FILTER;
		/*c->me_method = 7;
		c->sub_id = 6;
		c->me_range=16;
		c->me_cmp|=FF_CMP_CHROMA;
		c->ildct_cmp|=FF_CMP_CHROMA;
		c->qcompress=0.6;
		c->keyint_min = 25;
		c->b_frame_strategy = 1;
		c->i_quant_factor=0.71;
		c->scenechange_threshold=40;

		c->qmax=51;
		c->qmin=10;
		//c->bframebias=16;
		c->refs=2;
		c->max_qdiff = 16;
		//c->directpred=3;
		c->trellis=0;
		//c->flags2|=CODEC_FLAG2_BPYRAMID|CODEC_FLAG2_WPRED|CODEC_FLAG2_8X8DCT|CODEC_FLAG2_FASTPSKIP;
		c->gop_size = 10;

		c->dct_algo = FF_DCT_INT;
		c->idct_algo = FF_IDCT_INT; //FF_IDCT_H264
		//c->dsp_mask|=FF_MM_MMX|FF_MM_MMX2|FF_MM_SSE|FF_MM_SSE2|FF_MM_SSE42;*/



	}


	/* open it */
	if (avcodec_open(c, codec) < 0) {
		fprintf(stderr, "could not open codec\n");
		exit(1);
	}

	f = fopen(filename, "wb");
	if (!f) {
		fprintf(stderr, "could not open %s\n", filename);
		exit(1);
	}

	/* alloc image and output buffer */
	outbuf_size = 100000;
	outbuf = (uint8_t*) malloc(outbuf_size);
	size = c->width * c->height;	

	switch (c->pix_fmt) {
	case AV_PIX_FMT_YUVJ420P:
			c->color_range = AVCOL_RANGE_MPEG; //AVCOL_RANGE_JPEG;
	case AV_PIX_FMT_YUV420P:
	case AV_PIX_FMT_YUYV422:
	
		picture_buf = (uint8_t*) malloc((size * 3) / 2); /* size for YUV 420 */
		picture->data[0] = picture_buf;
		picture->data[1] = picture->data[0] + size;
		picture->data[2] = picture->data[1] + size / 4;
		picture->linesize[0] = c->width;
		picture->linesize[1] = c->width / 2;
		picture->linesize[2] = c->width / 2;
		break;
	case AV_PIX_FMT_RGB24:
	case AV_PIX_FMT_BGR24:
		picture_buf = (uint8_t*) malloc((size * 3)); /* size for YUV 420 */
		picture->data[0] = picture_buf;
		picture->data[1] = picture->data[0] + size;
		picture->data[2] = picture->data[1] + size *2;
		picture->linesize[0] = c->width;
		picture->linesize[1] = c->width;
		picture->linesize[2] = c->width;
		break;
	default:
		std::cout<<"output pixel format is not supported : "<<c->pix_fmt<<std::endl;
	}

	SwsContext* pSWSContext = sws_getContext(W, H, PIX_FMT_RGB24, W, H, c->pix_fmt, SWS_BILINEAR, 0, 0, 0); 
	AVFrame* pFrameRGB=avcodec_alloc_frame();

	// Determine required buffer size and allocate buffer
	int numBytes=avpicture_get_size(PIX_FMT_RGB24, W, H);
	uint8_t* buffer=new uint8_t[numBytes];

	avpicture_fill((AVPicture *)pFrameRGB, buffer,PIX_FMT_RGB24,	W, H);


	/* encode 1 second of video */
	for(i=0;i<nb_frames;i++) {

		unsigned char* cur_frame = &video[i*W*H*3];
		for (int y=0; y<H; y++)
			for (int x=0; x<W; x++)
				for (int c=0; c<3; c++)	{
					//pFrameRGB->data[0][y*pFrameRGB->linesize[0]+x*3+c] = cur_frame[y*W*3+x*3+c];	
					pFrameRGB->data[0][y*pFrameRGB->linesize[0]+x*3+c] = cur_frame[y*W*3+x*3+2-c];	
				}
		sws_scale(pSWSContext, pFrameRGB->data, pFrameRGB->linesize, 0, H, picture->data, picture->linesize); 
		//memcpy(picture->data, pFrameRGB->data, W*H*3*sizeof(pFrameRGB->data[0]));

		fflush(stdout);
		//picture->pts=(float) i * (1000.0/(float)(25.0)) * 90;;
		picture->pts = AV_NOPTS_VALUE;
		out_size = avcodec_encode_video(c, outbuf, outbuf_size, picture);
		fwrite(outbuf, 1, out_size, f);
	}

	/* get the delayed frames */
	for(; out_size; i++) {
		fflush(stdout);

		out_size = avcodec_encode_video(c, outbuf, outbuf_size, NULL);
		fwrite(outbuf, 1, out_size, f);
	}

	/* add sequence end code to have a real mpeg file */
	outbuf[0] = 0x00;
	outbuf[1] = 0x00;
	outbuf[2] = 0x01;
	outbuf[3] = 0xb7;
	fwrite(outbuf, 1, 4, f);
	fclose(f);
	free(picture_buf);
	free(outbuf);

	avcodec_close(c);
	av_free(c);
	av_free(picture);
	printf("\n");
	if (buffer)
		delete[] buffer;

}


std::string codec_id_to_str(int id) {

	switch(id) {
	case CODEC_ID_NONE: return std::string("none");
	case CODEC_ID_MPEG1VIDEO: return std::string("MPEG1");
	case CODEC_ID_MPEG2VIDEO: return std::string("MPEG2");
	case CODEC_ID_MPEG2VIDEO_XVMC: return std::string("MPEG2_XVMC");
	case CODEC_ID_H261: return std::string("H261");
	case CODEC_ID_H263: return std::string("H263");
	case CODEC_ID_RV10: return std::string("RV10");
	case CODEC_ID_RV20: return std::string("RV20");
	case CODEC_ID_MJPEG: return std::string("MJPEG");
	case CODEC_ID_MJPEGB: return std::string("MJPEGB");
	case CODEC_ID_LJPEG: return std::string("LJPEG");
	case CODEC_ID_SP5X: return std::string("SP5X");
	case CODEC_ID_JPEGLS: return std::string("JPEGLS");
	case CODEC_ID_MPEG4: return std::string("MPEG4");
	case CODEC_ID_RAWVIDEO: return std::string("RAWVIDEO");
	case CODEC_ID_MSMPEG4V1: return std::string("MSMPEG4V1");
	case CODEC_ID_MSMPEG4V2: return std::string("MSMPEG4V2");
	case CODEC_ID_MSMPEG4V3: return std::string("MSMPEG4V3");
	case CODEC_ID_WMV1: return std::string("WMV1");
	case CODEC_ID_WMV2: return std::string("WMV2");
	case CODEC_ID_H263P: return std::string("H263P");
	case CODEC_ID_H263I: return std::string("H263I");
	case CODEC_ID_FLV1: return std::string("FLV1");
	case CODEC_ID_SVQ1: return std::string("SVQ1");
	case CODEC_ID_SVQ3: return std::string("SVQ3");
	case CODEC_ID_DVVIDEO: return std::string("DVVIDEO");
	case CODEC_ID_HUFFYUV: return std::string("HUFFYUV");
	case CODEC_ID_CYUV: return std::string("CYUV");
	case CODEC_ID_H264: return std::string("H264");
	case CODEC_ID_INDEO3: return std::string("INDEO3");
	case CODEC_ID_VP3: return std::string("VP3");
	case CODEC_ID_THEORA: return std::string("THEORA");
	case CODEC_ID_ASV1: return std::string("ASV1");
	case CODEC_ID_ASV2: return std::string("ASV2");
	case CODEC_ID_FFV1: return std::string("FFV1");
	case CODEC_ID_4XM: return std::string("4XM");
	case CODEC_ID_VCR1: return std::string("VCR1");
	case CODEC_ID_CLJR: return std::string("CLJR");
	case CODEC_ID_MDEC: return std::string("MDEC");
	case CODEC_ID_ROQ: return std::string("ROQ");
	case CODEC_ID_INTERPLAY_VIDEO: return std::string("INTERPLAY_VIDEO");
	case CODEC_ID_XAN_WC3: return std::string("XAN_WC3");
	case CODEC_ID_XAN_WC4: return std::string("XAN_WC4");
	case CODEC_ID_RPZA: return std::string("RPZA");
	case CODEC_ID_CINEPAK: return std::string("CINEPAK");
	case CODEC_ID_WS_VQA: return std::string("WS_VQA");
	case CODEC_ID_MSRLE: return std::string("MSRLE");
	case CODEC_ID_MSVIDEO1: return std::string("MSVIDEO1");
	case CODEC_ID_IDCIN: return std::string("IDCIN");
	case CODEC_ID_8BPS: return std::string("8BPS");
	case CODEC_ID_SMC: return std::string("SMC");
	case CODEC_ID_FLIC: return std::string("FLIC");
	case CODEC_ID_TRUEMOTION1: return std::string("TRUEMOTION1");
	case CODEC_ID_VMDVIDEO: return std::string("VMDVIDEO");
	case CODEC_ID_MSZH: return std::string("MSZH");
	case CODEC_ID_ZLIB: return std::string("ZLIB");
	case CODEC_ID_QTRLE: return std::string("QTRLE");
	case CODEC_ID_SNOW: return std::string("SNOW");
	case CODEC_ID_TSCC: return std::string("TSCC");
	case CODEC_ID_ULTI: return std::string("ULTI");
	case CODEC_ID_QDRAW: return std::string("QDRAW");
	case CODEC_ID_VIXL: return std::string("VIXL");
	case CODEC_ID_QPEG: return std::string("QPEG");
	case CODEC_ID_PNG: return std::string("PNG");
	case CODEC_ID_PPM: return std::string("PPM");
	case CODEC_ID_PBM: return std::string("PBM");
	case CODEC_ID_PGM: return std::string("PGM");
	case CODEC_ID_PGMYUV: return std::string("PGMYUV");
	case CODEC_ID_PAM: return std::string("PAM");
	case CODEC_ID_FFVHUFF: return std::string("FFVHUFF");
	case CODEC_ID_RV30: return std::string("RV30");
	case CODEC_ID_RV40: return std::string("RV40");
	case CODEC_ID_VC1: return std::string("VC1");
	case CODEC_ID_WMV3: return std::string("WMV3");
	case CODEC_ID_LOCO: return std::string("LOCO");
	case CODEC_ID_WNV1: return std::string("WNV1");
	case CODEC_ID_AASC: return std::string("AASC");
	case CODEC_ID_INDEO2: return std::string("INDEO2");
	case CODEC_ID_FRAPS: return std::string("FRAPS");
	case CODEC_ID_TRUEMOTION2: return std::string("TRUEMOTION2");
	case CODEC_ID_BMP: return std::string("BMP");
	case CODEC_ID_CSCD: return std::string("CSCD");
	case CODEC_ID_MMVIDEO: return std::string("MMVIDEO");
	case CODEC_ID_ZMBV: return std::string("ZMBV");
	case CODEC_ID_AVS: return std::string("AVS");
	case CODEC_ID_SMACKVIDEO: return std::string("SMACKVIDEO");
	case CODEC_ID_NUV: return std::string("NUV");
	case CODEC_ID_KMVC: return std::string("KMVC");
	case CODEC_ID_FLASHSV: return std::string("FLASHSV");
	case CODEC_ID_CAVS: return std::string("CAVS");
	case CODEC_ID_JPEG2000: return std::string("JPEG2000");
	case CODEC_ID_VMNC: return std::string("VMNC");
	case CODEC_ID_VP5: return std::string("VP5");
	case CODEC_ID_VP6: return std::string("VP6");
	case CODEC_ID_VP6F: return std::string("VP6F");
	case CODEC_ID_TARGA: return std::string("TARGA");
	case CODEC_ID_DSICINVIDEO: return std::string("DSICINVIDEO");
	case CODEC_ID_TIERTEXSEQVIDEO: return std::string("TIERTEXSEQVIDEO");
	case CODEC_ID_TIFF: return std::string("TIFF");
	case CODEC_ID_GIF: return std::string("GIF");
	case CODEC_ID_DXA: return std::string("DXA");
	case CODEC_ID_DNXHD: return std::string("DNXHD");
	case CODEC_ID_THP: return std::string("THP");
	case CODEC_ID_SGI: return std::string("SGI");
	case CODEC_ID_C93: return std::string("C93");
	case CODEC_ID_BETHSOFTVID: return std::string("BETHSOFTVID");
	case CODEC_ID_PTX: return std::string("PTX");
	case CODEC_ID_TXD: return std::string("TXD");
	case CODEC_ID_VP6A: return std::string("VP6A");
	case CODEC_ID_AMV: return std::string("AMV");
	case CODEC_ID_VB: return std::string("VB");
	case CODEC_ID_PCX: return std::string("PCX");
	case CODEC_ID_SUNRAST: return std::string("SUNRAST");
	case CODEC_ID_INDEO4: return std::string("INDEO4");
	case CODEC_ID_INDEO5: return std::string("INDEO5");
	case CODEC_ID_MIMIC: return std::string("MIMIC");
	case CODEC_ID_RL2: return std::string("RL2");
	case CODEC_ID_ESCAPE124: return std::string("ESCAPE124");
	case CODEC_ID_DIRAC: return std::string("DIRAC");
	case CODEC_ID_BFI: return std::string("BFI");
	case CODEC_ID_CMV: return std::string("CMV");
	case CODEC_ID_MOTIONPIXELS: return std::string("MOTIONPIXELS");
	case CODEC_ID_TGV: return std::string("TGV");
	case CODEC_ID_TGQ: return std::string("TGQ");
	case CODEC_ID_TQI: return std::string("TQI");
	case CODEC_ID_AURA: return std::string("AURA");
	case CODEC_ID_AURA2: return std::string("AURA2");
	case CODEC_ID_V210X: return std::string("V210X");
	case CODEC_ID_TMV: return std::string("TMV");
	case CODEC_ID_V210: return std::string("V210");
	case CODEC_ID_DPX: return std::string("DPX");
	case CODEC_ID_MAD: return std::string("MAD");
	case CODEC_ID_FRWU: return std::string("FRWU");
	case CODEC_ID_FLASHSV2: return std::string("FLASHSV2");
	default: return std::string("unknown codec");
	}
}

void display_format(AVPixelFormat p) {
		switch(p) {
	case AV_PIX_FMT_RGB24:
		std::cout<<"AV_PIX_FMT_RGB24"<<std::endl;
		break;
	case AV_PIX_FMT_YUV420P:
		std::cout<<"AV_PIX_FMT_YUV420P"<<std::endl;
		break;
	case AV_PIX_FMT_BGR24:
		std::cout<<"AV_PIX_FMT_BGR24"<<std::endl;
		break;
	case AV_PIX_FMT_YUYV422:
		std::cout<<"AV_PIX_FMT_YUYV422"<<std::endl;
		break;
	case AV_PIX_FMT_PAL8:
		std::cout<<"AV_PIX_FMT_PAL8"<<std::endl;
		break;
	case AV_PIX_FMT_YUVJ420P:
		std::cout<<"AV_PIX_FMT_YUVJ420P"<<std::endl;
		break;
	case AV_PIX_FMT_YUV422P:
		std::cout<<"AV_PIX_FMT_YUV422P"<<std::endl;
		break;
	case AV_PIX_FMT_YUV444P:
		std::cout<<"AV_PIX_FMT_YUV444P"<<std::endl;
		break;
	case AV_PIX_FMT_YUV410P:
		std::cout<<"AV_PIX_FMT_YUV410P"<<std::endl;
		break;
	case AV_PIX_FMT_YUV411P:
		std::cout<<"AV_PIX_FMT_YUV411P"<<std::endl;
		break;
	case AV_PIX_FMT_GRAY8:
		std::cout<<"AV_PIX_FMT_GRAY8"<<std::endl;
		break;
	case AV_PIX_FMT_YUVJ422P:
		std::cout<<"AV_PIX_FMT_YUVJ422P"<<std::endl;
		break;
	case AV_PIX_FMT_YUVJ444P:
		std::cout<<"AV_PIX_FMT_YUVJ444P"<<std::endl;
		break;
	case AV_PIX_FMT_XVMC_MPEG2_MC:
		std::cout<<"AV_PIX_FMT_XVMC_MPEG2_MC"<<std::endl;
		break;
	case AV_PIX_FMT_XVMC_MPEG2_IDCT:
		std::cout<<"AV_PIX_FMT_XVMC_MPEG2_IDCT"<<std::endl;
		break;
	default:
		std::cout<<"unknown colors"<<std::endl;
	}
}


