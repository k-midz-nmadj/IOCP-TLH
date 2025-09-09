/////////////////////////////////////////////////////////////////////////////
// thread.h - �X���b�h�T�|�[�g�N���X
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

// APC�Ή��X���b�h�N���X
template <bool t_bManaged = true>	// t_bManaged: �f�X�g���N�^�ɂ��X���b�h�n���h������̗L��
class CAPCThreadT
{
protected:
	HANDLE m_hThread;	// �X���b�h�n���h��
	DWORD m_dwThreadID;	// �X���b�hID
	
public:
	CAPCThreadT() : m_hThread(NULL), m_dwThreadID(0)
	{
	}
	~CAPCThreadT()
	{
		if (t_bManaged)
			BindThread();	// �X���b�h�n���h�����
	}
	
	// �֐��ƃp�����[�^�w��ɂ��X���b�h�N��(bSync=TRUE: �֐��𓯊��ďo��)
	DWORD BeginThread(LPTHREAD_START_ROUTINE pfnThread, LPVOID pParam = NULL, BOOL bSync = FALSE)
	{
		if (m_hThread)
			return -1;	// �X���b�h�쐬�ς�
		if (!pParam)
			pParam = this;	// �p�����[�^�ȗ���
		
		if (bSync)	// �����L��
		{
			m_dwThreadID = ::GetCurrentThreadId();
			m_hThread = ::OpenThread(THREAD_ALL_ACCESS, FALSE, m_dwThreadID);
			if (m_hThread)
				return pfnThread(pParam);	// �֐��I���܂Ńu���b�N
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
				return ::ResumeThread(m_hThread);	// �n���h����ID���擾���Ă���N��
		}
		return -1;	// �N���G���[
	}
	
	// �ʃX���b�h����̊O���n���h������(�p�����[�^�ȗ����̓n���h�����)
	DWORD BindThread(const CAPCThreadT* pThread = NULL)
	{
		DWORD dwThreadID = m_dwThreadID;
		
		if (m_hThread && dwThreadID)	// �O���n���h��������(ID=0)�͉���Ȃ�
		{
			::CloseHandle(m_hThread);	// �����̃X���b�h�n���h�����
			m_dwThreadID = 0;
		}
		m_hThread = (pThread ? pThread->m_hThread :  NULL);
		
		return dwThreadID;
	}
	
	// APC�ɂ��֐��̔񓯊����s
	BOOL QueueAPC(PAPCFUNC pfnAPC, LPVOID pParam)
	{
		if (m_hThread && ::GetCurrentThreadId() != m_dwThreadID)
			return (::QueueUserAPC(pfnAPC, m_hThread, reinterpret_cast<ULONG_PTR>(pParam)) != 0);
		
		pfnAPC(reinterpret_cast<ULONG_PTR>(pParam));	// ����X���b�h�ł̎��s���͒��ڌďo��
		return TRUE;
	}
	
	// APC�ɂ��֐����s�܂őҋ@
	BOOL WaitForAPC(DWORD dwMilliseconds = INFINITE)
	{
		return (m_hThread ? 
			::WaitForSingleObjectEx(m_hThread, dwMilliseconds, TRUE) == WAIT_IO_COMPLETION : FALSE);
	}
};

typedef CAPCThreadT<true> CAPCThread;
