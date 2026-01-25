/////////////////////////////////////////////////////////////////////////////
// iocp.h - IOCPサポートクラス
//
#pragma once

#include "thread.h"
#include <malloc.h>

#define DIFF_TIME(now, tm) (now >= tm ? now - (tm) : (DWORD)(0x100000000 - (tm)) + now)
#define TRUNC_WAIT_OBJECTS(n) (n < MAXIMUM_WAIT_OBJECTS ? n : MAXIMUM_WAIT_OBJECTS)

// IO完了イベント定義クラス(THRD: スレッド実装クラス)
template <class THRD = CAPCThread>
class CIocpOverlapped
{
	template <class> friend class CIocpThreadPool;
protected:
	OVERLAPPED m_ovl;
	DWORD m_dwLastTime;	// イベント発生した最終時間(ms)
	THRD* m_pThread;	// イベント発生スレッド
	
	CIocpOverlapped() : m_ovl{}, m_dwLastTime(::GetTickCount()), m_pThread(NULL)
	{
	}
public:
	virtual BOOL OnCompletionIO(LPOVERLAPPED_ENTRY lpCPEntry) = 0;	// IO完了イベント
	virtual operator HANDLE() = 0;	// IOCP用のハンドル取得
	
	THRD* GetThread()	// 現行スレッド取得
	{
		return m_pThread;
	}
};

// IOCP用スレッドクラス(MaxEnt: 完了イベント1回で取得できる最大エントリ数)
template <class THRD = CAPCThread, int MaxEnt = 1>
class CIocpThread : public CAPCThread
{
	template <class> friend class CIocpThreadPool;
protected:
	typedef CIocpOverlapped<THRD> TOVL;	// イベントクラス
	typedef CIocpThreadPool<THRD> TTPL;	// スレッドプールクラス
	enum { NEnt = MaxEnt };
	
	TTPL* m_pIocp;	// 所有元スレッドプールへの参照
	
	// 各スレッドイベントのデフォルト実装
	VOID OnCancelIO(TOVL* pIO)	// IO無効化
	{
	}
	VOID OnTimeout(DWORD dwCurrentTime)	// タイムアウト
	{
	}
	
	CIocpThread(TTPL* pIocp = NULL) : m_pIocp(pIocp)
	{
	}
public:
	TTPL* GetIocp()	// IOCPスレッドプール取得
	{
		return m_pIocp;
	}
};

// IOCP用スレッドプールクラス(THRD: スレッド実装クラス)
template <class THRD = CIocpThread<>>
class CIocpThreadPool
{
	friend THRD;
protected:
	typedef typename THRD::TOVL TOVL;
	
	HANDLE m_hIocp;			// IOCPハンドル
	DWORD m_dwMinWaitTime;	// 最小待機時間(ms)
	DWORD m_nThreadNum;		// スレッド数
	LONG  m_nThreadCnt;		// スレッド終了カウンタ
	THRD* m_pThreads;		// スレッド配列
	
	// ユーザイベントのデフォルト実装
	virtual VOID OnPostEvent(LPOVERLAPPED_ENTRY lpCPEntry)
	{
	}
	
	// IO完了イベント待機用メインスレッド
	static DWORD WINAPI WaitForIocp(LPVOID pParam)
	{
		THRD* pThread = static_cast<THRD*>(pParam);	// IOCPスレッド
		CIocpThreadPool* pThis = pThread->m_pIocp;	// スレッドプール
		DWORD dwLastTime = ::GetTickCount();	// 最終更新時間
		
		while (pThis->m_hIocp != INVALID_HANDLE_VALUE)	// IOCPハンドル有効時
		{
			ULONG ulNumEntries = 0;
			OVERLAPPED_ENTRY CPEntries[THRD::NEnt] = {};
			DWORD dwError = ::GetQueuedCompletionStatusEx(
								pThis->m_hIocp,
								CPEntries, THRD::NEnt, &ulNumEntries,
								pThis->m_dwMinWaitTime, TRUE),	// APC有効化(Alert状態)で待機
			      dwCurrentTime = ::GetTickCount();
			if (dwError)
			{
				for (ULONG i = 0; i < ulNumEntries; ++i)	// 全エントリチェック
				{
					LPOVERLAPPED_ENTRY lpCPEntry = &CPEntries[i];
					if (!lpCPEntry->lpOverlapped)
					{
						lpCPEntry->Internal = reinterpret_cast<ULONG_PTR>(pThread);
						pThis->OnPostEvent(lpCPEntry);	// ユーザイベント発行
					}
					else if (lpCPEntry->lpCompletionKey)
					{	// イベントハンドラ取得
						TOVL* pIO = reinterpret_cast<TOVL*>(lpCPEntry->lpCompletionKey);
						
						pIO->m_dwLastTime = dwCurrentTime;
						pIO->m_pThread = pThread;
						if (!pIO->OnCompletionIO(lpCPEntry))	// IO完了イベント発行
							pThread->OnCancelIO(pIO);		// IOエラー時のイベント発行
					}
				}
				dwError = WAIT_IO_COMPLETION;	// 正常時もタイムアウト判定
			}
			else
				dwError = ::GetLastError();
			
			switch (dwError)	// エラー判定(ERROR_ABANDONED_WAIT_0,ERROR_INVALID_HANDLE)
			{
			case WAIT_IO_COMPLETION:	// APC実行時はタイムアウト判定
				if (DIFF_TIME(dwCurrentTime, dwLastTime) <= pThis->m_dwMinWaitTime)
					break;
			case WAIT_TIMEOUT:	// タイムアウトイベント発行
				pThread->OnTimeout(dwCurrentTime);
				dwLastTime = dwCurrentTime;
				break;
			default:	// ERROR_ABANDONED_WAIT_0,ERROR_INVALID_HANDLE
				return dwError;
			}
		}
		return 0;
	}
	
	// スレッドプール終了待機
	BOOL JoinThread(DWORD nThreadNum)
	{
		// IOCPハンドルのクリア判定で再入防止(IOCPが再作成される可能性を考慮し、NULLクリアはNG)
		HANDLE hIocp = ::InterlockedExchangePointer(&m_hIocp, INVALID_HANDLE_VALUE);
		if (hIocp != INVALID_HANDLE_VALUE && hIocp)
			::CloseHandle(hIocp);	// IOCP解放によりスレッド終了
		
		if (nThreadNum > m_nThreadNum || ::InterlockedExchange(&m_nThreadCnt, 0) <= 0)
			return FALSE;	// スレッド未作成か範囲外
		
		if (nThreadNum && m_pThreads->m_hThread)	// スレッドプール実行中
		{
			DWORD nThreadCnt, nTplCnt;
			DWORD dwCrtThreadId = ::GetCurrentThreadId();
			LPHANDLE phThreadPool = static_cast<LPHANDLE>(alloca(sizeof(HANDLE) * nThreadNum));
			
			// 待機用スレッドハンドルの配列作成
			for (nThreadCnt = nTplCnt = 0; nThreadCnt < nThreadNum; ++nThreadCnt)
				if (dwCrtThreadId != m_pThreads[nThreadCnt].m_dwThreadID)	// 自スレッドは除外
					phThreadPool[nTplCnt++] = m_pThreads[nThreadCnt].m_hThread;
			
			// スレッドプール(自スレッドを除く)の終了待機
			if (nTplCnt > 0)
				::WaitForMultipleObjects(nTplCnt, phThreadPool, TRUE, INFINITE);
		}
		delete[] m_pThreads;	// 全終了でスレッド配列を解放
		
		return TRUE;
	}
public:
	CIocpThreadPool(DWORD nThreadNum = 0) : 
		m_hIocp(INVALID_HANDLE_VALUE),
		m_dwMinWaitTime(INFINITE),
		m_nThreadNum(TRUNC_WAIT_OBJECTS(nThreadNum)),
		m_nThreadCnt(0),
		m_pThreads(NULL)
	{
	}
	~CIocpThreadPool()
	{
		Stop();	// スレッドプール終了待機
	}
	
	BOOL IsRunning()	// スレッド実行中判定
	{
		return (m_nThreadCnt > 0 && m_pThreads->m_hThread);
	}
	
	DWORD GetThreadCount()	// 実行スレッド数取得
	{
		return (m_nThreadCnt > 0 ? m_nThreadNum : 0);
	}
	THRD* GetThread(DWORD nThreadNum = 0)	// 実行スレッド取得
	{
		return ((m_nThreadCnt > 0 || CreateThreadPool()) && nThreadNum < m_nThreadNum ? 
				&m_pThreads[nThreadNum] : NULL);
	}
	
	// ユーザイベント発行
	BOOL PostEvent(LPVOID lpCompletionKey, DWORD dwIoSize = 0)
	{
		return ::PostQueuedCompletionStatus(m_hIocp, dwIoSize, reinterpret_cast<ULONG_PTR>(lpCompletionKey), NULL);
	}
	
	// IOCPの作成とIOハンドル関連付け
	BOOL CreateIocp(TOVL* pIO = NULL, DWORD nThreadNum = 0)
	{
		HANDLE hIo, *phIocp;
		
		if (m_hIocp == INVALID_HANDLE_VALUE || !m_hIocp)	// 初回か前回エラー時はIOCP作成
		{
			if (m_nThreadCnt)
				return FALSE;	// スレッド起動中は作成無効
			
			hIo = (pIO ? *pIO : INVALID_HANDLE_VALUE);	// IOハンドルがあれば指定
			phIocp = &(m_hIocp = NULL);
			
			if (nThreadNum > 0)	// 1以上ならスレッド数セット(最大64まで)
				m_nThreadNum = TRUNC_WAIT_OBJECTS(nThreadNum);
		}
		else if (pIO)	// 2回目以降はハンドル指定必須
			phIocp = &(hIo = *pIO);
		else
			return TRUE;	// ハンドル省略時は無視
		
		*phIocp = ::CreateIoCompletionPort(hIo, m_hIocp, reinterpret_cast<ULONG_PTR>(pIO), m_nThreadNum);
		return (*phIocp != NULL);
	}
	
	// スレッドプールの作成
	BOOL CreateThreadPool(DWORD nThreadNum = 0)
	{
		if (m_nThreadCnt || !CreateIocp(NULL, nThreadNum))	// IOCPがなければ作成
			return FALSE;	// スレッドプール実行中
		
		if (m_nThreadNum == 0)	// スレッド数が未設定ならCPU数をセット
		{
			SYSTEM_INFO systemInfo = {};
			::GetSystemInfo(&systemInfo);
			m_nThreadNum = systemInfo.dwNumberOfProcessors;
		}
		
		m_pThreads = new THRD[m_nThreadNum];	// スレッドプール作成
		if (!m_pThreads)
			return FALSE;
		
		m_nThreadCnt = m_nThreadNum;	// スレッドカウンタ初期化
		for (DWORD nThreadCnt = 0; nThreadCnt < m_nThreadNum; ++nThreadCnt)
			m_pThreads[nThreadCnt].m_pIocp = this;	// 各スレッドからのIOCP参照
		
		return TRUE;
	}
	
	// スレッドプールの実行開始(bSync: 同期/非同期)
	BOOL Start(DWORD nWaitTime = INFINITE, BOOL bSync = TRUE)
	{
		if (m_nThreadCnt <= 0 && !CreateThreadPool() || m_pThreads->m_hThread)	// スレッドプールがなければ作成
			return FALSE;	// スレッドプール実行中
		
		// タイムアウト時間設定(最大タイムアウト値/2以上は無効)
		m_dwMinWaitTime = (nWaitTime <= INFINITE / 2 ? nWaitTime : INFINITE);
		
		DWORD nThreadCnt, nThreadNum = m_nThreadNum - (bSync != 0);	// 最終スレッドは同期(終了待機)に割当
		for (nThreadCnt = 0; 	// 各スレッドを非同期実行
			 nThreadCnt < nThreadNum && m_pThreads[nThreadCnt].BeginThread(WaitForIocp) != -1;
			 ++nThreadCnt);
		
		nThreadNum = (nThreadCnt == nThreadNum);
		if (nThreadNum) 	// 全スレッド起動成功
		{
			if (bSync)	// 同期実行時は待機
				nThreadNum = (m_pThreads[nThreadCnt].BeginThread(WaitForIocp, NULL, TRUE) != -1);
			else
				return TRUE;	// 非同期実行
		}
		JoinThread(nThreadCnt);	// 実行スレッドがあれば終了待機
		
		return nThreadNum;
	}
	
	// スレッドプールの実行終了
	BOOL Stop()
	{
		return JoinThread(m_nThreadNum);	// スレッドプール外なら終了まで待機
	}
};
