/////////////////////////////////////////////////////////////////////////////
// IocpServer.cpp - IOCPクラスのソケット拡張
//
#include "IocpServer.h"

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "Mswsock.lib")

// 重複IOソケット作成
BOOL CSocketAsync::Socket(int af, int type, int protocol)
{
	if (m_hSocket == INVALID_SOCKET)
	{	// ソケット未作成時、重複IOモードでソケット作成
		m_hSocket = WSASocket(af, type, protocol, NULL, 0, WSA_FLAG_OVERLAPPED);
		if (m_hSocket != INVALID_SOCKET)
		{
			u_long nbio = 1;
			if (Ioctl(FIONBIO, &nbio))	// ノンブロッキングに設定
				return TRUE;
			
			Close();	// 作成失敗時はソケット解放
		}
	}
	return FALSE;
}

// 外部ソケットのハンドル割付け
SOCKET CSocketAsync::Attach(CSocketAsync& sock)
{
	SOCKET hSockOld = m_hSocket;
	
	m_hSocket = sock;
	sock.m_hSocket = INVALID_SOCKET;	// 割付け元のハンドルを無効化
	
	return hSockOld;
}


CIocpSocket::CIocpSocket()
{
	m_ioState = IOInit;
	
	m_pSockFct = NULL;
	m_pWsaBuf = NULL;
}

CIocpSocket::~CIocpSocket()
{
	if (m_pSockFct && m_pWsaBuf)	// 保留バッファ残留時は解放
		m_pSockFct->Free(m_pWsaBuf);
}

// 非同期Socketイベントハンドラ
BOOL CIocpSocket::OnCompletionIO(CSocketFactory* pFct)
{
	int iRes, iIoType = m_ioState;
	
	::ZeroMemory(&m_ovl, sizeof(m_ovl));
	m_pSockFct = pFct;	// アロケータ(TLH)取得
	m_ioState = IOInit;	// イベント発生時は状態初期化
	
	switch (iIoType)	// 待機中のイベント判定
	{
	case IOConnect:	// 接続完了(ソケットのアドレス情報を更新し、Shutdownを使用可にする)
		iRes = (SetSockOpt(SO_UPDATE_CONNECT_CONTEXT, NULL, 0) && OnConnect());
		break;
	case IOReceive:	// 受信完了
		iRes = OnRead(0);
		break;
	case IOSend:	// 送信完了
		if (m_pWsaBuf)
		{
			pFct->Free(m_pWsaBuf);	// 保留バッファ解放
			m_pWsaBuf = NULL;
		}
		iRes = OnWrite(0);
		break;
	default:	// 不正イベントはエラー
		iRes = 0;
	}
	return (iRes > 0 && (IsPending() || Start()));	// エラーとIO要求がなければ受信再開
}

// Socketハンドル取得
CIocpSocket::operator HANDLE()
{
	return reinterpret_cast<HANDLE>(m_hSocket);
}

// 接続受入イベント(デフォルト実装)
BOOL CIocpSocket::OnAccept(const CSockAddrIn& addrRemote)
{
	return TRUE;
}
// 接続完了イベント(デフォルト実装)
BOOL CIocpSocket::OnConnect()
{
	return TRUE;
}
// タイムアウトイベント(デフォルト実装)
BOOL CIocpSocket::OnTimeout()
{
	return (m_ioState > IOSuspend ? Shutdown() : TRUE);	// イベント待機時は切断
}

// 受信完了イベント(デフォルト実装)
int CIocpSocket::OnRead(int iIoSize)
{
	return 0;
}
// 送信完了イベント(デフォルト実装)
int CIocpSocket::OnWrite(int iIoSize)
{
	return 0;
}

// 受信開始
BOOL CIocpSocket::Start()
{
	if (m_ioState > IOSuspend || !IsValid())
		return FALSE;	// 一時停止(IOSuspend)は受信再開
	
	ULONG ulFlags = 0;
	WSABUF wsaNull = { 0, NULL };	// (メモリ節約のため、受信バッファはイベント発行時に用意)
	
	return (WSARecv(m_hSocket, 
				&wsaNull, 1, 
				NULL, &ulFlags, 
				&m_ovl, NULL) != SOCKET_ERROR ||
				WSAGetLastError() == WSA_IO_PENDING ? 
			bool(m_ioState = IOReceive) : FALSE);	// 非同期受信成功でイベント状態更新
}

// 一時停止(イベント発生時限定のIO要求スキップ)
BOOL CIocpSocket::Stop()
{
	return (IsPending() ? FALSE : bool(m_ioState = IOSuspend));	// IOInit=>IOSuspend
}

// イベント待機(IO要求保留)判定
BOOL CIocpSocket::IsPending()
{
	return (m_ioState != IOInit);	// 初期状態以外:TRUE
}

// 非同期送信
int CIocpSocket::Write(LPCVOID pBuff, int iSendSize)
{
	if (IsPending() || m_pWsaBuf)
		return 0;	// IO要求済みか保留データがあれば送信無効
	
	int iIoSize = Send(pBuff, iSendSize);
	if (iIoSize == SOCKET_ERROR)
	{
		if (WSAGetLastError() != WSAEWOULDBLOCK)
			return SOCKET_ERROR;	// 送信保留以外はエラー
		
		iIoSize = 0;
	}
	
	if (iIoSize < iSendSize && m_pSockFct)	// 送信できなかった保留データをTLHで保存
	{
		iSendSize -= iIoSize;
		m_pWsaBuf = static_cast<LPWSABUF>(m_pSockFct->Alloc(sizeof(WSABUF) + iSendSize));
		if (!m_pWsaBuf)
			return SOCKET_ERROR;
		
		m_pWsaBuf->buf = reinterpret_cast<PCHAR>(m_pWsaBuf + 1);
		m_pWsaBuf->len = iSendSize;
		::CopyMemory(m_pWsaBuf->buf, static_cast<LPCSTR>(pBuff) + iIoSize, iSendSize);
		
		if (WSASend(m_hSocket, m_pWsaBuf, 1, NULL, 0, &m_ovl, NULL) == SOCKET_ERROR &&
			WSAGetLastError() != WSA_IO_PENDING)
			return SOCKET_ERROR;
		
		m_ioState = IOSend;	// 非同期送信成功でイベント状態更新
	}
	return iIoSize;
}

#if(_WIN32_WINNT >= 0x0501)
// クライアントの接続開始
BOOL CIocpSocket::Connect(const SOCKADDR* ai_addr, int ai_addrlen)
{
	if (IsPending() || m_pSockFct)
		return FALSE;	// IO要求済みか接続済みなら接続無効
	
	LPFN_CONNECTEX lpfnConnectEx = NULL;
	GUID guidConnectEx = WSAID_CONNECTEX;	// ConnectEx関数の識別子
	DWORD dwBytes = 0;
	
	return (WSAIoctl(m_hSocket, SIO_GET_EXTENSION_FUNCTION_POINTER,
				&guidConnectEx, sizeof(guidConnectEx),
				&lpfnConnectEx, sizeof(lpfnConnectEx),
				&dwBytes, NULL, NULL) != SOCKET_ERROR &&	// ConnectEx関数のポインタ取得
			(lpfnConnectEx(m_hSocket,
				ai_addr, ai_addrlen,
				NULL, 0, &dwBytes, &m_ovl) ||
				WSAGetLastError() == WSA_IO_PENDING) ? 
			bool(m_ioState = IOConnect) : FALSE);	// 非同期接続成功でイベント状態更新
}
#endif

CSocketFactory::CSocketFactory(DWORD dwHeapOpt) : CTLHeap(dwHeapOpt)
{
}

CSocketFactory::~CSocketFactory()
{
	BindThread();	// リストコンテナより先にスレッドを解放し、ソケット解放時のAPCを無効化
}

// ソケット検索
BOOL CSocketFactory::FindSocket(CIocpSocket* pFind)
{
	CIocpSocket* pSocket;
	CSocketList::Node* pItr = m_listSocket.GetHead();	// リスト先頭取得
	
	while ((pSocket = m_listSocket.GetNext(pItr)) && pSocket != pFind);
	
	return (pSocket != NULL);	// ソケット検出:TRUE
}

// ソケット削除
BOOL CSocketFactory::DeleteSocket(CIocpSocket* pSocket)
{
	return m_listSocket.DeleteItem(pSocket, TRUE);	// ソケット検出有効
}

// IOキャンセルイベント
VOID CSocketFactory::OnCancelIO(CSocketOverlapped* pIO)
{
	CIocpSocket* pSocket = static_cast<CIocpSocket*>(pIO);
	
	if (pSocket->m_ioState > CIocpSocket::IOSuspend)
		pSocket->Close();	// IO要求済みならソケット解放
	else
		m_listSocket.DeleteItem(pSocket);	// 停止中なら削除
}

// タイムアウトイベント
VOID CSocketFactory::OnTimeout(DWORD dwCurrentTime)
{
	if (m_listSocket.IsEmpty())
		return;	// リストが空
	
	CIocpSocket* pSocket;
	CSocketList::Node* pItr = m_listSocket.GetHead();	// リスト先頭取得
	
	while (pSocket = m_listSocket.GetNext(pItr))
	{
		if (DIFF_TIME(dwCurrentTime, pSocket->m_dwLastTime) >= m_pIocp->m_dwMinKeepAlive && 
			!pSocket->OnTimeout())	// 各ソケットのタイムアウトイベント発行
			m_listSocket.DeleteItem(pSocket);	// 戻り値=FALSE:ソケット削除
	}
}

// ヒープオプション(0:Serialize有効)指定
CIocpServer::CIocpServer(DWORD nThreadCnt) : CSocketFactory(0), CIocpServerBase(nThreadCnt)
{
	m_pIocp = this;
}

// PostEventによるユーザイベント
VOID CIocpServer::OnPostEvent(LPOVERLAPPED_ENTRY lpCPEntry)
{
	if (m_nThreadCnt > lpCPEntry->lpCompletionKey)	// 単独の同期スレッド実行時(1>1)以外
		BindThread(&m_pThreads[0]);	// サーバ実行スレッドにスレッドプール割当
}

// CTLHeap::ApcMsgSend用パラメータ(DelPort)
CIocpServer::DelPort::DelPort(USHORT nPort)
{
	m_nPort = nPort;
}

// 非同期ポート削除
void CIocpServer::DelPort::operator ()(CIocpServer* pServer)
{
	CSocketList* pList = &pServer->m_listSocket;	// ソケットリスト
	CIocpSocket* pSocket;
	CSocketList::Node* pItr = pList->GetHead();	// リスト先頭取得
	
	m_nPort = htons(m_nPort);	// バイトオーダ変換
	while (pSocket = pList->GetNext(pItr))	// 全ソケットについて検索
	{
		CSockAddrIn addr;
		
		// 指定したポート番号と一致
		if (pSocket->GetSockName(addr) && (!m_nPort || m_nPort == addr.sin_port))
		{
			pSocket->Close();	// ソケット解放(削除は2重解放になるため禁止)
			if (m_nPort)	// 0ポート指定時は全解放
				break;
		}
	}
}
