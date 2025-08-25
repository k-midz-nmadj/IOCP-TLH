/////////////////////////////////////////////////////////////////////////////
// thread.h - スレッドサポートクラス
//
#pragma once

#if defined(_M_IX86)
#define _X86_
#include <windef.h>
#include <winbase.h>
#elif defined(_M_AMD64)
#define _AMD64_
#include <windef.h>
#include <winbase.h>
#else
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
//#include <process.h>

// APC対応スレッドクラス
template <bool t_bManaged = true>	// t_bManaged: デストラクタによるスレッドハンドル解放の有無
class CAPCThreadT
{
protected:
	HANDLE m_hThread;	// スレッドハンドル
	DWORD m_dwThreadID;	// スレッドID
	
public:
	CAPCThreadT() : m_hThread(NULL), m_dwThreadID(0)
	{
	}
	~CAPCThreadT()
	{
		if (t_bManaged)
			BindThread();	// スレッドハンドル解放
	}
	
	// 関数とパラメータ指定によるスレッド起動(bSync=TRUE: 関数を同期呼出し)
	DWORD BeginThread(LPTHREAD_START_ROUTINE pfnThread, LPVOID pParam = NULL, BOOL bSync = FALSE)
	{
		if (m_hThread)
			return -1;	// スレッド作成済み
		if (!pParam)
			pParam = this;	// パラメータ省略時
		
		if (bSync)	// 同期有効
		{
			m_dwThreadID = ::GetCurrentThreadId();
			m_hThread = ::OpenThread(THREAD_ALL_ACCESS, FALSE, m_dwThreadID);
			if (m_hThread)
				return pfnThread(pParam);	// 関数終了までブロック
		}
		else
		{
#ifdef _INC_PROCESS
			m_hThread = (HANDLE)::_beginthreadex(NULL, 0, (_beginthreadex_proc_type)pfnThread, pParam, 
														CREATE_SUSPENDED, (UINT*)&m_dwThreadID);
#else
			m_hThread = ::CreateThread(NULL, 0, pfnThread, pParam, CREATE_SUSPENDED, &m_dwThreadID);
#endif
			if (m_hThread)
				return ::ResumeThread(m_hThread);	// ハンドルとIDを取得してから起動
		}
		return -1;	// 起動エラー
	}
	
	// 別スレッドからの外部ハンドル割当(パラメータ省略時はハンドル解放)
	DWORD BindThread(const CAPCThreadT* pThread = NULL)
	{
		DWORD dwThreadID = m_dwThreadID;
		
		if (m_hThread && dwThreadID)	// 外部ハンドル割当時(ID=0)は解放なし
		{
			::CloseHandle(m_hThread);	// 既存のスレッドハンドル解放
			m_dwThreadID = 0;
		}
		m_hThread = (pThread ? pThread->m_hThread :  NULL);
		
		return dwThreadID;
	}
	
	// APCによる関数の非同期実行
	BOOL QueueAPC(PAPCFUNC pfnAPC, LPVOID pParam)
	{
		if (m_hThread && ::GetCurrentThreadId() != m_dwThreadID)
			return (::QueueUserAPC(pfnAPC, m_hThread, reinterpret_cast<ULONG_PTR>(pParam)) != 0);
		
		pfnAPC(reinterpret_cast<ULONG_PTR>(pParam));	// 同一スレッドでの実行時は直接呼出し
		return TRUE;
	}
	
	// APCによる関数実行まで待機
	BOOL WaitForAPC(DWORD dwMilliseconds = INFINITE)
	{
		return (m_hThread ? 
			::WaitForSingleObjectEx(m_hThread, dwMilliseconds, TRUE) == WAIT_IO_COMPLETION : FALSE);
	}
};

typedef CAPCThreadT<true> CAPCThread;
