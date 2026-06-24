/////////////////////////////////////////////////////////////////////////////
// iocp.h - IOCPサポートクラス
//
#pragma once

#include "thread.h"
#include <malloc.h>

#define DIFF_TIME(now, tm) (now >= tm ? now - (tm) : (DWORD)(0x100000000 - (tm)) + now)

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
class CIocpThreadT : public CAPCThread
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
	
	CIocpThreadT(TTPL* pIocp = NULL) : m_pIocp(pIocp)
	{
	}
public:
	TTPL* GetIocp()	// IOCPスレッドプール取得
	{
		return m_pIocp;
	}
};

class CIocpThread : public CIocpThreadT<CIocpThread>
{
};

// IOCP用スレッドプールクラス(THRD: スレッド実装クラス)
template <class THRD = CIocpThread>
class CIocpThreadPool
{
	friend THRD;
protected:
	typedef typename THRD::TOVL TOVL;
	
	HANDLE m_hIocp;			// IOCPハンドル
	DWORD m_dwMinWaitTime;	// 最小待機時間(ms)
	LONG m_nSync;			// スレッド起動状態(0:非同期/1:同期/-1:非同期時にスレッドプール内で停止)
	LONG m_nThreadNum;		// スレッド数
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
		DWORD dwCurrentTime, dwLastTime = ::GetTickCount();	// 最終更新時間
		DWORD dwError = -1;
		
		while (pThis->m_hIocp)	// IOCPハンドル有効時
		{
			ULONG ulNumEntries = 0;
			OVERLAPPED_ENTRY CPEntries[THRD::NEnt] = {};
			dwError = ::GetQueuedCompletionStatusEx(
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
			}
		}
		
		if (pThis->m_nSync < 1 && ::InterlockedDecrement(&pThis->m_nThreadNum) == 0 && 
			pThis->m_nSync < 0)	// 非同期でスレッドプール内から停止
		{
			pThis->m_nSync = 0;
			delete[] pThis->m_pThreads;	// 非同期時にスレッド配列を解放
			pThis->m_pThreads = NULL;
		}
		return dwError;
	}
	
	// スレッドプール終了待機(bSync: 同期/非同期)
	BOOL JoinThread(BOOL bSync = FALSE)
	{
		LONG nThreadNum = m_nThreadNum;	// 更新される前に退避
		HANDLE hIocp = ::InterlockedExchangePointer(&m_hIocp, NULL);
		
		if (hIocp)	// IOCPハンドルのクリア判定で再入防止(実行中スレッドから終了)
		{
			::CloseHandle(hIocp);	// IOCPによる待機中スレッドを解放
			if (!m_pThreads || m_nSync == 1)
				return TRUE;	// 同期時は待機無し
		}
		else if (!bSync || m_nSync < 1)	// 同期時は再入可能
			return FALSE;
		
		if (nThreadNum > 0 && m_pThreads->m_hThread)	// スレッドプール実行中
		{	// 待機用スレッドハンドルの配列作成
			DWORD dwCrtThreadId = ::GetCurrentThreadId();
			LPHANDLE phThreadPool = static_cast<LPHANDLE>(alloca(sizeof(HANDLE) * nThreadNum));
			
			for (LONG nTplCnt = 0; nTplCnt < nThreadNum; ++nTplCnt)
			{
				if (dwCrtThreadId == m_pThreads[nTplCnt].m_dwThreadID)
				{
					m_nSync = -1;
					return TRUE;	// スレッドプール内からの停止は待機無し
				}
				phThreadPool[nTplCnt] = m_pThreads[nTplCnt].m_hThread;	// 自スレッドは除外
			}
			// スレッドプール(自スレッドを除く)の終了待機
			::WaitForMultipleObjects(nThreadNum, phThreadPool, TRUE, INFINITE);
		}
		
		m_nThreadNum = m_nSync = 0;
		delete[] m_pThreads;	// 待機終了後にスレッド配列を解放
		m_pThreads = NULL;
		
		return TRUE;
	}
public:
	CIocpThreadPool(DWORD nThreadNum = 0) : 
		m_hIocp(NULL),
		m_dwMinWaitTime(INFINITE),
		m_nThreadNum(0),
		m_nSync(0),
		m_pThreads(NULL)
	{
		if (nThreadNum > 0)
			CreateThreadPool(nThreadNum);
	}
	~CIocpThreadPool()
	{
		JoinThread();	// スレッドプール終了待機
	}
	
	BOOL IsRunning()	// スレッド実行中判定
	{
		return (m_pThreads && m_pThreads->m_hThread);
	}
	
	LONG GetThreadCount()	// 実行スレッド数取得(終了待機中は無効)
	{
		return (m_hIocp ? m_nThreadNum + m_nSync : 0);
	}
	THRD* GetThread(LONG nThreadNum = 0)	// 実行スレッド取得
	{
		return (nThreadNum < (m_pThreads ? GetThreadCount() : CreateThreadPool()) ? 
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
		
		if (!m_hIocp)	// 初回か前回エラー時はIOCP作成
		{
			if (m_pThreads)
				return FALSE;	// スレッド起動中は作成無効
			
			hIo = (pIO ? *pIO : INVALID_HANDLE_VALUE);	// IOハンドルがあれば指定
			phIocp = &m_hIocp;
		}
		else if (pIO)	// 2回目以降はハンドル指定必須
			phIocp = &(hIo = *pIO);
		else
			return TRUE;	// ハンドル省略時は無視
		
		*phIocp = ::CreateIoCompletionPort(hIo, m_hIocp, reinterpret_cast<ULONG_PTR>(pIO), nThreadNum);
		
		return (*phIocp != NULL);
	}
	
	// スレッドプールの作成
	LONG CreateThreadPool(DWORD nThreadNum = 0, DWORD nSuspendCnt = 0)
	{
		if (m_pThreads && (m_pThreads->m_hThread || !JoinThread()) ||	// 再作成のため解放
			!CreateIocp(NULL, nThreadNum))	// IOCPがなければ作成
			return 0;	// スレッドプール実行中
		
		if (nThreadNum == 0)	// スレッド数が未設定ならCPU数をセット
		{
			SYSTEM_INFO systemInfo = {};
			::GetSystemInfo(&systemInfo);
			nThreadNum = systemInfo.dwNumberOfProcessors;
		}
		
		nThreadNum *= (nSuspendCnt + 1);	// 各スレッドのサスペンド回数でスレッド数倍増
		if (nThreadNum > MAXIMUM_WAIT_OBJECTS)	// スレッド数セット(最大64まで)
			nThreadNum = MAXIMUM_WAIT_OBJECTS;
		
		m_pThreads = new THRD[nThreadNum];	// スレッドプール作成
		if (!m_pThreads)
			return 0;
		
		m_nThreadNum = nThreadNum;
		for (DWORD nThreadCnt = 0; nThreadCnt < nThreadNum; ++nThreadCnt)
			m_pThreads[nThreadCnt].m_pIocp = this;	// 各スレッドからのIOCP参照
		
		return nThreadNum;
	}
	
	// スレッドプールの実行開始(bSync: 同期/非同期)
	BOOL Start(DWORD nWaitTime = INFINITE, BOOL bSync = TRUE)
	{
		// スレッドプールがなければ作成
		if (!m_pThreads && !CreateThreadPool() || m_pThreads->m_hThread)
			return FALSE;	// スレッドプール実行中
		
		// タイムアウト時間設定(最大タイムアウト値/2以上は無効)
		m_dwMinWaitTime = (nWaitTime <= INFINITE / 2 ? nWaitTime : INFINITE);
		m_nSync = (bSync != FALSE);
		m_nThreadNum -= m_nSync;	// 最終スレッドは同期(終了待機)に割当
		
		LONG nThreadCnt;
		for (nThreadCnt = 0; 	// 各スレッドを非同期実行
			 nThreadCnt < m_nThreadNum && m_pThreads[nThreadCnt].BeginThread(WaitForIocp) != -1;
			 ++nThreadCnt);
		
		m_nThreadNum = nThreadCnt;	// 実際のスレッド数に更新
		if (bSync)	// 同期実行(失敗時はステータスクリア)
			m_nSync = nThreadCnt = (m_pThreads[nThreadCnt].BeginThread(WaitForIocp, NULL, TRUE) != -1);
		else if (nThreadCnt > 0)	// 非同期実行
			return TRUE;
		
		return (JoinThread(bSync) && nThreadCnt);	// 実行スレッドがあれば終了待機
	}
	
	// スレッドプールの実行終了
	BOOL Stop()
	{
		return (IsRunning() ? JoinThread() : FALSE);	// スレッド実行中なら終了
	}
};
