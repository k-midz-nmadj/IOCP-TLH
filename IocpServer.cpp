/////////////////////////////////////////////////////////////////////////////
// IocpServer.cpp - IOCP�N���X�̃\�P�b�g�g��
//
#include "IocpServer.h"

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "Mswsock.lib")


CIocpSocket::CIocpSocket() : m_ioState(IOInit), m_pWsaBuf(NULL)
{
}

CIocpSocket::~CIocpSocket()
{
	if (m_pThread && m_pWsaBuf)	// �ۗ��o�b�t�@�c�����͉��
		m_pThread->Free(m_pWsaBuf);
}

// �񓯊�Socket�C�x���g�n���h��
BOOL CIocpSocket::OnCompletionIO(LPOVERLAPPED_ENTRY lpCPEntry)
{
	int iRes = 0, iIoType = m_ioState;
	
	::ZeroMemory(&m_ovl, sizeof(m_ovl));
	m_ioState = IOInit;	// �C�x���g�������͏�ԏ�����
	
	if (!IsValid())
		return FALSE;	// �\�P�b�g����(IO�v������Close)
	
	switch (iIoType)	// �ҋ@���̃C�x���g����
	{
	case IOAccept:	// �ڑ����(�T�[�o�\�P�b�g�p)
		iRes = OnAccept();
		break;
	case IOConnect:	// �ڑ�����(�\�P�b�g�̃A�h���X�����X�V���AShutdown���g�p�ɂ���)
		if (lpCPEntry->Internal >= 0xc0000000 || SetSockOpt(SO_UPDATE_CONNECT_CONTEXT, NULL, 0))
			iRes = OnConnect((DWORD)lpCPEntry->Internal);	// �ڑ����(NTSTATUS�l)�ʒm
		break;
	case IOReceive:	// ��M����
		iRes = OnRead(lpCPEntry->dwNumberOfBytesTransferred);
		break;
	case IOSend:	// ���M����
		if (m_pWsaBuf)
		{
			m_pThread->Free(m_pWsaBuf);	// �ۗ��o�b�t�@���
			m_pWsaBuf = NULL;
		}
		iRes = OnWrite(lpCPEntry->dwNumberOfBytesTransferred);
		break;
	}
	return (iRes > 0 && (IsPending() || Start()));	// �G���[��IO�v�����Ȃ���Ύ�M�ĊJ
}

// Socket�n���h���擾
CIocpSocket::operator HANDLE()
{
	return reinterpret_cast<HANDLE>(m_hSocket);
}

// �ڑ�����C�x���g(�f�t�H���g����)
BOOL CIocpSocket::OnAccept(const CSockAddrIn* pAddrRemote)
{
	return TRUE;
}
// �ڑ������C�x���g(�f�t�H���g����)
BOOL CIocpSocket::OnConnect(DWORD dwError)
{
	return TRUE;
}
// �^�C���A�E�g�C�x���g(�f�t�H���g����)
BOOL CIocpSocket::OnTimeout()
{
	return (m_ioState >= IOReceive ? Close() : TRUE);	// �C�x���g�ҋ@���͐ؒf
}

// ��M�����C�x���g(�f�t�H���g����)
int CIocpSocket::OnRead(DWORD dwBytes)
{
	return 0;
}
// ���M�����C�x���g(�f�t�H���g����)
int CIocpSocket::OnWrite(DWORD dwBytes)
{
	return 0;
}

// ��M�J�n
BOOL CIocpSocket::Start()
{
	if (IsPending() < 0)
		return FALSE;	// �ꎞ��~(IOSuspend)�͎�M�ĊJ
	
	ULONG ulFlags = 0;
	WSABUF wsaNull = { 0, NULL };	// (�������ߖ�̂��߁A��M�o�b�t�@�̓C�x���g���s���ɗp��)
	
	m_ioState = IOReceive;	// �C�x���g��ԍX�V(IO�v������ɔ������銮���C�x���g����s���čX�V)
	if (WSARecv(m_hSocket, &wsaNull, 1, NULL, &ulFlags, &m_ovl, NULL) == SOCKET_ERROR &&
		WSAGetLastError() != WSA_IO_PENDING)
	{
		m_ioState = IOInit;	// �G���[���̓C�x���g��ԏ�����
		return FALSE;
	}
	return TRUE;
}

// �ꎞ��~(�C�x���g�����������IO�v���X�L�b�v)
BOOL CIocpSocket::Stop()
{
	return (IsPending() ? FALSE : bool(m_ioState = IOSuspend));	// IOInit=>IOSuspend
}

// �C�x���g�ҋ@(IO�v���ۗ�)����
int CIocpSocket::IsPending()
{
	return (m_ioState > IOSuspend ? -1 : m_ioState);	// �������:0,��~���:1,�ҋ@���:-1
}

// �d��IO�\�P�b�g�쐬
BOOL CIocpSocket::Socket(int af, int type, int protocol)
{
	if (m_hSocket == INVALID_SOCKET)
	{	// �\�P�b�g���쐬���A�d��IO���[�h�Ń\�P�b�g�쐬
		m_hSocket = WSASocket(af, type, protocol, NULL, 0, WSA_FLAG_OVERLAPPED);
		if (m_hSocket != INVALID_SOCKET)
		{
			u_long nbio = 1;
			if (Ioctl(FIONBIO, &nbio))	// �m���u���b�L���O�ɐݒ�
				return TRUE;
			
			Close();	// �쐬���s���̓\�P�b�g���
		}
	}
	return FALSE;
}

// �N���C�A���g�̐ڑ��J�n
BOOL CIocpSocket::Connect(const SOCKADDR* ai_addr, int ai_addrlen)
{
	if (IsPending())
		return FALSE;	// IO�v���ς݂Ȃ�ڑ�����
	
	LPFN_CONNECTEX lpfnConnectEx = NULL;
	GUID guidConnectEx = WSAID_CONNECTEX;	// ConnectEx�֐��̎��ʎq
	DWORD dwBytes = 0;
	
	m_ioState = IOConnect;	// �C�x���g��ԍX�V
	if (WSAIoctl(m_hSocket, SIO_GET_EXTENSION_FUNCTION_POINTER,
				&guidConnectEx, sizeof(guidConnectEx),
				&lpfnConnectEx, sizeof(lpfnConnectEx),
				&dwBytes, NULL, NULL) == SOCKET_ERROR ||	// ConnectEx�֐��̃|�C���^�擾
		!lpfnConnectEx(m_hSocket, ai_addr, ai_addrlen, NULL, 0, &dwBytes, &m_ovl) &&
		WSAGetLastError() != WSA_IO_PENDING)
	{
		m_ioState = IOInit;	// �G���[���̓C�x���g��ԏ�����
		return FALSE;
	}
	return TRUE;
}

// �񓯊����M
int CIocpSocket::Write(LPCVOID pBuff, int iSendSize)
{
	if (IsPending() || m_pWsaBuf)
		return 0;	// IO�v���ς݂��ۗ��f�[�^������Α��M����
	
	int iIoSize = Send(pBuff, iSendSize);
	if (iIoSize == SOCKET_ERROR)
	{
		if (WSAGetLastError() != WSAEWOULDBLOCK)
			return SOCKET_ERROR;	// ���M�ۗ��ȊO�̓G���[
		
		iIoSize = 0;
	}
	
	if (iIoSize < iSendSize && m_pThread)	// ���M�ł��Ȃ������ۗ��f�[�^��TLH�ŕۑ�
	{
		iSendSize -= iIoSize;
		m_pWsaBuf = static_cast<LPWSABUF>(m_pThread->Alloc(sizeof(WSABUF) + iSendSize));
		if (!m_pWsaBuf)
			return SOCKET_ERROR;
		
		m_pWsaBuf->buf = reinterpret_cast<PCHAR>(m_pWsaBuf + 1);
		m_pWsaBuf->len = iSendSize;
		::CopyMemory(m_pWsaBuf->buf, static_cast<LPCSTR>(pBuff) + iIoSize, iSendSize);
		
		m_ioState = IOSend;	// �C�x���g��ԍX�V
		if (WSASend(m_hSocket, m_pWsaBuf, 1, NULL, 0, &m_ovl, NULL) == SOCKET_ERROR &&
			WSAGetLastError() != WSA_IO_PENDING)
		{
			m_ioState = IOInit;	// �G���[���̓C�x���g��ԏ�����
			iIoSize = SOCKET_ERROR;
		}
	}
	return iIoSize;
}


CSocketFactory::CSocketFactory(DWORD dwHeapOpt) : CTLHeap(dwHeapOpt)
{
}

CSocketFactory::~CSocketFactory()
{
	BindThread();	// ���X�g�R���e�i����ɃX���b�h��������A�\�P�b�g�������APC�𖳌���
}

// IO�L�����Z���C�x���g
VOID CSocketFactory::OnCancelIO(CSocketOverlapped* pIO)
{
	CIocpSocket* pSocket = static_cast<CIocpSocket*>(pIO);
	int iState = pSocket->IsPending();	// ����O�ɏ�Ԏ擾
	
	pSocket->Close();	// �\�P�b�g���
	if (iState >= 0)
		m_listSocket.DeleteItem(pSocket);	// ��~���Ȃ�폜
}

// �^�C���A�E�g�C�x���g
VOID CSocketFactory::OnTimeout(DWORD dwCurrentTime)
{
	if (m_listSocket.IsEmpty())
		return;	// ���X�g����
	
	CIocpSocket* pSocket;
	CSocketList::Node* pItr = m_listSocket.GetHead();	// ���X�g�擪�擾
	
	while (pSocket = m_listSocket.GetNext(pItr))
	{
		if (DIFF_TIME(dwCurrentTime, pSocket->m_dwLastTime) > m_pIocp->m_dwMinWaitTime && 
			!pSocket->OnTimeout())	// �e�\�P�b�g�̃^�C���A�E�g�C�x���g���s
			m_listSocket.DeleteItem(pSocket);	// �߂�l=FALSE:�\�P�b�g�폜
	}
}

// �\�P�b�g����
BOOL CSocketFactory::FindSocket(CIocpSocket* pFind)
{
	CIocpSocket* pSocket;
	CSocketList::Node* pItr = m_listSocket.GetHead();	// ���X�g�擪�擾
	
	while ((pSocket = m_listSocket.GetNext(pItr)) && pSocket != pFind);
	
	return (pSocket != NULL);	// �\�P�b�g���o:TRUE
}

// �\�P�b�g�폜
BOOL CSocketFactory::DeleteSocket(CIocpSocket* pSocket, BOOL bFind)
{
	return m_listSocket.DeleteItem(pSocket, bFind);	// �\�P�b�g���o�L��
}


CIocpServer::CIocpServer(DWORD nThreadCnt) : CIocpServerBase(nThreadCnt)
{
}

// �\�P�b�g�쐬�C�x���g���s
BOOL CIocpServer::PostCreator(CSocketCreator* pCreator)
{
	if (pCreator)
	{
		if (CreateIocp() && PostEvent(pCreator))	// IOCP�쐬�A���[�U�C�x���g���s
			return TRUE;
		
		delete pCreator;	// �G���[���͉��
	}
	return FALSE;
}

// PostEvent�ɂ�郆�[�U�C�x���g
VOID CIocpServer::OnPostEvent(LPOVERLAPPED_ENTRY lpCPEntry)
{
	CSocketCreator* pCreator = reinterpret_cast<CSocketCreator*>(lpCPEntry->lpCompletionKey);
	
	if (pCreator)	// �\�P�b�g�쐬���X���b�h�v�[���Ŕ񓯊����s
	{
		CIocpSocket* pSocket = pCreator->Create(reinterpret_cast<CSocketFactory*>(lpCPEntry->Internal));
		
		lpCPEntry->lpCompletionKey = reinterpret_cast<ULONG_PTR>(pSocket);
		delete pCreator;
	}
}
