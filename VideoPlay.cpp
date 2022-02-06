//#include "stdafx.h"
#include "VideoPlay.h"
//#include <winbase.h>
#include <stdio.h>
#include <queue>
#include <list>

using namespace std;

#define SDL_MAIN_HANDLED
//
//#include "libavcodec/avcodec.h"
#define __STDC_CONSTANT_MACROS
extern "C"
{
#include "libavutil/time.h"
#include "libavformat/avformat.h"
#include "libavdevice/avdevice.h"
#include "libswscale/swscale.h"
#include "libswresample/swresample.h"
#include "SDL2/SDL.h"
};

#define PLAYMODE_FILE 0x1001
#define PLAYMODE_STREAM 0x1002

#define SDL_AUDIO_MIN_BUFFER_SIZE 512

char thread_exit[MAX_PORT_NUM] = "";  //退出标记 1使能
char thread_pause[MAX_PORT_NUM] = ""; //暂停标记 1使能
char thread_seek[MAX_PORT_NUM] = "";  //跳播标记 1使能
float play_speed[MAX_PORT_NUM] = {1};
long thread_jump[MAX_PORT_NUM] = {0};
unsigned int CurrentTime[MAX_PORT_NUM]; //当前播放的时间

int PlayStyle[MAX_PORT_NUM];
char DecodeFlag[MAX_PORT_NUM]; //解码标记 1使能 -1失败

AVFormatContext *pFormatCtx[MAX_PORT_NUM];
AVFormatContext *pRecFormatCtx[MAX_PORT_NUM];
char Record_exit[MAX_PORT_NUM] = "";

pStateCB StateCallBack = NULL;

typedef struct
{
	long CurrentPort;
	HWND CurrentHWND;
	char CH;
	char trainNum[50];
	char IPCName[50];
	BOOL AudioFlag;
} PlayInfo;

queue<PlayInfo *> InfoList;
queue<PlayInfo *> InfoDecList;
queue<PlayInfo *> RecordList;
// list<PlayInfo*> InfoList;
// PlayInfo CurrentInfo;

class CVideoBuf
{
public:
	unsigned int len;
	char *Vbuf;
	CVideoBuf(int i)
	{
		len = i;
		Vbuf = new char[i];
	}
	~CVideoBuf()
	{
		delete[] Vbuf;
	}
};
// queue<CVideoBuf*> VideoList[MAX_PORT_NUM];
list<CVideoBuf *> VideoList[MAX_PORT_NUM];
char PortBuf[MAX_PORT_NUM];

//音频相关
static AVPacket flush_pkt;

typedef struct MyAVPacketList
{
	AVPacket pkt;
	struct MyAVPacketList *next;
	int serial;
} MyAVPacketList;

typedef struct PacketQueue
{
	MyAVPacketList *first_pkt, *last_pkt;
	int nb_packets;
	int size;
	int64_t duration;
	int abort_request;
	int serial;
	SDL_mutex *mutex;
	SDL_cond *cond;
} PacketQueue;

typedef struct AudioState
{
	PacketQueue audioq;
	int64_t wanted_channel_layout;
	AVCodecContext *Audio_pCodecCtx;
	SDL_AudioSpec *wanted_spec, *spec;
	uint8_t *audio_buf;
	uint8_t *audio_buf1;
	unsigned int audio_buf_size; /* in bytes */
	unsigned int audio_buf1_size;
	int audio_buf_index; /* in bytes */
	double vClock;
	double aClock;
	struct SwrContext *au_convert_ctx;
	int64_t FirstAPts;
	char *thread_exit;
} AudioState;

static int packet_queue_put_private(PacketQueue *q, AVPacket *pkt)
{
	MyAVPacketList *pkt1;

	if (q->abort_request)
		return -1;

	pkt1 = (MyAVPacketList *)av_malloc(sizeof(MyAVPacketList));
	if (!pkt1)
		return -1;
	pkt1->pkt = *pkt;
	pkt1->next = NULL;
	if (pkt == &flush_pkt)
		q->serial++;
	pkt1->serial = q->serial;

	if (!q->last_pkt)
		q->first_pkt = pkt1;
	else
		q->last_pkt->next = pkt1;
	q->last_pkt = pkt1;
	q->nb_packets++;
	q->size += pkt1->pkt.size + sizeof(*pkt1);
	q->duration += pkt1->pkt.duration;
	/* XXX: should duplicate packet data in DV case */
	SDL_CondSignal(q->cond);
	return 0;
}

static int packet_queue_put(PacketQueue *q, AVPacket *pkt)
{
	int ret;

	SDL_LockMutex(q->mutex);
	ret = packet_queue_put_private(q, pkt);
	SDL_UnlockMutex(q->mutex);

	if (pkt != &flush_pkt && ret < 0)
		av_packet_unref(pkt);

	return ret;
}

/* return < 0 if aborted, 0 if no packet and > 0 if packet.  */
static int packet_queue_get(PacketQueue *q, AVPacket *pkt, int block, int *serial)
{
	MyAVPacketList *pkt1;
	int ret;

	SDL_LockMutex(q->mutex);

	for (;;)
	{
		if (q->abort_request)
		{
			ret = -1;
			break;
		}

		pkt1 = q->first_pkt;
		if (pkt1)
		{
			q->first_pkt = pkt1->next;
			if (!q->first_pkt)
				q->last_pkt = NULL;
			q->nb_packets--;
			q->size -= pkt1->pkt.size + sizeof(*pkt1);
			q->duration -= pkt1->pkt.duration;
			*pkt = pkt1->pkt;
			if (serial)
				*serial = pkt1->serial;
			av_free(pkt1);
			ret = 1;
			break;
		}
		else if (!block)
		{
			ret = 0;
			break;
		}
		else
		{
			SDL_CondWait(q->cond, q->mutex);
		}
	}
	SDL_UnlockMutex(q->mutex);
	return ret;
}

/* packet queue handling */
static int packet_queue_init(PacketQueue *q)
{
	memset(q, 0, sizeof(PacketQueue));
	q->mutex = SDL_CreateMutex();
	if (!q->mutex)
	{
		av_log(NULL, AV_LOG_FATAL, "SDL_CreateMutex(): %s\n", SDL_GetError());
		return AVERROR(ENOMEM);
	}
	q->cond = SDL_CreateCond();
	if (!q->cond)
	{
		av_log(NULL, AV_LOG_FATAL, "SDL_CreateCond(): %s\n", SDL_GetError());
		return AVERROR(ENOMEM);
	}
	q->abort_request = 1;
	return 0;
}

static void packet_queue_flush(PacketQueue *q)
{
	MyAVPacketList *pkt, *pkt1;

	SDL_LockMutex(q->mutex);
	for (pkt = q->first_pkt; pkt; pkt = pkt1)
	{
		pkt1 = pkt->next;
		av_packet_unref(&pkt->pkt);
		av_freep(&pkt);
	}
	q->last_pkt = NULL;
	q->first_pkt = NULL;
	q->nb_packets = 0;
	q->size = 0;
	q->duration = 0;
	SDL_UnlockMutex(q->mutex);
}

static void packet_queue_destroy(PacketQueue *q)
{
	packet_queue_flush(q);
	SDL_DestroyMutex(q->mutex);
	SDL_DestroyCond(q->cond);
}

static void packet_queue_abort(PacketQueue *q)
{
	SDL_LockMutex(q->mutex);

	q->abort_request = 1;

	SDL_CondSignal(q->cond);

	SDL_UnlockMutex(q->mutex);
}

static void packet_queue_start(PacketQueue *q)
{
	SDL_LockMutex(q->mutex);
	q->abort_request = 0;
	packet_queue_put_private(q, &flush_pkt);
	SDL_UnlockMutex(q->mutex);
}

// HANDLE hMutex;

int __stdcall Video_Init()
{
	av_register_all();
	avformat_network_init();
	// hMutex = CreateMutex(nullptr, FALSE, nullptr);

	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_AUDIO))
	{
		// AfxMessageBox( "Could not initialize SDL\n");
		return -1;
	}

	av_init_packet(&flush_pkt);
	flush_pkt.data = (uint8_t *)&flush_pkt;

	for (int i = 0; i < MAX_PORT_NUM; i++)
	{
		PortBuf[i] = i;
	}
	for (int i = 0; i < MAX_PORT_NUM; i++)
	{
		play_speed[i] = 1.0;
	}
	return 0;
}
int __stdcall Video_Destroy()
{
	// CloseHandle(hMutex);
	SDL_Quit();
	return 0;
}

void audio_callback(void *userdata, Uint8 *stream, int len)
{
	AudioState *as = (AudioState *)userdata;
	SDL_memset(stream, 0, len);

	int len1, audio_data_size;
	while (len > 0 && *as->thread_exit == 0)
	{
		if (as->audio_buf_index >= as->audio_buf_size)
		{
			AVPacket pkt;
			int serial;
			int got_frame = 0;

			av_freep(&as->audio_buf1);
			as->audio_buf1_size = 0;
			as->audio_buf = NULL;

			if (packet_queue_get(&as->audioq, &pkt, 1, &serial) < 0)
			{
				continue;
			}

			double aClock = (double)(pkt.pts - as->FirstAPts) / as->Audio_pCodecCtx->time_base.den;
			/*TRACE("sdl apts = %0.6f\n",aClock);
			TRACE("sdl vpts = %0.6f\n",as->vClock);*/
			if (as->vClock - aClock > 0.5)
			{
				av_packet_unref(&pkt);
				// TRACE("audio pkt give up.\n");
				as->aClock = 0.0;
				continue;
				// as->aClock = aClock - as->vClock;
			}
			else if (aClock - as->vClock > 0.5)
			{
				as->aClock = aClock - as->vClock;
			}
			/*else
				as->aClock = 0.0;*/

			AVFrame *pFrame = av_frame_alloc();
			avcodec_decode_audio4(as->Audio_pCodecCtx, pFrame, &got_frame, &pkt);
			if (got_frame)
			{

				// int out_count = (int64_t)pFrame->nb_samples * as->wanted_spec->freq / pFrame->sample_rate + 256;

				int out_size = av_samples_get_buffer_size(NULL, as->Audio_pCodecCtx->channels, /*out_count*/ pFrame->nb_samples, AV_SAMPLE_FMT_S16, 1);

				av_fast_malloc(&as->audio_buf1, &as->audio_buf1_size, out_size);
				int len2 = swr_convert(as->au_convert_ctx, &as->audio_buf1, /*out_count*/ pFrame->nb_samples, (const uint8_t **)pFrame->data, pFrame->nb_samples);
				as->audio_buf = as->audio_buf1;
				// as->audio_buf = pFrame->data[0];
				// as->audio_buf_size = out_size;
				as->audio_buf_size = len2 * as->Audio_pCodecCtx->channels * av_get_bytes_per_sample(AV_SAMPLE_FMT_S16);
				// TRACE("au count = %d,out size = %d\n",out_count,out_size);

				as->audio_buf_index = 0;
			}
			av_packet_unref(&pkt);
			av_frame_free(&pFrame);
		}

		len1 = as->audio_buf_size - as->audio_buf_index;
		if (len1 > len)
			len1 = len;

		memcpy(stream, (uint8_t *)as->audio_buf + as->audio_buf_index, len1);
		/*memset(stream, 0, len1);
		SDL_MixAudio(stream, (uint8_t *)as->audio_buf + as->audio_buf_index, len1, SDL_MIX_MAXVOLUME);*/
		len -= len1;
		stream += len1;
		as->audio_buf_index += len1;
	}
	// TRACE("audio quit = %d,len = %d \n",*as->thread_exit,len);
}

int WINAPI Thread_Play(LPVOID lpPara)
{
	PlayInfo *temp = InfoList.front();
	InfoList.pop();
	LONG nPort = temp->CurrentPort;
	HWND hWnd = temp->CurrentHWND;
	BOOL AudioFlag = temp->AudioFlag;
	delete temp;

	while (DecodeFlag[nPort] <= 0)
	{
		if (DecodeFlag[nPort] == -1)
			return -1;
		Sleep(10);
	}

	AVCodecContext *pCodecCtx, *Audio_pCodecCtx = NULL;
	AVCodec *pCodec, *Audio_pCodec;
	AVFrame *pFrame, *pFrameYUV;
	uint8_t *out_buffer;
	AVPacket *packet;
	int ret, got_picture;

	char erro[AV_ERROR_MAX_STRING_SIZE] = {0};

	//------------SDL----------------
	int screen_w, screen_h;
	SDL_Window *screen;
	SDL_Renderer *sdlRenderer;
	SDL_Texture *sdlTexture;
	/*SDL_Rect sdlRect;
	SDL_Rect sdlSrcRect;*/
	// SDL_Thread *video_tid;
	// SDL_Event event;
	// struct SwrContext *au_convert_ctx;
	struct SwsContext *img_convert_ctx;
	int i, videoindex, audioindex;
	videoindex = -1;
	audioindex = -1;
	for (i = 0; i < pFormatCtx[nPort]->nb_streams; i++)
	{
		if (pFormatCtx[nPort]->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO)
		{
			videoindex = i;
			// break;
		}
		if (pFormatCtx[nPort]->streams[i]->codec->codec_type == AVMEDIA_TYPE_AUDIO)
		{
			audioindex = i;
			// break;
		}
	}
	if (videoindex == -1)
	{
		return -1;
	}

	AudioState *as;
	SDL_AudioSpec wanted_spec, spec;

	if (audioindex != -1 && AudioFlag) //音频相关
	{
		Audio_pCodecCtx = pFormatCtx[nPort]->streams[audioindex]->codec;
		Audio_pCodec = avcodec_find_decoder(pFormatCtx[nPort]->streams[audioindex]->codec->codec_id);
		if (!Audio_pCodec || (avcodec_open2(Audio_pCodecCtx, Audio_pCodec, NULL) < 0))
		{
			Audio_pCodecCtx = NULL;
			Audio_pCodec = NULL;
		}
		else
		{
			as = (AudioState *)av_mallocz(sizeof(AudioState));

			int64_t wanted_channel_layout = 0;
			int wanted_nb_channels;
			const int next_nb_channels[] = {0, 0, 1, 6, 2, 6, 4, 6};

			wanted_nb_channels = Audio_pCodecCtx->channels;
			if (!wanted_channel_layout || wanted_nb_channels != av_get_channel_layout_nb_channels(wanted_channel_layout))
			{
				wanted_channel_layout = av_get_default_channel_layout(wanted_nb_channels);
				wanted_channel_layout &= ~AV_CH_LAYOUT_STEREO_DOWNMIX;
			}
			wanted_spec.channels = av_get_channel_layout_nb_channels(wanted_channel_layout);
			wanted_spec.freq = Audio_pCodecCtx->sample_rate;
			if (wanted_spec.freq <= 0 || wanted_spec.channels <= 0)
			{
				Audio_pCodecCtx = NULL;
				Audio_pCodec = NULL;
			}
			else
			{
				as->Audio_pCodecCtx = Audio_pCodecCtx;
				as->spec = &spec;
				as->wanted_spec = &wanted_spec;
				as->wanted_channel_layout = wanted_channel_layout;
				packet_queue_init(&as->audioq);
				as->audio_buf_index = 0;
				as->audio_buf_size = 0;
				as->thread_exit = &thread_exit[nPort];

				wanted_spec.format = AUDIO_S16SYS;
				wanted_spec.silence = 0;
				wanted_spec.samples = Audio_pCodecCtx->frame_size;
				wanted_spec.callback = audio_callback;
				wanted_spec.userdata = as;

				CoInitializeEx(NULL, COINIT_MULTITHREADED);
				while (SDL_OpenAudio(&wanted_spec, &spec) < 0)
				{
					wanted_spec.channels = next_nb_channels[FFMIN(7, wanted_spec.channels)];
					if (!wanted_spec.channels)
					{

						av_free(as);
						Audio_pCodecCtx = NULL;
						Audio_pCodec = NULL;
						break;
					}
				}
				// wanted_channel_layout = av_get_default_channel_layout(wanted_spec.channels);

				if (Audio_pCodecCtx)
				{
					as->au_convert_ctx = swr_alloc_set_opts(NULL,
															as->wanted_channel_layout, AV_SAMPLE_FMT_S16, as->spec->freq,
															av_get_default_channel_layout(as->Audio_pCodecCtx->channels), as->Audio_pCodecCtx->sample_fmt, as->Audio_pCodecCtx->sample_rate,
															0, NULL);
					swr_init(as->au_convert_ctx);
					packet_queue_start(&as->audioq);
					SDL_PauseAudio(0);
				}
			}
		}
	}
	char pause_flag;
	float speed;
	float framerate;

	int64_t prePts;
	double timeBase;
	LONG fileLen;
	int res;
	int64_t FirstPts;
	BOOL FirstPtsFlag;
	BOOL FirstAPtsFlag;
	int64_t diff;
	int64_t aDiff;

	pCodecCtx = pFormatCtx[nPort]->streams[videoindex]->codec;
	if (pCodecCtx->codec_id == AV_CODEC_ID_NONE)
	{
		pCodecCtx->codec_id = AV_CODEC_ID_H264;
		pCodecCtx->pix_fmt = AV_PIX_FMT_YUV420P;
	}
	pCodec = avcodec_find_decoder(pCodecCtx->codec_id);
	if (pCodec == NULL)
	{
		// AfxMessageBox("Codec not found.\n");
		sprintf(erro, "Codec not found.");
		goto end;
	}
	if (avcodec_open2(pCodecCtx, pCodec, NULL) < 0)
	{
		// AfxMessageBox("Could not open codec.\n");
		sprintf(erro, "Could not open codec.");
		goto end;
	}
	pFrame = av_frame_alloc();
	pFrameYUV = av_frame_alloc();
	out_buffer = (uint8_t *)av_malloc(avpicture_get_size(AV_PIX_FMT_YUV420P, pCodecCtx->width, pCodecCtx->height));
	avpicture_fill((AVPicture *)pFrameYUV, out_buffer, AV_PIX_FMT_YUV420P, pCodecCtx->width, pCodecCtx->height);

	img_convert_ctx = sws_getContext(pCodecCtx->width, pCodecCtx->height, pCodecCtx->pix_fmt,
									 pCodecCtx->width, pCodecCtx->height, AV_PIX_FMT_YUV420P, SWS_BICUBIC, NULL, NULL, NULL);

	// if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER)) {
	//	//AfxMessageBox( "Could not initialize SDL\n");
	//	return -1;
	// }
	// SDL 2.0 Support for multiple windows
	screen_w = pCodecCtx->width;
	screen_h = pCodecCtx->height;
	framerate = (float)(pFormatCtx[nPort]->streams[0]->r_frame_rate.num) / (pFormatCtx[nPort]->streams[0]->r_frame_rate.den);

	// TRACE("rate = %5.2f\n",framerate);

	//显示在弹出窗口
	/*screen = SDL_CreateWindow("Simplest ffmpeg player's Window", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
			screen_w, screen_h,SDL_WINDOW_OPENGL);*/
	//===========================================
	//显示在MFC控件上
	screen = SDL_CreateWindowFrom(hWnd);
	//===========================================
	if (!screen)
	{
		// AfxMessageBox("SDL: could not create window - exiting\n");
		return -1;
	}
	sdlRenderer = SDL_CreateRenderer(screen, -1, SDL_RENDERER_ACCELERATED);
	// IYUV: Y + U + V  (3 planes)
	// YV12: Y + V + U  (3 planes)
	sdlTexture = SDL_CreateTexture(sdlRenderer, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING /*SDL_TEXTUREACCESS_TARGET*/, pCodecCtx->width, pCodecCtx->height);

	packet = (AVPacket *)av_malloc(sizeof(AVPacket));

	// video_tid = SDL_CreateThread(sfp_refresh_thread,NULL,NULL);
	//------------SDL End------------
	// Event Loop
	prePts = 0;
	timeBase = 0.0;
	timeBase = av_q2d(pFormatCtx[nPort]->streams[videoindex]->time_base);
	fileLen = 0;
	res = 0;
	FirstPts = 0;
	FirstPtsFlag = TRUE;
	FirstAPtsFlag = TRUE;

	diff = 0;
	aDiff = 0;

	speed = 1;
	pause_flag = thread_pause[nPort];

	while (thread_exit[nPort] == 0)
	{
		// Wait
		// SDL_WaitEvent(&event);
		if (thread_seek[nPort] > 0)
		{
			int defaultStreamIndex = av_find_default_stream_index(pFormatCtx[nPort]);
			int timelen = pFormatCtx[nPort]->duration / 1000000;
			if (timelen > 0)
			{
				// int seektime = thread_seek[nPort]*timelen/100;
				int64_t seektime = FirstPts + av_rescale(thread_seek[nPort] * timelen / 100, pFormatCtx[nPort]->streams[videoindex]->time_base.den, pFormatCtx[nPort]->streams[videoindex]->time_base.num);
				// av_seek_frame(pFormatCtx[nPort], -1 , (seektime + FirstPts/pFormatCtx[nPort]->streams[videoindex]->time_base.den) * AV_TIME_BASE, AVSEEK_FLAG_ANY);
				av_seek_frame(pFormatCtx[nPort], defaultStreamIndex, seektime, AVSEEK_FLAG_ANY);
			}
			thread_seek[nPort] = 0;

			if (Audio_pCodecCtx)
			{
				packet_queue_flush(&as->audioq);
			}

			avcodec_flush_buffers(pCodecCtx);
		}
		if (thread_jump[nPort] != 0)
		{
			int defaultStreamIndex = av_find_default_stream_index(pFormatCtx[nPort]);
			// int timelen = pFormatCtx[nPort]->duration/1000000;

			int64_t seektime = FirstPts + av_rescale(CurrentTime[nPort] + thread_jump[nPort], pFormatCtx[nPort]->streams[videoindex]->time_base.den, pFormatCtx[nPort]->streams[videoindex]->time_base.num);

			av_seek_frame(pFormatCtx[nPort], defaultStreamIndex, seektime, AVSEEK_FLAG_ANY);

			thread_jump[nPort] = 0;

			if (Audio_pCodecCtx)
			{
				packet_queue_flush(&as->audioq);
			}
			avcodec_flush_buffers(pCodecCtx);
		}
		if (play_speed[nPort] != 1)
		{
			if (Audio_pCodecCtx)
			{
				// packet_queue_abort(&as->audioq);
				packet_queue_flush(&as->audioq);
				SDL_PauseAudio(1);

				// packet_queue_put_private(&as->audioq, &flush_pkt);
			}
			speed = play_speed[nPort];
		}
		else if (speed != 1)
		{
			// packet_queue_put_private(&as->audioq, &flush_pkt);
			if (Audio_pCodecCtx)
			{
				SDL_PauseAudio(0);
			}
			speed = play_speed[nPort];
		}

		if (thread_pause[nPort] == 0)
		{
			//------------------------------
			// TRACE("play\n");

			if (Audio_pCodecCtx && pause_flag != thread_pause[nPort])
			{
				SDL_PauseAudio(0);
				pause_flag = thread_pause[nPort];
			}

			if (res = av_read_frame(pFormatCtx[nPort], packet) >= 0)
			{
				/*avio_alloc_context()*/
				if (packet->stream_index == videoindex)
				{

					diff = av_gettime();
					ret = avcodec_decode_video2(pCodecCtx, pFrame, &got_picture, packet);
					if (ret < 0)
					{
						// AfxMessageBox("Decode Error.\n");
						// continue;
						// return -1;
					}
					if (FirstPtsFlag)
					{
						FirstPts = packet->pts;
						FirstPtsFlag = FALSE;
					}
					CurrentTime[nPort] = (packet->pts - FirstPts) / pFormatCtx[nPort]->streams[videoindex]->time_base.den;

					if (got_picture)
					{
						// TRACE("vPts = %d\n",pFrame->pkt_pts);
						if (Audio_pCodecCtx)
						{
							as->vClock = (double)(packet->pts - FirstPts) / pFormatCtx[nPort]->streams[videoindex]->time_base.den;
						}
						sws_scale(img_convert_ctx, (const uint8_t *const *)pFrame->data, pFrame->linesize, 0, pCodecCtx->height, pFrameYUV->data, pFrameYUV->linesize);
						// SDL---------------------------
						SDL_RenderClear(sdlRenderer);
						SDL_UpdateTexture(sdlTexture, NULL, pFrameYUV->data[0], pFrameYUV->linesize[0]);
						// SDL_RenderClear( sdlRenderer );
						// SDL_RenderCopy( sdlRenderer, sdlTexture, &sdlRect, &sdlRect );
						SDL_RenderCopy(sdlRenderer, sdlTexture, NULL, NULL);
						SDL_RenderPresent(sdlRenderer);
						// SDL End-----------------------
						// SDL_Delay(40);
						diff = av_gettime() - diff;
						if (Audio_pCodecCtx && as->aClock > 0)
						{

							diff -= (as->aClock * 1000) - 10;
						}
						if (PlayStyle[nPort] == PLAYMODE_FILE)
						{
							if (prePts == 0)
							{
								av_usleep(1000000 / framerate);
							}
							else
							{
								// int64_t difPts = pFrame->pkt_pts - prePts;
								int64_t delay = ((pFrame->pkt_pts - prePts) * 1000 * 1000 * timeBase) / play_speed[nPort] - diff - aDiff /* - (as->aClock*1000000)*/;
								if (delay > 0 && delay < 1500000)
								{
									/*TRACE("SleepPts = %d\n",delay);
										TRACE("aclok = %0.8f\n",(double)as->aClock);*/
									av_usleep(delay);
								}
							}
						}

						prePts = pFrame->pkt_pts;
					}
					av_free_packet(packet);
				}
				else if (packet->stream_index == audioindex && Audio_pCodecCtx != NULL && play_speed[nPort] == 1) //音频
				{
					// TRACE("aPts = %d\n",packet->pts);

					aDiff = av_gettime();
					packet->pts = av_rescale_q_rnd(packet->pts, pFormatCtx[nPort]->streams[audioindex]->time_base, Audio_pCodecCtx->time_base, (AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
					if (FirstAPtsFlag)
					{
						as->FirstAPts = packet->pts;
						FirstAPtsFlag = FALSE;
					}
					packet_queue_put(&as->audioq, packet);
					aDiff = av_gettime() - aDiff;
				}
				else
				{
					av_free_packet(packet);
				}
			}
			else
			{
				// Exit Thread
				thread_exit[nPort] = 1;
				/*
					if (PlayStyle[nPort] == PLAYMODE_FILE)
					{
						thread_exit[nPort]=1;
					}
					else if (PlayStyle[nPort] == PLAYMODE_STREAM)
					{
						//thread_pause[nPort] = 1;
					}
						*/
				// TRACE("exit\n");
			}
		}
		else
		{
			if (Audio_pCodecCtx && pause_flag != thread_pause[nPort])
			{
				// packet_queue_flush(&as->audioq);
				SDL_PauseAudio(1);
				pause_flag = thread_pause[nPort];
			}
			Sleep(10);
		}
	}

end:
	if (Audio_pCodecCtx)
	{
		packet_queue_abort(&as->audioq);
		SDL_CloseAudio();
		packet_queue_destroy(&as->audioq);
		swr_free(&as->au_convert_ctx);
		av_free(as);
	}

	sws_freeContext(img_convert_ctx);

	play_speed[nPort] = 1.0;
	SDL_DestroyTexture(sdlTexture);
	SDL_DestroyRenderer(sdlRenderer);
	SDL_DestroyWindow(screen);

	ShowWindow(hWnd, SW_SHOWNORMAL);
	// SDL_Quit();
	// FIX Small Bug
	// SDL Hide Window When it finished
	// CWnd::FromHandle(hWnd)->ShowWindow(SW_SHOWNORMAL);
	//--------------
	// av_free(aviobuffer);

	av_frame_free(&pFrameYUV);
	av_frame_free(&pFrame);
	avcodec_close(pCodecCtx);
	avcodec_close(Audio_pCodecCtx);
	avformat_close_input(&pFormatCtx[nPort]);
	avformat_free_context(pFormatCtx[nPort]);
	/*thread_pause[nPort] = 0;
		thread_exit[nPort] = 0;*/
	DecodeFlag[nPort] = 0;

	if (!strcmp(erro, ""))
	{
		sprintf_s(erro, "Thread exit");
	}
	if (StateCallBack)
	{
		StateCallBack(nPort, 0, erro);
	}

	return 1;
}

int __stdcall Video_Play(LONG nPort, HWND hWnd, BOOL AudioFlag)
{
	if (nPort > MAX_PORT_NUM)
	{
		return -1;
	}
	PlayInfo *temp = new PlayInfo;
	temp->CurrentPort = nPort;
	temp->CurrentHWND = hWnd;
	temp->AudioFlag = AudioFlag;
	InfoList.push(temp);

	thread_pause[nPort] = 0;
	thread_exit[nPort] = 0;
	CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)Thread_Play, NULL, 0, NULL); //开启线程
	return 0;
}

int __stdcall Video_OpenFile(long nPort, char *sFileName)
{
	if (nPort > MAX_PORT_NUM)
	{
		return -1;
	}
	// thread_pause[nPort] = 0;
	thread_exit[nPort] = 1;
	// Sleep(1000);

	DecodeFlag[nPort] = 0;

	pFormatCtx[nPort] = avformat_alloc_context();

	if (strstr(sFileName, "rtsp:"))
	{
		AVDictionary *options = NULL;
		av_dict_set(&options, "rtsp_transport", "tcp", 0);
		av_dict_set(&options, "stimeout", "2000000", 0);
		if (avformat_open_input(&pFormatCtx[nPort], sFileName, NULL, &options) != 0)
		{

			return -1;
		}
		av_dict_free(&options);
		PlayStyle[nPort] = PLAYMODE_STREAM;
	}
	else
	{
		if (avformat_open_input(&pFormatCtx[nPort], sFileName, NULL, NULL) != 0)
		{

			return -1;
		}
		PlayStyle[nPort] = PLAYMODE_FILE;
	}

	if (avformat_find_stream_info(pFormatCtx[nPort], NULL) < 0)
	{
		// AfxMessageBox("Couldn't find stream information.\n");
		DecodeFlag[nPort] = -1;
		return -1;
	}
	DecodeFlag[nPort] = 1;
	return 0;
}

int __stdcall Video_SetPlaySpeed(LONG nPort, float speed)
{
	if (nPort > MAX_PORT_NUM)
	{
		return -1;
	}

	// thread_pause[nPort] = 1;
	play_speed[nPort] = speed;
	Sleep(100);
	// thread_pause[nPort] = 0;
	return 0;
}

int __stdcall Video_Seek(LONG nPort, char pos)
{
	if (nPort > MAX_PORT_NUM)
	{
		return -1;
	}

	if (PlayStyle[nPort] == PLAYMODE_STREAM)
	{
		return -1;
	}
	// thread_pause[nPort] = 1;//
	thread_seek[nPort] = pos;
	Sleep(100);
	// thread_pause[nPort] = 0;

	return 0;
}

int __stdcall Video_Jump(LONG nPort, LONG sec, char JumpFlag)
{
	if (nPort > MAX_PORT_NUM)
	{
		return -1;
	}

	if (PlayStyle[nPort] == PLAYMODE_STREAM)
	{
		return -1;
	}

	// thread_pause[nPort] = 1;//

	if (JumpFlag == VEDIOPLAY_JUMP_SET)
	{
		thread_jump[nPort] = sec - Video_GetCurrentTime(nPort);
	}
	else if (JumpFlag == VEDIOPLAY_JUMP_CUR)
	{
		thread_jump[nPort] = sec;
	}

	Sleep(100);
	// thread_pause[nPort] = 0;

	return 0;
}

unsigned int __stdcall Video_GetCurrentTime(LONG nPort)
{
	return CurrentTime[nPort];
}
unsigned int __stdcall Video_GetTotalTime(LONG nPort)
{
	if (pFormatCtx[nPort] == nullptr)
	{
		return -1;
	}
	else
		return pFormatCtx[nPort]->duration / 1000000;
}

int __stdcall Video_PauseContinue(LONG nPort)
{
	if (nPort > MAX_PORT_NUM)
	{
		return -1;
	}

	if (PlayStyle[nPort] == PLAYMODE_STREAM)
	{
		return -1;
	}
	thread_pause[nPort] = ~thread_pause[nPort];
	return 0;
}
int __stdcall Video_Stop(LONG nPort)
{
	if (nPort > MAX_PORT_NUM)
	{
		return -1;
	}

	DecodeFlag[nPort] = -1;
	thread_exit[nPort] = 1;
	return 0;
}

// Callback
int read_buffer(void *opaque, uint8_t *buf, int buf_size)
{
	char nPort = *(char *)opaque;
	if (VideoList[nPort].size() > 2 /*!VideoList[nPort].empty()*/)
	{
		int true_size = 0;
		CVideoBuf *temp = VideoList[nPort].front();
		VideoList[nPort].pop_front();
		memcpy(buf, temp->Vbuf, temp->len);
		// TRACE("len = %d,port = %d\n",temp->len,nPort);
		true_size = temp->len;
		delete temp;

		return true_size;
	}
	else
		return 0;
}

int WINAPI Thread_OpenStream(LPVOID lpPara)
{
	char nPort = *(char *)lpPara;
	while (VideoList[nPort].size() < 50)
		;

	if (avformat_open_input(&pFormatCtx[nPort], NULL, NULL, NULL) != 0)
	{
		// printf("Couldn't open input stream.\n");
		// TRACE("Couldn't open input stream.\n");
		DecodeFlag[nPort] = -1;
		return -1;
	}

	if (avformat_find_stream_info(pFormatCtx[nPort], NULL) < 0)
	{
		// AfxMessageBox("Couldn't find stream information.\n");
		DecodeFlag[nPort] = -1;
		return -1;
	}

	DecodeFlag[nPort] = 1;

	return 0;
}

int __stdcall Video_OpenStream(LONG nPort, DWORD nSize)
{
	if (nPort > MAX_PORT_NUM)
	{
		return -1;
	}
	// thread_pause[nPort] = 0;
	thread_exit[nPort] = 1;
	// Sleep(1000);

	PlayStyle[nPort] = PLAYMODE_STREAM;
	DecodeFlag[nPort] = 0;

	pFormatCtx[nPort] = avformat_alloc_context();
	unsigned char *aviobuffer = (unsigned char *)av_malloc(nSize);
	AVIOContext *avio = avio_alloc_context(aviobuffer, nSize, 0, &PortBuf[nPort], read_buffer, NULL, NULL);
	pFormatCtx[nPort]->pb = avio;

	CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)Thread_OpenStream, &PortBuf[nPort], 0, NULL); //开启线程

	return 0;
}

int __stdcall Video_InputData(LONG nPort, PBYTE pBuf, DWORD nSize)
{
	if (nPort > MAX_PORT_NUM)
	{
		return -1;
	}
	if (PlayStyle[nPort] != PLAYMODE_STREAM)
	{
		return -1;
	}

	CVideoBuf *Temp = new CVideoBuf(nSize);
	memcpy(Temp->Vbuf, pBuf, nSize);
	VideoList[nPort].push_back(Temp);

	/*int num =VideoList[nPort].size();
	TRACE("%d list = %d\n",nPort,num);*/

	return nSize;
}

int __stdcall Video_CloseStream(LONG nPort)
{
	if (nPort > MAX_PORT_NUM)
	{
		return -1;
	}
	PlayStyle[nPort] = 0;
	DecodeFlag[nPort] = 0;

	/*int num =VideoList[nPort].size();
	TRACE("%d closelist = %d\n",nPort,num);*/
	while (VideoList[nPort].size() > 0)
	{
		CVideoBuf *temp = VideoList[nPort].front();
		delete temp;
		VideoList[nPort].pop_front();
	}
	return 0;
}

int WINAPI Thread_Decode(LPVOID lpPara)
{
	PlayInfo *temp = InfoDecList.front();
	InfoDecList.pop();
	LONG nPort = temp->CurrentPort;
	// HWND hWnd = temp->CurrentHWND;
	delete temp;

	pCB dataDeal = (pCB)lpPara;

	AVCodecContext *pCodecCtx;
	AVCodec *pCodec;
	AVFrame *pFrame, *pFrameYUV;
	uint8_t *out_buffer;
	AVPacket *packet;
	int ret, got_picture;

	int64_t prePts = 0;
	double timeBase = 0.0;
	LONG fileLen = 0;
	int res = 0;
	int64_t time = 0;
	float framerate;
	int re;

	char erro[AV_ERROR_MAX_STRING_SIZE] = {0};

	struct SwsContext *img_convert_ctx;
	int i, videoindex;
	videoindex = -1;
	for (i = 0; i < pFormatCtx[nPort]->nb_streams; i++)
	{
		if (pFormatCtx[nPort]->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO)
		{
			videoindex = i;
			break;
		}
	}
	if (videoindex == -1)
	{
		return -1;
	}

	pCodecCtx = pFormatCtx[nPort]->streams[videoindex]->codec;
	if (pCodecCtx->codec_id == AV_CODEC_ID_NONE)
	{
		pCodecCtx->codec_id = AV_CODEC_ID_H264;
		pCodecCtx->pix_fmt = AV_PIX_FMT_YUV420P;
		// pCodecCtx->pix_fmt = AV_PIX_FMT_RGB24;
	}
	pCodec = avcodec_find_decoder(pCodecCtx->codec_id);
	if (pCodec == NULL)
	{
		// AfxMessageBox("Codec not found.\n");
		sprintf(erro, "Codec not found.");
		goto end;
	}
	if (avcodec_open2(pCodecCtx, pCodec, NULL) < 0)
	{
		// AfxMessageBox("Could not open codec.\n");
		sprintf(erro, "Could not open codec.");
		goto end;
	}
	pFrame = av_frame_alloc();
	pFrameYUV = av_frame_alloc();

	out_buffer = (uint8_t *)av_malloc(avpicture_get_size(AV_PIX_FMT_BGR24, pCodecCtx->width, pCodecCtx->height));
	re = avpicture_fill((AVPicture *)pFrameYUV, out_buffer, AV_PIX_FMT_BGR24, pCodecCtx->width, pCodecCtx->height);

	/*img_convert_ctx = sws_getContext(pCodecCtx->width, pCodecCtx->height, pCodecCtx->pix_fmt,
			pCodecCtx->width, AV_PIX_FMT_RGB24, pCodecCtx->pix_fmt, SWS_BICUBIC, NULL, NULL, NULL);*/
	img_convert_ctx = sws_getContext(pCodecCtx->width, pCodecCtx->height, pCodecCtx->pix_fmt,
									 pCodecCtx->width, pCodecCtx->height, AV_PIX_FMT_BGR24, SWS_BICUBIC, NULL, NULL, NULL);

	framerate = (float)(pFormatCtx[nPort]->streams[0]->r_frame_rate.num) / (pFormatCtx[nPort]->streams[0]->r_frame_rate.den);

	// TRACE("rate = %5.2f\n",framerate);

	packet = (AVPacket *)av_malloc(sizeof(AVPacket));

	// video_tid = SDL_CreateThread(sfp_refresh_thread,NULL,NULL);
	//------------SDL End------------
	// Event Loop
	prePts = 0;
	timeBase = 0.0;
	fileLen = 0;
	res = 0;
	time = 0;
	timeBase = av_q2d(pFormatCtx[nPort]->streams[videoindex]->time_base);
	// FILE *output=fopen("out.rgb","wb+");

	while (thread_exit[nPort] == 0)
	{
		// Wait
		// SDL_WaitEvent(&event);
		if (thread_pause[nPort] == 0)
		{
			//------------------------------
			// TRACE("play\n");
			int64_t diff = av_gettime();
			if (res = av_read_frame(pFormatCtx[nPort], packet) >= 0)
			{
				/*avio_alloc_context()*/
				if (packet->stream_index == videoindex)
				{

					ret = avcodec_decode_video2(pCodecCtx, pFrame, &got_picture, packet);
					if (ret < 0)
					{
						// AfxMessageBox("Decode Error.\n");
						// continue;
						// return -1;
					}

					if (got_picture)
					{
						int res = sws_scale(img_convert_ctx, (const uint8_t *const *)pFrame->data, pFrame->linesize, 0, pCodecCtx->height, pFrameYUV->data, pFrameYUV->linesize);

						dataDeal(pFrameYUV->data[0], pCodecCtx->width, pCodecCtx->height, nPort);

						diff = av_gettime() - diff;
						// time = av_gettime();

						if (prePts == 0)
						{
							av_usleep(1000000 / framerate);
						}
						else
						{
							int64_t difPts = pFrame->pkt_pts - prePts;

							if (((difPts * 1000 * 1000 * timeBase) - diff) > 0 && ((difPts * 1000 * 1000 * timeBase) - diff) < 1000000)
							{
								// TRACE("SleepPts = %lf\n",(difPts*1000*1000*timeBase) - diff);
								av_usleep((difPts * 1000 * 1000 * timeBase) - diff);
							}
						}
						prePts = pFrame->pkt_pts;
					}
				}
				av_free_packet(packet);
			}
			else
			{

				if (PlayStyle[nPort] == PLAYMODE_FILE)
				{
					thread_exit[nPort] = 1;
				}
				else if (PlayStyle[nPort] == PLAYMODE_STREAM)
				{
					// thread_pause[nPort] = 1;
				}

				// TRACE("exit\n");
			}
		}
		else
		{
			Sleep(100);
		}
	}

end:
	sws_freeContext(img_convert_ctx);

	av_frame_free(&pFrameYUV);
	av_frame_free(&pFrame);
	avcodec_close(pCodecCtx);
	avformat_close_input(&pFormatCtx[nPort]);
	avformat_free_context(pFormatCtx[nPort]);

	if (!strcmp(erro, ""))
	{
		sprintf_s(erro, "Thread exit");
	}
	if (StateCallBack)
	{
		StateCallBack(nPort, 1, erro);
	}

	return 1;
}

int __stdcall Video_OpenDecode(LONG nPort, char *sFileName, pCB pCallBack)
{
	if (nPort > MAX_PORT_NUM)
	{
		return -1;
	}
	thread_exit[nPort] = 0;

	pFormatCtx[nPort] = avformat_alloc_context();
	if (strstr(sFileName, "rtsp:"))
	{
		AVDictionary *options = NULL;
		av_dict_set(&options, "rtsp_transport", "tcp", 0);
		av_dict_set(&options, "stimeout", "2000000", 0);
		if (avformat_open_input(&pFormatCtx[nPort], sFileName, NULL, &options) != 0)
		{

			return -1;
		}
		av_dict_free(&options);
	}
	else
	{
		if (avformat_open_input(&pFormatCtx[nPort], sFileName, NULL, NULL) != 0)
		{

			return -1;
		}
	}

	if (avformat_find_stream_info(pFormatCtx[nPort], NULL) < 0)
	{
		// AfxMessageBox("Couldn't find stream information.\n");

		return -1;
	}

	PlayInfo *temp = new PlayInfo;
	temp->CurrentPort = nPort;
	// temp->CurrentHWND = hWnd;
	InfoDecList.push(temp);

	CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)Thread_Decode, (void *)pCallBack, 0, NULL); //开启线程
	return 0;
}

int __stdcall Video_CloseDecode(LONG nPort)
{
	if (nPort > MAX_PORT_NUM)
	{
		return -1;
	}

	// DecodeFlag[nPort] = -1;
	thread_exit[nPort] = 1;
	return 0;
}

int __stdcall Video_SetBGRPlay(HDC hdc)
{
	return SetStretchBltMode(hdc, STRETCH_DELETESCANS);
}

int __stdcall Video_BGR24Play(HDC hdc, unsigned char *data, int DestWidth, int DestHeight, int lXSrc, int lYSrc, int pSrcWidth, int pSrcHeight, int SrcWidth, int SrcHeight)
{
	// BMP Header
	BITMAPINFO m_bmphdr = {0};
	DWORD dwBmpHdr = sizeof(BITMAPINFO);
	m_bmphdr.bmiHeader.biBitCount = 24;
	m_bmphdr.bmiHeader.biClrImportant = 0;
	m_bmphdr.bmiHeader.biSize = dwBmpHdr;
	m_bmphdr.bmiHeader.biSizeImage = 0;
	m_bmphdr.bmiHeader.biWidth = SrcWidth;
	//注意BMP在y方向是反着存储的，一次必须设置一个负值，才能使图像正着显示出来
	m_bmphdr.bmiHeader.biHeight = -SrcHeight;
	m_bmphdr.bmiHeader.biXPelsPerMeter = 0;
	m_bmphdr.bmiHeader.biYPelsPerMeter = 0;
	m_bmphdr.bmiHeader.biClrUsed = 0;
	m_bmphdr.bmiHeader.biPlanes = 1;
	m_bmphdr.bmiHeader.biCompression = BI_RGB;

	return StretchDIBits(hdc,
						 0, 0,
						 DestWidth, DestHeight,
						 lXSrc, lYSrc,
						 pSrcWidth, pSrcHeight,
						 data,
						 &m_bmphdr,
						 DIB_RGB_COLORS,
						 SRCCOPY);
}

int WINAPI Thread_Record(LPVOID lpPara)
{
	// char* path = (char*)lpPara;
	PlayInfo *temp = RecordList.front();
	RecordList.pop();
	char TrainNum[50] = "";
	char IPCName[50] = "";
	LONG nPort = temp->CurrentPort;
	// HWND hWnd = temp->CurrentHWND;
	char ch = temp->CH;
	strcpy(TrainNum, temp->trainNum);
	strcpy(IPCName, temp->IPCName);
	delete temp;

	char path[100] = "";
	strcpy(path, (char *)lpPara);

	char min = 1; //保存上次切文件时的分钟

	AVFormatContext *ofmt_ctx = NULL;
	AVOutputFormat *ofmt = NULL;
	int ret, i;
	AVPacket pkt;

	char erro[AV_ERROR_MAX_STRING_SIZE] = {0};
	bool IsFirst = true; //第一个视频包的标记

	SYSTEMTIME Time;
	char FileName[200] = "";
	char FilePath[200] = "";
	SYSTEMTIME CRTime;
	char FailNum = 0;
	AVStream *in_stream, *out_stream;

CutFile:

	avformat_alloc_output_context2(&ofmt_ctx, NULL, /*"dvd"*/ /*"avi"*/ NULL, /*NULL*/ "test.ts");
	if (!ofmt_ctx)
	{
		sprintf(erro, "Could not create output context");
		goto end;
	}

	ofmt = ofmt_ctx->oformat;
	for (i = 0; i < pRecFormatCtx[nPort]->nb_streams; i++)
	{
		AVStream *in_stream = pRecFormatCtx[nPort]->streams[i];
		AVStream *out_stream = avformat_new_stream(ofmt_ctx, in_stream->codec->codec);
		if (!out_stream)
		{
			sprintf(erro, "Failed allocating output stream");
			goto end;
		}
		ret = avcodec_copy_context(out_stream->codec, in_stream->codec);
		if (ret < 0)
		{
			sprintf(erro, "Failed to copy context from input to output stream codec context");
			goto end;
		}
		out_stream->codec->codec_tag = 0;
		if (ofmt_ctx->oformat->flags & AVFMT_GLOBALHEADER)
			out_stream->codec->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
	}

	GetLocalTime(&Time);
	min = Time.wMinute;

	sprintf_s(FilePath, "%s/%d-%02d-%02d/", path, Time.wYear, Time.wMonth, Time.wDay);
	CreateDirectory(FilePath, NULL);
	sprintf_s(FileName, "%s%s_VIDEO_%02d_%s_%d%02d%02d_%02d%02d%02d.mp4", FilePath, TrainNum, (ch == 0 ? nPort : ch), IPCName, Time.wYear, Time.wMonth, Time.wDay, Time.wHour, Time.wMinute, 0);

	if (!(ofmt->flags & AVFMT_NOFILE))
	{
		ret = avio_open(&ofmt_ctx->pb, FileName, AVIO_FLAG_WRITE);
		// TRACE("avio\n");
		if (ret < 0)
		{
			/*fprintf(stderr, "Could not open output file '%s'", out_filename);
			goto end;*/
			sprintf(erro, "Could not open output file");
			goto end;
		}
	}

	ret = avformat_write_header(ofmt_ctx, NULL);
	if (ret < 0)
	{
		av_make_error_string(erro, AV_ERROR_MAX_STRING_SIZE, ret);
		goto end;
	}

	// SYSTEMTIME CRTime;

	// pkt = av_packet_alloc();
	// pkt = (AVPacket *)av_malloc(sizeof(AVPacket));

	while (Record_exit[nPort] == 0)
	{

		GetLocalTime(&CRTime);
		// if (CRTime.wMinute >= Time.wMinute ? CRTime.wMinute - Time.wMinute >= 10 : Time.wMinute - CRTime.wMinute <= 50)
		//{
		//	//TRACE("cutFile\n");
		//	av_write_trailer(ofmt_ctx);
		//	avio_closep(&ofmt_ctx->pb);
		//	avformat_free_context(ofmt_ctx);
		//	goto CutFile;
		// }
		if (CRTime.wMinute == 0 || CRTime.wMinute == 15 || CRTime.wMinute == 30 || CRTime.wMinute == 45)
		{
			// TRACE("cutFile\n");
			if (CRTime.wMinute != min)
			{
				// sprintf(erro, "Cut File");
				min = CRTime.wMinute;
				av_write_trailer(ofmt_ctx);
				avio_closep(&ofmt_ctx->pb);
				avformat_free_context(ofmt_ctx);
				memset(erro, 0, sizeof(erro));
				goto CutFile;

				// goto end;
			}
		}
		else if (CRTime.wYear != Time.wYear || CRTime.wMonth != Time.wMonth || CRTime.wDay != Time.wDay || CRTime.wHour != Time.wHour)
		{
			sprintf(erro, "Time Changed");
			// goto end;
			// goto CutFile;
			break;
		}

		ret = av_read_frame(pRecFormatCtx[nPort], &pkt);
		/*if (ret < 0)
		break;*/
		if (ret < 0)
		{
			sprintf(erro, "Error read_frame");
			break;
		}
		//////////////////////////////////////////////////////////////////////////

		/*sprintf(erro, "index = %d",pkt.stream_index);
		StateCallBack(nPort,2,erro);*/

		//////////////////////////////////////////////////////////////////////////
		in_stream = pRecFormatCtx[nPort]->streams[pkt.stream_index];
		out_stream = ofmt_ctx->streams[pkt.stream_index];

		// log_packet(ifmt_ctx, &pkt, "in");
		// TRACE("index = %d ,pre pts = %d dts = %d\n",pkt.stream_index,pkt.pts,pkt.dts);
		/* copy packet */
		pkt.pts = av_rescale_q_rnd(pkt.pts, in_stream->time_base, out_stream->time_base, (AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
		pkt.dts = av_rescale_q_rnd(pkt.dts, in_stream->time_base, out_stream->time_base, (AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
		pkt.duration = av_rescale_q(pkt.duration, in_stream->time_base, out_stream->time_base);
		pkt.pos = -1;
		// log_packet(ofmt_ctx, &pkt, "out");
		// TRACE("pts = %d dts = %d\n",pkt.pts,pkt.dts);

		//由于海康摄像机升级后，第一包视频包的时间戳大于第二包，所以这里把第一包的时间戳置零
		if (IsFirst && ofmt_ctx->streams[pkt.stream_index]->codec->codec_type == AVMEDIA_TYPE_VIDEO)
		{
			pkt.pts = 0;
			pkt.dts = 0;
			IsFirst = false;
		}

		ret = av_interleaved_write_frame(ofmt_ctx, &pkt);
		/*av_buffer_unref(&pkt->buf);
		delete[] pkt->data;*/
		// av_packet_free(&pkt);
		// av_free_packet(pkt);
		av_packet_unref(&pkt);

		if (ret < 0)
		{
			// av_packet_unref(pkt);
			sprintf(erro, "Error muxing packet");
			break;
		}
		// if (ret < 0) {
		//	//fprintf(stderr, "Error muxing packet\n");
		//	//if (++FailNum > 5)
		//	//{
		//	//	//av_packet_unref(&pkt);
		//	//	sprintf(erro, "Error muxing packet");
		//	//	goto end;
		//	//}
		//
		// }else
		//{
		//	FailNum = 0;
		//	//av_packet_unref(&pkt);
		// }
		// av_packet_unref(pkt);
	}
	av_write_trailer(ofmt_ctx);

end:

	avformat_close_input(&pRecFormatCtx[nPort]);

	/* close output */
	if (ofmt_ctx && !(ofmt->flags & AVFMT_NOFILE))
		avio_closep(&ofmt_ctx->pb);
	avformat_free_context(ofmt_ctx);
	avformat_free_context(pRecFormatCtx[nPort]);

	if (!strcmp(erro, ""))
	{
		sprintf_s(erro, "Thread exit");
	}
	if (StateCallBack)
	{
		StateCallBack(nPort, 2, erro);
	}

	/*if (ret < 0 && ret != AVERROR_EOF) {
		fprintf(stderr, "Error occurred: %s\n", av_err2str(ret));
		return 1;
	}*/
	return 0;
}

int __stdcall Video_StartRecord(LONG nPort, const char *sUrl, const char *path, const char *TrainNum, const char *IPCName, int CH)
{
	if (nPort > MAX_PORT_NUM)
	{
		return -1;
	}
	Record_exit[nPort] = 0;
	if (pRecFormatCtx[nPort] != NULL)
	{
		return -1;
	}

	pRecFormatCtx[nPort] = avformat_alloc_context();
	if (strstr(sUrl, "rtsp:"))
	{
		AVDictionary *options = NULL;
		av_dict_set(&options, "rtsp_transport", "tcp", 0);
		av_dict_set(&options, "stimeout", "5000000", 0);
		if (avformat_open_input(&pRecFormatCtx[nPort], sUrl, NULL, &options) != 0)
		{

			return -1;
		}
		av_dict_free(&options);
	}
	else
	{
		if (avformat_open_input(&pRecFormatCtx[nPort], sUrl, NULL, NULL) != 0)
		{

			return -1;
		}
	}

	if (avformat_find_stream_info(pRecFormatCtx[nPort], NULL) < 0)
	{
		// AfxMessageBox("Couldn't find stream information.\n");

		return -1;
	}

	PlayInfo *temp = new PlayInfo;
	temp->CurrentPort = nPort;
	if (CH != 0)
		temp->CH = CH;
	else
		temp->CH = 0;
	strcpy(temp->trainNum, TrainNum);
	strcpy(temp->IPCName, IPCName);
	// temp->CurrentHWND = hWnd;
	RecordList.push(temp);

	CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)Thread_Record, (void*)path, 0, NULL); //开启线程
	return 0;
}
int __stdcall Video_StopRecord(LONG nPort)
{
	if (nPort > MAX_PORT_NUM)
	{
		return -1;
	}

	// DecodeFlag[nPort] = -1;
	Record_exit[nPort] = 1;
	return 0;
}

int __stdcall Video_SetStateCallBack(pStateCB pSCB)
{
	if (pSCB)
	{
		StateCallBack = pSCB;
	}
	else
		return -1;

	return 0;
}