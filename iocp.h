/////////////////////////////////////////////////////////////////////////////
// iocp.h - IOCP�T�|�[�g�N���X
//
#pragma once

#include "thread.h"
#include <malloc.h>

#define DIFF_TIME(now, tm) (now >= tm ? now - (tm) : (DWORD)(0x100000000 - (tm)) + now)
#define TRUNC_WAIT_OBJECTS(n) (n < MAXIMUM_WAIT_OBJECTS ? n : MAXIMUM_WAIT_OBJECTS)

// IO�����C�x���g��`�N���X(THRD: �X���b�h�����N���X)
template <class THRD = CAPCThread>
class CIocpOverlapped
{
	template <class> friend class CIocpThreadPool;
protected:
	OVERLAPPED m_ovl;
	DWORD m_dwLastTime;	// �C�x���g���������ŏI����(ms)
	THRD* m_pThread;	// �C�x���g�����X���b�h
	
	CIocpOverlapped() : m_ovl{}, m_dwLastTime(::GetTickCount()), m_pThread(NULL)
	{
	}
public:
	virtual BOOL OnCompletionIO(LPOVERLAPPED_ENTRY lpCPEntry) = 0;	// IO�����C�x���g
	virtual operator HANDLE() = 0;	// IOCP�p�̃n���h���擾
	
	THRD* GetThread()	// ���s�X���b�h�擾
	{
		return m_pThread;
	}
};

// IOCP�p�X���b�h�N���X(MaxEnt: �����C�x���g1��Ŏ擾�ł���ő�G���g����)
template <class THRD = CAPCThread, int MaxEnt = 1>
class CIocpThread : public CAPCThread
{
	template <class> friend class CIocpThreadPool;
protected:
	typedef CIocpOverlapped<THRD> TOVL;	// �C�x���g�N���X
	typedef CIocpThreadPool<THRD> TTPL;	// �X���b�h�v�[���N���X
	enum { NEnt = MaxEnt };
	
	TTPL* m_pIocp;	// ���L���X���b�h�v�[���ւ̎Q��
	
	// �e�X���b�h�C�x���g�̃f�t�H���g����
	VOID OnCancelIO(TOVL* pIO)	// IO������
	{
	}
	VOID OnTimeout(DWORD dwCurrentTime)	// �^�C���A�E�g
	{
	}
	
	CIocpThread(TTPL* pIocp = NULL) : m_pIocp(pIocp)
	{
	}
public:
	TTPL* GetIocp()	// IOCP�X���b�h�v�[���擾
	{
		return m_pIocp;
	}
};

// IOCP�p�X���b�h�v�[���N���X(THRD: �X���b�h�����N���X)
template <class THRD = CIocpThread<>>
class CIocpThreadPool
{
	friend THRD;
protected:
	typedef typename THRD::TOVL TOVL;
	
	HANDLE m_hIocp;			// IOCP�n���h��
	DWORD m_dwMinWaitTime;	// �ŏ��ҋ@����(ms)
	DWORD m_nThreadNum;		// �X���b�h��
	THRD* m_pThreads;		// �X���b�h�z��
	
	// ���[�U�C�x���g�̃f�t�H���g����
	virtual VOID OnPostEvent(LPOVERLAPPED_ENTRY lpCPEntry)
	{
	}
	
	// IO�����C�x���g�ҋ@�p���C���X���b�h
	static DWORD WINAPI WaitForIocp(LPVOID pParam)
	{
		THRD* pThread = static_cast<THRD*>(pParam);	// IOCP�X���b�h
		CIocpThreadPool* pThis = pThread->m_pIocp;	// �X���b�h�v�[��
		DWORD dwLastTime = ::GetTickCount();	// �ŏI�X�V����
		
		while (pThis->m_hIocp != INVALID_HANDLE_VALUE)	// IOCP�n���h���L����
		{
			ULONG ulNumEntries = 0;
			OVERLAPPED_ENTRY CPEntries[THRD::NEnt] = {};
			DWORD dwError = ::GetQueuedCompletionStatusEx(
								pThis->m_hIocp,
								CPEntries, THRD::NEnt, &ulNumEntries,
								pThis->m_dwMinWaitTime, TRUE),	// APC�L����(Alert���)�őҋ@
			      dwCurrentTime = ::GetTickCount();
			if (dwError)
			{
				for (ULONG i = 0; i < ulNumEntries; ++i)	// �S�G���g���`�F�b�N
				{
					LPOVERLAPPED_ENTRY lpCPEntry = &CPEntries[i];
					if (!lpCPEntry->lpOverlapped)
					{
						lpCPEntry->Internal = reinterpret_cast<ULONG_PTR>(pThread);
						pThis->OnPostEvent(lpCPEntry);	// ���[�U�C�x���g���s
					}
					else if (lpCPEntry->lpCompletionKey)
					{	// �C�x���g�n���h���擾
						TOVL* pIO = reinterpret_cast<TOVL*>(lpCPEntry->lpCompletionKey);
						
						pIO->m_dwLastTime = dwCurrentTime;
						pIO->m_pThread = pThread;
						if (!pIO->OnCompletionIO(lpCPEntry))	// IO�����C�x���g���s
							pThread->OnCancelIO(pIO);		// IO�G���[���̃C�x���g���s
					}
				}
				dwError = WAIT_IO_COMPLETION;	// ���펞���^�C���A�E�g����
			}
			else
				dwError = ::GetLastError();
			
			switch (dwError)	// �G���[����
			{
			case WAIT_IO_COMPLETION:	// APC���s���̓^�C���A�E�g����
				if (DIFF_TIME(dwCurrentTime, dwLastTime) <= pThis->m_dwMinWaitTime)
					break;
			case WAIT_TIMEOUT:	// �^�C���A�E�g�C�x���g���s
				pThread->OnTimeout(dwCurrentTime);
				dwLastTime = dwCurrentTime;
				break;
			default:	// ERROR_ABANDONED_WAIT_0,ERROR_INVALID_HANDLE
				return dwError;
			}
		}
		return 0;
	}
	
	// �X���b�h�v�[���I���ҋ@
	BOOL JoinThread(DWORD nThreadNum)
	{
		// IOCP�n���h���̃N���A����ōē��h�~(IOCP���č쐬�����\�����l�����ANULL�N���A��NG)
		HANDLE hIocp = ::InterlockedExchangePointer(&m_hIocp, INVALID_HANDLE_VALUE);
		if (hIocp != INVALID_HANDLE_VALUE && hIocp)
			::CloseHandle(hIocp);	// IOCP����ɂ��X���b�h�I��
		
		if (!m_pThreads || nThreadNum > m_nThreadNum)
			return FALSE;	// �X���b�h���쐬���͈͊O
		
		if (nThreadNum && m_pThreads->m_hThread)	// �X���b�h�v�[�����s��
		{
			DWORD dwCrtThreadId = ::GetCurrentThreadId();
			LPHANDLE phThreadPool = static_cast<LPHANDLE>(alloca(sizeof(HANDLE) * nThreadNum));
			
			// �ҋ@�p�X���b�h�n���h���̔z��쐬
			for (DWORD nThreadCnt = 0;  nThreadCnt < nThreadNum; ++nThreadCnt)
			{	// ���X���b�h�͏��O
				if (dwCrtThreadId != m_pThreads[nThreadCnt].m_dwThreadID)
					phThreadPool[nThreadCnt] = m_pThreads[nThreadCnt].m_hThread;
				else
					return TRUE;	// �X���b�h�v�[�����ŏI��
			}
			// �X���b�h�v�[���O�ŏI���Ȃ�ҋ@
			::WaitForMultipleObjects(nThreadNum, phThreadPool, TRUE, INFINITE);
		}
		delete[] m_pThreads;	// �S�I���ŃX���b�h�z������
		m_pThreads = NULL;
		
		return TRUE;
	}
public:
	CIocpThreadPool(DWORD nThreadNum = 0) : 
		m_hIocp(INVALID_HANDLE_VALUE),
		m_dwMinWaitTime(INFINITE),
		m_nThreadNum(TRUNC_WAIT_OBJECTS(nThreadNum)),
		m_pThreads(NULL)
	{
	}
	~CIocpThreadPool()
	{
		Stop();	// �X���b�h�v�[���I���ҋ@
	}
	
	BOOL IsRunning()	// �X���b�h���s������
	{
		return (m_pThreads && m_pThreads->m_hThread);
	}
	
	DWORD GetThreadCount()	// ���s�X���b�h���擾
	{
		return (m_pThreads ? m_nThreadNum : 0);
	}
	THRD* GetThread(DWORD nThreadNum = 0)	// ���s�X���b�h�擾
	{
		return ((m_pThreads || CreateThreadPool()) && nThreadNum < m_nThreadNum ? 
				&m_pThreads[nThreadNum] : NULL);
	}
	
	// ���[�U�C�x���g���s
	BOOL PostEvent(LPVOID lpCompletionKey, DWORD dwIoSize = 0)
	{
		return ::PostQueuedCompletionStatus(m_hIocp, dwIoSize, reinterpret_cast<ULONG_PTR>(lpCompletionKey), NULL);
	}
	
	// IOCP�̍쐬��IO�n���h���֘A�t��
	BOOL CreateIocp(TOVL* pIO = NULL, DWORD nThreadNum = 0)
	{
		HANDLE hIo, *phIocp;
		
		if (m_hIocp == INVALID_HANDLE_VALUE || !m_hIocp)	// ���񂩑O��G���[����IOCP�쐬
		{
			if (m_pThreads)
				return FALSE;	// �X���b�h�N�����͍쐬����
			
			hIo = (pIO ? *pIO : INVALID_HANDLE_VALUE);	// IO�n���h��������Ύw��
			phIocp = &(m_hIocp = NULL);
			
			if (nThreadNum > 0)	// 1�ȏ�Ȃ�X���b�h���Z�b�g(�ő�64�܂�)
				m_nThreadNum = TRUNC_WAIT_OBJECTS(nThreadNum);
		}
		else if (pIO)	// 2��ڈȍ~�̓n���h���w��K�{
			phIocp = &(hIo = *pIO);
		else
			return TRUE;	// �n���h���ȗ����͖���
		
		*phIocp = ::CreateIoCompletionPort(hIo, m_hIocp, reinterpret_cast<ULONG_PTR>(pIO), m_nThreadNum);
		return (*phIocp != NULL);
	}
	
	// �X���b�h�v�[���̍쐬
	BOOL CreateThreadPool(DWORD nThreadNum = 0)
	{
		if (m_pThreads || !CreateIocp(NULL, nThreadNum))	// IOCP���Ȃ���΍쐬
			return FALSE;	// �X���b�h�v�[�����s��
		
		if (m_nThreadNum == 0)	// �X���b�h�������ݒ�Ȃ�CPU�����Z�b�g
		{
			SYSTEM_INFO systemInfo = {};
			::GetSystemInfo(&systemInfo);
			m_nThreadNum = systemInfo.dwNumberOfProcessors;
		}
		
		m_pThreads = new THRD[m_nThreadNum];	// �X���b�h�v�[���쐬
		if (!m_pThreads)
			return FALSE;
		
		for (DWORD nThreadCnt = 0; nThreadCnt < m_nThreadNum; ++nThreadCnt)
			m_pThreads[nThreadCnt].m_pIocp = this;	// �e�X���b�h�����IOCP�Q��
		
		return TRUE;
	}
	
	// �X���b�h�v�[���̎��s�J�n(bSync: ����/�񓯊�)
	BOOL Start(DWORD nWaitTime = INFINITE, BOOL bSync = TRUE)
	{
		if (!m_pThreads && !CreateThreadPool() || m_pThreads->m_hThread)	// �X���b�h�v�[�����Ȃ���΍쐬
			return FALSE;	// �X���b�h�v�[�����s��
		
		// �^�C���A�E�g���Ԑݒ�(�ő�^�C���A�E�g�l/2�ȏ�͖���)
		m_dwMinWaitTime = (nWaitTime <= INFINITE / 2 ? nWaitTime : INFINITE);
		
		DWORD nThreadCnt, nThreadNum = m_nThreadNum - bSync;	// �ŏI�X���b�h�͓���(�I���ҋ@)�Ɋ���
		for (nThreadCnt = 0; 	// �e�X���b�h��񓯊����s
			 nThreadCnt < nThreadNum && m_pThreads[nThreadCnt].BeginThread(WaitForIocp) != -1;
			 ++nThreadCnt);
		
		nThreadNum = (nThreadCnt == nThreadNum);
		if (nThreadNum) 	// �S�X���b�h�N������
		{
			if (bSync)	// �������s���͑ҋ@
				nThreadNum = (m_pThreads[nThreadCnt].BeginThread(WaitForIocp, NULL, TRUE) != -1);
			else
				return TRUE;	// �񓯊����s
		}
		JoinThread(nThreadCnt);	// ���s�X���b�h������ΏI���ҋ@
		
		return nThreadNum;
	}
	
	// �X���b�h�v�[���̎��s�I��
	BOOL Stop()
	{
		return JoinThread(m_nThreadNum);	// �X���b�h�v�[���O�Ȃ�I���܂őҋ@
	}
};
