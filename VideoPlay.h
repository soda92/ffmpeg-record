/************************************************************************/
/* VideoPlay SDK �汾1.0 2016��9��29��                                  */
/* VideoPlay SDK �汾1.1 2016��10��27��                                 */
/* VideoPlay SDK �汾1.2 2019��04��01��                                 */
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

#define MAX_PORT_NUM 99 //���֧�ֵ�ͨ����

#define VEDIOPLAY_JUMP_CUR 1
#define VEDIOPLAY_JUMP_SET 0

_VEDIOPLAY_API int __stdcall Video_Init();
_VEDIOPLAY_API int __stdcall Video_Destroy();

_VEDIOPLAY_API int __stdcall Video_Play(LONG nPort, HWND hWnd, BOOL AudioFlag = FALSE);
_VEDIOPLAY_API int __stdcall Video_PauseContinue(LONG nPort);
_VEDIOPLAY_API int __stdcall Video_Seek(LONG nPort, char pos);                // pos 0-100 �ٷֱ�����
_VEDIOPLAY_API int __stdcall Video_Jump(LONG nPort, LONG sec, char JumpFlag); // sec ��
_VEDIOPLAY_API int __stdcall Video_SetPlaySpeed(LONG nPort, float speed);
_VEDIOPLAY_API int __stdcall Video_Stop(LONG nPort);
_VEDIOPLAY_API unsigned int __stdcall Video_GetCurrentTime(LONG nPort); //��ȡ��ǰ���ŵ�ʱ�� ������
_VEDIOPLAY_API unsigned int __stdcall Video_GetTotalTime(LONG nPort);   //��ȡ��Ƶ��ʱ�� ������

_VEDIOPLAY_API int __stdcall Video_OpenStream(LONG nPort, DWORD nSize);
_VEDIOPLAY_API int __stdcall Video_InputData(LONG nPort, PBYTE pBuf, DWORD nSize);
_VEDIOPLAY_API int __stdcall Video_CloseStream(LONG nPort);

_VEDIOPLAY_API int __stdcall Video_OpenFile(LONG nPort, char *sFileName);
//_VEDIOPLAY_API int __stdcall Video_CloseFile();

//�ص���������
typedef int(__stdcall *pCB)(unsigned char *data, int width, int height, LONG nPort);
//������Ƶ��->BGR24
_VEDIOPLAY_API int __stdcall Video_OpenDecode(LONG nPort, char *sFileName, pCB pCallBack);
_VEDIOPLAY_API int __stdcall Video_CloseDecode(LONG nPort);
//��ʾBGR24
_VEDIOPLAY_API int __stdcall Video_SetBGRPlay(HDC hdc);
_VEDIOPLAY_API int __stdcall Video_BGR24Play(HDC hdc, unsigned char *data, int DestWidth, int DestHeight, int lXSrc, int lYSrc, int pSrcWidth, int pSrcHeight, int SrcWidth, int SrcHeight);

//����Ƶ���ݱ���MP4�ļ�
_VEDIOPLAY_API int __stdcall Video_StartRecord(LONG nPort, const char *sUrl, const char *path, const char *TrainNum, const char *IPCName, int CH = 0); // CH �ļ�������ͨ���š�Ĭ��Ϊ0 ����nPort��ֵ ��Ϊ0���������CH��ֵ��ֵ
_VEDIOPLAY_API int __stdcall Video_StopRecord(LONG nPort);

//״̬�ص�����
// type 0-���� 1-����BGR 2-�����ļ� 3-����
// error ����
typedef int(__stdcall *pStateCB)(LONG nPort, char type, char *error);
_VEDIOPLAY_API int __stdcall Video_SetStateCallBack(pStateCB pSCB);

/**
 *�ļ�����
 *Video_Init();
 *Video_OpenFile(LONG nPort,char* sFileName);
 *Video_Play(LONG nPort, HWND hWnd);
 *Video_Stop(LONG nPort);
 *Video_Destroy();
 **/

/**
 *��Ƶ������
 *Video_Init();
 *Video_OpenStream(LONG nPort,DWORD nSize); nSize��������С ȡֵ���� 64*1024��128*1024 Խ����ʱԽ��
 *Video_Play(LONG nPort, HWND hWnd);
 *Video_InputData(LONG nPort,PBYTE pBuf,DWORD nSize);
 *Video_CloseStream(LONG nPort);
 *Video_Stop(LONG nPort);
 *Video_Destroy();
 **/

/**
 *����BGR24
 *
 *�ȶ���ص�����
 *int __stdcall dataDeal(unsigned char *data, int width,int height,LONG nPort)//data RGB���� width ͼ��� height ͼ��� nPort �˿ں�
 *{
 *	TRACE("���ݴ���\n");
 *	return 0;
 *}
 *Video_OpenDecode(0,filepath,dataDeal);//���뺯��
 *
 *Video_CloseDecode(LONG nPort);//ֹͣ����
 *
 *;
 **/

#endif