/////////////////////////////////////////////////////////////////////////////
// iocp.h - IOCPサポートクラス
//
#pragma once

#include "thread.h"
#include <malloc.h>

#define DIFF_TIME(now, tm) (now >= tm ? now - (tm) : (DWORD)(0x100000000 - (tm)) + now)
#define TRUNC_WAIT_OBJECTS(n) (n < MAXIMUM_WAIT_OBJECTS ? n : MAXIMUM_WAIT_OBJECTS)

// IO完了イベント定義クラス
template <class THRD = CAPCThread>	// THRD: スレッド実装クラス
class CIocpOverlapped
{
	template <class, int> friend class CIocpThreadPool;
protected:
	OVERLAPPED m_ovl;
	DWORD m_dwLastTime;	// イベント発生した最終時間(ms)
	THRD* m_pThread;	// イベント発生スレッド
	
	CIocpOverlapped()
	{
		::ZeroMemory(&m_ovl, sizeof(m_ovl));
		
		m_dwLastTime = ::GetTickCount();
		m_pThread = NULL;
	}
public:
	virtual BOOL OnCompletionIO(LPOVERLAPPED_ENTRY lpCPEntry) = 0;	// IO完了イベント
	virtual operator HANDLE() = 0;	// IOCP用のハンドル取得
};

// IOCP用スレッドクラス
template <class THRD = CAPCThread, int NEnt = 1>
class CIocpThread : public CAPCThread
{
	template <class, int> friend class CIocpThreadPool;
protected:
	typedef CIocpOverlapped<THRD> TOVL;	// イベントクラス
	typedef CIocpThreadPool<THRD, NEnt> TTPL;	// スレッドプールクラス
	
	TTPL* m_pIocp;	// 所有元スレッドプールへの参照
	
	// 各スレッドイベントのデフォルト実装
	VOID OnCancelIO(TOVL* pIO)	// IO無効化
	{
	}
	VOID OnTimeout(DWORD dwCurrentTime)	// タイムアウト
	{
	}
	
public:
	CIocpThread(TTPL* pIocp = NULL) : m_pIocp(pIocp)
	{
	}
	
	TTPL* GetIocp()	// IOCPスレッドプール取得
	{
		return m_pIocp;
	}
};

// IOCP用スレッドプールクラス(NEnt: 完了イベント1回で取得できる最大エントリ数)
template <class THRD = CIocpThread<>, int NEnt = 1>
class CIocpThreadPool
{
	friend THRD;
protected:
	typedef typename THRD::TOVL TOVL;
	
	HANDLE m_hIocp;			// IOCPハンドル
	DWORD m_dwMinKeepAlive;	// 最小接続維持時間(ms)
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
		
		while (pThis->m_hIocp)	// IOCPハンドル有効時
		{
			ULONG ulNumEntries = 0;
			OVERLAPPED_ENTRY CPEntries[NEnt] = {0};
			DWORD dwError = ::GetQueuedCompletionStatusEx(
								pThis->m_hIocp,
								CPEntries, NEnt, &ulNumEntries,
								pThis->m_dwMinKeepAlive, TRUE),	// APC有効化(Alert状態)で待機
			      dwCurrentTime = ::GetTickCount();
			if (dwError)
			{
				LPOVERLAPPED_ENTRY lpCPEntry = CPEntries + ulNumEntries;
				while (--lpCPEntry >= CPEntries)	// 全エントリチェック
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
				
				dwError = WAIT_IO_COMPLETION;	// 正常時もタイムアウト判定
			}
			else
				dwError = ::GetLastError();
			
			switch (dwError)	// エラー判定(ERROR_ABANDONED_WAIT_0: IOCP closed)
			{
			case WAIT_IO_COMPLETION:	// APC実行時はタイムアウト判定
				if (DIFF_TIME(dwCurrentTime, dwLastTime) <= pThis->m_dwMinKeepAlive)
					break;
			case WAIT_TIMEOUT:	// タイムアウトイベント発行
				pThread->OnTimeout(dwCurrentTime);
				dwLastTime = dwCurrentTime;
			}
		}
		// IOCPハンドル解放による終了時、スレッド数をカウントダウン
		pThread = pThis->m_pThreads;
		if (pThread && ::InterlockedDecrement(&pThis->m_nThreadCnt) == 0)
			delete[] pThread;	// 全終了でスレッド配列を解放
		
		return dwLastTime;	// 待機終了
	}
	
	// IOCPメインスレッド開始
	BOOL BeginThread(THRD& thread, BOOL bSync = FALSE)
	{
		thread.m_pIocp = this;
		
		return (thread.CreateThread(WaitForIocp, &thread, bSync) != -1);
	}
	
	// スレッドプール終了待機
	BOOL JoinThread(DWORD nThreadNum)
	{
		Stop();	// スレッド停止⇒終了判定
		if (nThreadNum > m_nThreadNum || ::InterlockedExchange(&m_nThreadCnt, 0) <= 0)
			return FALSE;
		
		if (nThreadNum > 0)	// 起動エラー時は待機せず解放のみ
		{
			DWORD nThreadCnt, nTplCnt;
			DWORD dwCrtThreadId = ::GetCurrentThreadId();
			LPHANDLE phThreadPool = static_cast<LPHANDLE>(alloca(sizeof(HANDLE) * nThreadNum));
			
			// 待機用スレッドハンドルの配列作成
			for (nThreadCnt = nTplCnt = 0; nThreadCnt < nThreadNum ; ++nThreadCnt)
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
		m_hIocp(NULL),
		m_dwMinKeepAlive(INFINITE),
		m_nThreadNum(TRUNC_WAIT_OBJECTS(nThreadNum)),
		m_nThreadCnt(0),
		m_pThreads(NULL)
	{
	}
	~CIocpThreadPool()
	{
		JoinThread(m_nThreadNum);	// スレッドプール終了待機
	}
	
	DWORD GetThreadCount()	// 実行スレッド数取得
	{
		return (m_nThreadCnt > 0 ? m_nThreadCnt : 0);
	}
	
	// IOCPの作成とIOハンドル関連付け
	BOOL CreateIocp(TOVL* pIO = NULL, DWORD nThreadNum = 0)
	{
		HANDLE hIo, *phIocp;
		
		if (!m_hIocp)	// 初回時はIOCP作成
		{
			hIo = (pIO ? *pIO : INVALID_HANDLE_VALUE);	// IOハンドルがあれば指定
			phIocp = &m_hIocp;
			
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
	
	// ユーザイベント発行
	BOOL PostEvent(LPVOID lpCompletionKey, DWORD dwIoSize = 0)
	{
		return ::PostQueuedCompletionStatus(m_hIocp, dwIoSize, reinterpret_cast<ULONG_PTR>(lpCompletionKey), NULL);
	}
	
	// スレッドプールの実行開始(bSync: 同期/非同期)
	BOOL Start(DWORD nKeepAlive = INFINITE, BOOL bSync = TRUE)
	{
		if (m_nThreadCnt > 0 || !CreateIocp())	// IOCP未作成なら作成
			return FALSE;	// スレッドプール実行中
		
		if (m_nThreadNum == 0)	// スレッド数が未設定ならCPU数をセット
		{
			SYSTEM_INFO systemInfo = { 0 };
			::GetSystemInfo(&systemInfo);
			m_nThreadNum = systemInfo.dwNumberOfProcessors;
		}
		
		// スレッドプール作成(同時にSuspendするスレッドの最大数を加算: m_nThreadNum += N)
		m_pThreads = new THRD[m_nThreadNum];
		if (!m_pThreads)
			return FALSE;
		
		// タイムアウト時間設定(最大タイムアウト値/2以上は無効)
		m_dwMinKeepAlive = (nKeepAlive <= INFINITE / 2 ? nKeepAlive : INFINITE);
		m_nThreadCnt = m_nThreadNum;	// スレッドカウンタ初期化
		
		DWORD nThreadCnt, nThreadNum = m_nThreadNum - bSync;	// 最終スレッドは同期(終了待機)に割当
		for (nThreadCnt = 0; nThreadCnt < nThreadNum; ++nThreadCnt)
			if (!BeginThread(m_pThreads[nThreadCnt]))	// 各スレッドを非同期実行
			{	// 起動エラー時
				JoinThread(nThreadCnt);	// 実行スレッドがあれば終了待機
				return FALSE;
			}
		
		return (bSync ? BeginThread(m_pThreads[nThreadNum], TRUE) : TRUE);	// 同期時は終了まで待機
	}
	
	// スレッドプールの実行終了
	BOOL Stop()
	{
		HANDLE hIocp = ::InterlockedExchangePointer(&m_hIocp, NULL);	// IOCPハンドルのクリア判定で再入防止
		
		return (hIocp ? ::CloseHandle(hIocp) : FALSE);	// IOCP解放によりスレッド終了
	}
};
