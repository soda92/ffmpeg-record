/************************************************************************/
/* VideoPlay SDK 版本1.0 2016年9月29日                                  */
/* VideoPlay SDK 版本1.1 2016年10月27日                                 */
/* VideoPlay SDK 版本1.2 2019年04月01日                                 */
/************************************************************************/

#ifndef _WINDOWS_VEDIOPLAY_H_
#define _WINDOWS_VEDIOPLAY_H_

#include <Windows.h>

// #if defined(_WINDLL)
// #define _VEDIOPLAY_API  extern "C" __declspec(dllexport)
// #else 
// #define _VEDIOPLAY_API  extern "C" __declspec(dllimport)
// #endif

#define _VEDIOPLAY_API

#define MAX_PORT_NUM 99//最大支持的通道数

#define VEDIOPLAY_JUMP_CUR    1
#define VEDIOPLAY_JUMP_SET    0

_VEDIOPLAY_API int __stdcall Video_Init();
_VEDIOPLAY_API int __stdcall Video_Destroy();

_VEDIOPLAY_API int __stdcall Video_Play(LONG nPort, HWND hWnd,BOOL AudioFlag = FALSE);
_VEDIOPLAY_API int __stdcall Video_PauseContinue(LONG nPort);
_VEDIOPLAY_API int __stdcall Video_Seek(LONG nPort,char pos);//pos 0-100 百分比跳播
_VEDIOPLAY_API int __stdcall Video_Jump(LONG nPort,LONG sec,char JumpFlag);//sec 秒
_VEDIOPLAY_API int __stdcall Video_SetPlaySpeed(LONG nPort,float speed);
_VEDIOPLAY_API int __stdcall Video_Stop(LONG nPort);
_VEDIOPLAY_API unsigned int __stdcall Video_GetCurrentTime(LONG nPort);//获取当前播放的时长 返回秒 
_VEDIOPLAY_API unsigned int __stdcall Video_GetTotalTime(LONG nPort);//获取视频总时长 返回秒

_VEDIOPLAY_API int __stdcall Video_OpenStream(LONG nPort,DWORD nSize);
_VEDIOPLAY_API int __stdcall Video_InputData(LONG nPort,PBYTE pBuf,DWORD nSize);
_VEDIOPLAY_API int __stdcall Video_CloseStream(LONG nPort);

_VEDIOPLAY_API int __stdcall Video_OpenFile(LONG nPort,char* sFileName);
//_VEDIOPLAY_API int __stdcall Video_CloseFile();

//回调函数定义
typedef int(__stdcall *pCB)(unsigned char *data, int width,int height,LONG nPort);
//解码视频流->BGR24
_VEDIOPLAY_API int __stdcall Video_OpenDecode(LONG nPort,char* sFileName,pCB pCallBack);
_VEDIOPLAY_API int __stdcall Video_CloseDecode(LONG nPort);
//显示BGR24
_VEDIOPLAY_API int __stdcall Video_SetBGRPlay(HDC hdc);
_VEDIOPLAY_API int __stdcall Video_BGR24Play(HDC hdc,unsigned char *data,int DestWidth,int DestHeight,int lXSrc,int lYSrc,int pSrcWidth,int pSrcHeight,int SrcWidth,int SrcHeight);

//将视频数据已PS流形式保存MP4文件
_VEDIOPLAY_API int __stdcall Video_StartRecord(LONG nPort,char* sUrl,char* path,char* TrainNum,char* IPCName,char CH = 0);//CH 文件名最后的通道号。默认为0 按照nPort赋值 不为0的情况按照CH的值赋值
_VEDIOPLAY_API int __stdcall Video_StopRecord(LONG nPort);

//状态回调函数
//type 0-播放 1-解码BGR 2-保存文件 3-调试
//error 错误
typedef int(__stdcall *pStateCB)(LONG nPort,char type,char* error);
_VEDIOPLAY_API int __stdcall Video_SetStateCallBack(pStateCB pSCB);

/**
 *文件播放
 *Video_Init();
 *Video_OpenFile(LONG nPort,char* sFileName);
 *Video_Play(LONG nPort, HWND hWnd);
 *Video_Stop(LONG nPort);
 *Video_Destroy();
**/

/**
 *视频流播放
 *Video_Init();
 *Video_OpenStream(LONG nPort,DWORD nSize); nSize缓存区大小 取值例如 64*1024，128*1024 越大延时越高
 *Video_Play(LONG nPort, HWND hWnd);
 *Video_InputData(LONG nPort,PBYTE pBuf,DWORD nSize);
 *Video_CloseStream(LONG nPort);
 *Video_Stop(LONG nPort);
 *Video_Destroy();
**/

/**
 *解码BGR24
 *
 *先定义回调函数 
 *int __stdcall dataDeal(unsigned char *data, int width,int height,LONG nPort)//data RGB数据 width 图像宽 height 图像高 nPort 端口号
 *{
 *	TRACE("数据处理\n");
 *	return 0;
 *}
 *Video_OpenDecode(0,filepath,dataDeal);//解码函数
 *
 *Video_CloseDecode(LONG nPort);//停止函数
 *
 *;
**/


#endif