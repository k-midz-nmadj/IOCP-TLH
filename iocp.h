/////////////////////////////////////////////////////////////////////////////
// iocp.h - IOCPサポートクラス
//
#pragma once

#include "thread.h"
#include <malloc.h>

#define MIN_KEEPALIVE 30000
#define DIFF_TIME(now, tm) (now >= tm ? now - (tm) : ((DWORD)(~0) - (tm)) + now)
#define TRUNC_WAIT_OBJECTS(n) (n < MAXIMUM_WAIT_OBJECTS ? n : MAXIMUM_WAIT_OBJECTS)

// IO完了イベント定義クラス
template <class THRD>	// THRD: スレッド実装クラス
class CIocpOverlapped
{
	template <class, int> friend class CIocpThread;
	friend THRD;
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

// IOCP用スレッド実装クラス
template <class THRD, int NEnt = 1>	// NEnt: 完了イベント1回で取得できる最大エントリ数
//template <class THRD, class TTPL>
class CIocpThread : public CAPCThread
{
	template <class> friend class CIocpThreadPool;
protected:
	typedef CIocpOverlapped<THRD> TOVL;	// イベントクラス
	typedef CIocpThreadPool<THRD> TTPL;	// スレッドプールクラス
	
	TTPL* m_pIocp;
	
	// 各スレッドイベントのデフォルト実装
	VOID OnCancelIO(TOVL* pIO)	// IO無効化
	{
	}
	VOID OnTimeout(DWORD dwCurrentTime)	// タイムアウト
	{
	}
	
	// IO完了イベント待機用メインスレッド
	static DWORD WINAPI MainProc(LPVOID pParam)
	{
		THRD* pThis = static_cast<THRD*>(pParam);
		TTPL* pIocp = pThis->m_pIocp;	// IOCPスレッドプール
		OVERLAPPED_ENTRY CPEntries[NEnt] = { 0 };	// (スレッド数増加=>エントリ数減少:16 / m_nThreadCnt + 1)
		INT iNumEntries;
		DWORD dwLastTime = ::GetTickCount();	// 最終更新時間
		
		while ((iNumEntries = pIocp->WaitForIocp(CPEntries, NEnt)) >= 0)	// 完了イベント待機
		{
			DWORD dwCurrentTime = ::GetTickCount();
			LPOVERLAPPED_ENTRY lpCPEntry = CPEntries + iNumEntries;
			
			while (--lpCPEntry >= CPEntries)	// 全エントリチェック
			{
				if (!lpCPEntry->lpOverlapped)
				{
					lpCPEntry->Internal = reinterpret_cast<ULONG_PTR>(pThis);
					pIocp->OnPostEvent(lpCPEntry);	// ユーザイベント発行
				}
				else if (lpCPEntry->lpCompletionKey)
				{	// イベントハンドラ取得
					TOVL* pIO = reinterpret_cast<TOVL*>(lpCPEntry->lpCompletionKey);
					
					pIO->m_dwLastTime = dwCurrentTime;
					pIO->m_pThread = pThis;
					if (!pIO->OnCompletionIO(lpCPEntry))	// IO完了イベント発行
						pThis->OnCancelIO(pIO);		// IOエラー時のイベント発行
				}
			}
			
			if (DIFF_TIME(dwCurrentTime, dwLastTime) >= pIocp->m_dwMinKeepAlive)	// タイムアウト判定
			{
				pThis->OnTimeout(dwCurrentTime);	// タイムアウトイベント発行
				dwLastTime = dwCurrentTime;
			}
		}
		return 0;	// (エラー終了時はTHRDオブジェクトが解放されるためアクセス禁止)
	}
public:
	CIocpThread(TTPL* pIocp = NULL)
	{
		m_pIocp = pIocp;
	}
	
	TTPL* GetIocp()	// IOCPスレッドプール取得
	{
		return m_pIocp;
	}
	
	// IOCPメインスレッド開始
	BOOL BeginThread(TTPL* pIocp, BOOL bSync = FALSE)
	{
		m_pIocp = pIocp;
		
		return (CreateThread(MainProc, this, bSync) != -1);
	}
};

// IOCP用スレッドプールクラス
template <class THRD>
//template <class THRD, TOVL>
class CIocpThreadPool
{
	template <class, int> friend class CIocpThread;
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
	
	// IO完了イベント待機(戻り値: 0以上=実行継続/-1=エラー終了)
	INT WaitForIocp(LPOVERLAPPED_ENTRY CPEntries, ULONG ulNumEntries)
	{
		if (!::GetQueuedCompletionStatusEx(
				m_hIocp,
				CPEntries, ulNumEntries, &ulNumEntries,
				m_dwMinKeepAlive, TRUE))	// APC有効化(Alert状態)で待機
		{
			switch (::GetLastError())	// エラー判定
			{
			case WAIT_IO_COMPLETION:	// APC queued
			case WAIT_TIMEOUT:			// Timeout
				return 0;
//			case ERROR_INVALID_HANDLE:	// Invalid Handle
//			case ERROR_ABANDONED_WAIT_0:// IOCP closed
			default:
				// IOCPハンドル解放によるエラー時、スレッド数をカウントダウン
				THRD* pThreads = m_pThreads;
				if (pThreads && ::InterlockedDecrement(&m_nThreadCnt) == 0)
					delete[] pThreads;	// 全終了でスレッド配列を解放
				
				return -1;
			}
		}
		return ulNumEntries;
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
			LPHANDLE phThreadPool = (LPHANDLE)alloca(sizeof(HANDLE) * nThreadNum);
			
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
	CIocpThreadPool(DWORD nThreadNum = 0)
	{
		m_hIocp = NULL;
		m_dwMinKeepAlive = INFINITE;
		
		m_nThreadNum = TRUNC_WAIT_OBJECTS(nThreadNum);
		m_nThreadCnt = 0;
		m_pThreads = NULL;
	}
	~CIocpThreadPool()
	{
		JoinThread(m_nThreadNum);	// スレッドプール終了待機
	}
	
	BOOL IsRunning()	// スレッド実行中:TRUE
	{
		return (m_nThreadCnt > 0);
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
	BOOL PostEvent(ULONG_PTR ulCompletionKey, DWORD dwIoSize = 0)
	{
		return ::PostQueuedCompletionStatus(m_hIocp, dwIoSize, ulCompletionKey, NULL);
	}
	
	// スレッドプールの実行開始(bSync: 同期/非同期)
	BOOL Start(DWORD nKeepAlive = MIN_KEEPALIVE, BOOL bSync = TRUE)
	{
		if (IsRunning() || !CreateIocp())	// IOCP未作成なら作成
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
			if (!m_pThreads[nThreadCnt].BeginThread(this))	// 各スレッドを非同期実行
			{	// 起動エラー時
				JoinThread(nThreadCnt);	// 実行スレッドがあれば終了待機
				return FALSE;
			}
		
		return (bSync ? m_pThreads[nThreadNum].BeginThread(this, TRUE) : TRUE);	// 同期時は終了まで待機
	}
	
	// スレッドプールの実行終了
	BOOL Stop()
	{
		HANDLE hIocp = ::InterlockedExchangePointer(&m_hIocp, NULL);	// IOCPハンドルのクリア判定により再入防止
		
		return (hIocp ? ::CloseHandle(hIocp) : FALSE);	// IOCP解放によりスレッド終了
	}
};
