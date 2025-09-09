/////////////////////////////////////////////////////////////////////////////
// IocpServer.h - IOCP�N���X�̃\�P�b�g�g��
//
// [�p���֌W]
// CIocpOverlapped <= 
// CSocketBase     <= CIocpSocket
// CAPCThread      <= CIocpThread  <= CTLHeap <= CSocketFactory
// CIocpThreadPool <= CIocpServer
// 
// [��܊֌W]
// CIocpThreadPool<CSocketFactory> *- CSocketFactory +- CTLHList<CIocpSocket> *- CIocpSocket
// 
// [�Q�Ɗ֌W]
// CIocpServer <- CSocketFactory <- CIocpSocket
// 
#pragma once

#include "iocp.h"
#include "tlheap.h"
#include "sockbase.h"

class CSocketFactory;	// �\�P�b�g�쐬�N���X�錾

typedef CIocpOverlapped<CSocketFactory> CSocketOverlapped;	// �N���C�A���g�N���X
typedef CIocpThread<CSocketFactory>     CSocketThread;		// �X���b�h�N���X
typedef CIocpThreadPool<CSocketFactory> CIocpServerBase;	// �T�[�o�N���X

// IOCP�p�\�P�b�g�N���X
class CIocpSocket : public CSocketOverlapped, public CSocketBase
{
	friend class CSocketFactory;
protected:
	enum WSA_IOEVENT	// �\�P�b�g�C�x���gID
	{
		IOInit = 0,	// �������
		IOSuspend,	// �ꎞ��~
		IOAccept,	// �ڑ�����҂�
		IOConnect,	// �ڑ������҂�
		IOReceive,	// ��M�����҂�
		IOSend,		// ���M�����҂�
	} m_ioState;
	
	LPWSABUF m_pWsaBuf;	// ����M�ۗ��f�[�^
	
	CIocpSocket();
	~CIocpSocket();
	
	BOOL OnCompletionIO(LPOVERLAPPED_ENTRY lpCPEntry);	// IO�����C�x���g
	
	virtual BOOL OnAccept(const CSockAddrIn* pAddrRemote = NULL);	// �ڑ�����C�x���g
	virtual BOOL OnConnect(DWORD dwError = 0);	// �ڑ������C�x���g
	virtual BOOL OnTimeout();	// �^�C���A�E�g�C�x���g
	
	virtual int OnRead(DWORD dwBytes);	// ��M�����C�x���g
	virtual int OnWrite(DWORD dwBytes);	// ���M�����C�x���g
	
public:
	operator HANDLE();	// IOCP�p�̃n���h���擾
	
	virtual BOOL Start();	// ��M�J�n
	BOOL Stop();			// �ꎞ��~
	int IsPending();		// �C�x���g�ҋ@������
	
	BOOL Socket(int af = AF_INET, int type = SOCK_STREAM, int protocol = IPPROTO_TCP);	// �d��IO�\�P�b�g�쐬
	BOOL Connect(const SOCKADDR* ai_addr, int ai_addrlen = sizeof(SOCKADDR));	// �N���C�A���g�̐ڑ��J�n
	int Write(LPCVOID pBuff, int iSendSize);	// �񓯊����M
};

// IOCP�p�\�P�b�g�쐬�N���X
class CSocketFactory : public CTLHeap<CSocketThread>
{
	friend class CIocpThreadPool<CSocketFactory>;
protected:
	typedef CTLHList<CIocpSocket, CTLHeap> CSocketList;
	CSocketList m_listSocket;	// �\�P�b�g���X�g�R���e�i
	
	CSocketFactory(DWORD dwHeapOpt = HEAP_NO_SERIALIZE);
	~CSocketFactory();
	
	VOID OnCancelIO(CSocketOverlapped* pIO);// IO�L�����Z���C�x���g
	VOID OnTimeout(DWORD dwCurrentTime);	// �^�C���A�E�g�C�x���g
	
public:
	BOOL FindSocket(CIocpSocket* pSocket);	// �\�P�b�g����
	BOOL DeleteSocket(CIocpSocket* pSocket, BOOL bFind = TRUE);// �\�P�b�g�폜
	
	// �\�P�b�g�쐬(�����R���e�i�ւ̒ǉ�)
	template <class TYPE>	// TYPE: CIocpSocket�h���N���X
	TYPE* CreateSocket(const CSockAddrIn* pAddr = NULL, int iType = SOCK_STREAM, int iProto = IPPROTO_TCP)
	{
		TYPE* pSocket = m_listSocket.AddItem<TYPE>(this);	// �\�P�b�g���쐬�����X�g�ɒǉ�
		
		if (pSocket)
		{
			if (pSocket->Socket(pAddr ? pAddr->sin_family : AF_INET, iType, iProto) && 
				pSocket->Bind(pAddr ? *pAddr : CSockAddrIn()) &&
				m_pIocp->CreateIocp(pSocket))	// �쐬�����\�P�b�g��IOCP�Ɋ֘A�t����
				pSocket->m_pThread = this;
			else
			{
				m_listSocket.DeleteItem(pSocket);	// ���s���̓��X�g����폜
				pSocket = NULL;
			}
		}
		return pSocket;
	}
	
	// �T�[�o�\�P�b�g�ǉ�(nPort: �|�[�g�ԍ�)
	template <class TYPE>	// TYPE: CIocpSocket�h���N���X
	CIocpSocket* AddListener(USHORT nPort, int nConnections = SOMAXCONN)
	{
		class CListenSocket : public CIocpSocket
		{
			CIocpSocket* m_pAccept;	// �ڑ������\�P�b�g
			CSockAddrIn m_addrRemote;	// �ڑ���A�h���X���
			
			BOOL OnAccept(const CSockAddrIn* pAddrRemote)	// �ڑ�����ꊮ���C�x���g
			{
				// ����ꂽ�\�P�b�g�Ƀ��[�J���̃A�h���X���𔽉f
				if (m_pAccept->SetSockOpt(SO_UPDATE_ACCEPT_CONTEXT,
								reinterpret_cast<char*>(&m_hSocket), sizeof(m_hSocket)))
				{
					m_pAccept->m_dwLastTime = m_dwLastTime;
					m_pAccept->m_pThread = m_pThread;
					if (!m_pAccept->OnAccept(&m_addrRemote))	// �����C�x���g�𔭍s
						Stop();	// �߂�l=FALSE��Accept��~
					
					// �\�P�b�g�L�����́AIOCP�Ɋ֘A�t���Ď�M�J�n
					if (m_pAccept->IsValid() && m_pThread->m_pIocp->CreateIocp(m_pAccept) &&
						(m_pAccept->IsPending() || m_pAccept->Start()))
						return TRUE;
				}
				CSocketList::Delete(m_pAccept);
				
				return TRUE;	// (�߂�l=FALSE�Ŏ��T�[�o�\�P�b�g���폜)
			}
		public:
			BOOL Start()	// �ڑ������(Accept)�J�n
			{
				// �\�P�b�g���쐬�����X�g�ɒǉ�
				if (IsPending() >= 0 && 	// �ꎞ��~(IOSuspend)�͎����ĊJ
					(m_pAccept = m_pThread->m_listSocket.AddItem<TYPE>(m_pThread)))
				{
					m_ioState = IOAccept;	// �C�x���g��ԍX�V
					if (m_pAccept->Socket() && 	// �\�P�b�g�쐬����
						(AcceptEx(m_hSocket, *m_pAccept, 
								&m_addrRemote, 0, 0, sizeof(m_addrRemote), 
								NULL, &m_ovl) ||
								WSAGetLastError() == WSA_IO_PENDING))
						return TRUE;
					
					m_ioState = IOInit;	// �G���[���̓C�x���g��ԏ�����
					CSocketList::Delete(m_pAccept);
				}
				return FALSE;
			}
		} *pListener = CreateSocket<CListenSocket>(&CSockAddrIn(nPort));	// �T�[�o�\�P�b�g�쐬
		
		// �\�P�b�g�쐬�ɐ���������ڑ��̎󂯓���J�n
		if (pListener && (!pListener->Listen(nConnections) || !pListener->Start()))
		{
			DeleteSocket(pListener, FALSE);	// ���s���͍폜
			pListener = NULL;
		}
		return pListener;
	}
	
	// �N���C�A���g�\�P�b�g�ǉ�
	template <class TYPE>	// TYPE: CIocpSocket�h���N���X
	TYPE* AddConnection(LPCTSTR pAddr, USHORT nPort)
	{
		TYPE* pConnect = CreateSocket<TYPE>();	// �N���C�A���g�\�P�b�g�쐬
		
		// �\�P�b�g�쐬�ɐ���������ڑ��J�n
		if (pConnect && !pConnect->Connect(CSockAddrIn(nPort, pAddr), sizeof(CSockAddrIn)))
		{
			DeleteSocket(pConnect, FALSE);	// ���s���͍폜
			pConnect = NULL;
		}
		return pConnect;
	}
	
	// �N���C�A���g�\�P�b�g�ǉ�(pName:�h���C�����ApServiceName:�T�[�r�X��)
	template <class TYPE>	// TYPE: CIocpSocket�h���N���X
	TYPE* AddConnection(LPCTSTR pName, LPCTSTR pServiceName)
	{
		struct CAddrInfoQuery : public TYPE	// �A�h���X���(ADDRINFOEX)�擾�p�̔h���N���X
		{
			PADDRINFOEX pResult;
			
			static VOID WINAPI QueryComplete(DWORD dwError, DWORD dwBytes, LPWSAOVERLAPPED lpOverlapped)
			{
				PADDRINFOEX* ppResult = static_cast<PADDRINFOEX*>(lpOverlapped->Pointer);	// �A�h���X���
				TYPE* pThis = reinterpret_cast<TYPE*>(ppResult) - 1;	// �쐬�����\�P�b�g
				
				// ���O�����ɐ���������ڑ��J�n
				::ZeroMemory(&pThis->m_ovl, sizeof(pThis->m_ovl));
				if (dwError != ERROR_SUCCESS || 
					!pThis->Connect((*ppResult)->ai_addr, (int)(*ppResult)->ai_addrlen))
					CSocketList::Delete(pThis);	// ���s���͍폜
				
				FreeAddrInfoEx(*ppResult);	// �擾�����A�h���X���(ADDRINFOEX)�����
			}
		} *pConnect = CreateSocket<CAddrInfoQuery>();	// �N���C�A���g�\�P�b�g�쐬
		HANDLE hCancel;
		
		// �h���C�����A�T�[�r�X���ɂ�閼�O������񓯊�(�d��IO)�Ŏ��s(UNICODE�Ō���)
		if (pConnect && 
			 GetAddrInfoEx(pName, pServiceName, NS_DNS, NULL, NULL, &pConnect->pResult,
						NULL, &pConnect->m_ovl, pConnect->QueryComplete, &hCancel) != WSA_IO_PENDING)
		{
			DeleteSocket(pConnect, FALSE);	// ���s���͍폜
			pConnect = NULL;
		}
		return pConnect;
	}
};

// IOCP�Ή��T�[�o�[�N���X(�񓯊�IF�g��)
class CIocpServer : public CIocpServerBase
{
protected:
	struct CSocketCreator	// �\�P�b�g�쐬�C���^�[�t�F�[�X
	{
		virtual CIocpSocket* Create(CSocketFactory* pFactory) = 0;
	};
	template <class PRM1, class PRM2>	// �p�����[�^�i�[�p�e���v���[�g
	struct CSocketCreatorT : public CSocketCreator
	{
		CSocketCreatorT(const PRM1& param1, const PRM2& param2) : m_param1(param1), m_param2(param2)
		{
		}
	protected:
		PRM1 m_param1;	// ��1�p�����[�^
		PRM2 m_param2;	// ��2�p�����[�^
	};
	
	BOOL PostCreator(CSocketCreator* pCreator);	// �\�P�b�g�쐬�C�x���g���s
	VOID OnPostEvent(LPOVERLAPPED_ENTRY lpCPEntry);	// PostEvent�ɂ�郆�[�U�C�x���g
public:
	CIocpServer(DWORD nThreadCnt = 0);
	
	// �T�[�o�\�P�b�g�쐬(�X���b�h�v�[���O������s)
	template <class TYPE>	// TYPE: CIocpSocket�h���N���X
	BOOL AddListener(USHORT nPort, int nConnections = SOMAXCONN)
	{
		struct CSocketCreatorListen : public CSocketCreatorT<USHORT, int>
		{
			CSocketCreatorListen(USHORT nPort, int nConnections) : CSocketCreatorT(nPort, nConnections)
			{
			}
			CIocpSocket* Create(CSocketFactory* pFactory)	// (�X���b�h�v�[��������ďo)
			{
				return pFactory->AddListener<TYPE>(m_param1, m_param2);
			}
		private:
			~CSocketCreatorListen();	// new�ȊO�̐������֎~
		};
		return PostCreator(new CSocketCreatorListen(nPort, nConnections));	// Listen�C�x���g���s
	}
	
	// �N���C�A���g�\�P�b�g�쐬(�X���b�h�v�[���O������s)
	template <class TYPE, class HOST, class PORT>	// TYPE: CIocpSocket�h���N���X
	BOOL AddConnection(HOST host, PORT port)	// host: �z�X�g��, port: �|�[�g�ԍ�(�v���g�R����)
	{
		struct CSocketCreatorConnect : public CSocketCreatorT<HOST, PORT>
		{
			CSocketCreatorConnect(const HOST& host, const PORT& port) : CSocketCreatorT(host, port)
			{
			}
			CIocpSocket* Create(CSocketFactory* pFactory)	// (�X���b�h�v�[��������ďo)
			{
				return pFactory->AddConnection<TYPE>(m_param1, m_param2);
			}
		private:
			~CSocketCreatorConnect();	// new�ȊO�̐������֎~
		};
		return PostCreator(new CSocketCreatorConnect(host, port));	// Connect�C�x���g���s
	}
};
