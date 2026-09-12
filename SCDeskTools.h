
// SCDeskTools.h: PROJECT_NAME 애플리케이션에 대한 주 헤더 파일입니다.
//

#pragma once

#ifndef __AFXWIN_H__
	#error "PCH에 대해 이 파일을 포함하기 전에 'pch.h'를 포함합니다."
#endif

#include "resource.h"		// 주 기호입니다.
#include "Common/system/CSCSelfPatch/SCSelfPatch.h"
#include "Common/log/SCLog/SCLog.h"

//20260912 by claude. 실체는 SCDeskTools.cpp. logWrite() 매크로가 이 인스턴스를 통해 기록한다.
extern CSCLog gLog;


// CSCDeskToolsApp:
// 이 클래스의 구현에 대해서는 SCDeskTools.cpp을(를) 참조하세요.
//

class CSCDeskToolsApp : public CWinApp
{
public:
	CSCDeskToolsApp();
	~CSCDeskToolsApp();

	HANDLE		m_hMutex;

	//20260907 by claude. 자체 패치 — 시작 시 버전 검사 + 부팅 자동 실행 등록. 교체는 실행한 그 자리에서.
	//원리와 서버 구성은 Common/system/CSCSelfPatch/SCSelfPatch.h 주석 참조.
	CSCSelfPatch	m_self_patch;

// 재정의입니다.
public:
	virtual BOOL InitInstance();

// 구현입니다.

	DECLARE_MESSAGE_MAP()
	virtual int ExitInstance();
};

extern CSCDeskToolsApp theApp;
