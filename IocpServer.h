/////////////////////////////////////////////////////////////////////////////
// IocpServer.h - IOCPクラスのソケット拡張
//
// [継承関係]
//             CIocpOverlapped <= 
// CSocketBase <= CSocketAsync <= CIocpSocket
// CAPCThread  <= CIocpThread  <= CTLHeap <= CSocketFactory  <=
//                                           CIocpThreadPool <= CIocpServer
// [包含関係]
// CIocpThreadPool<CSocketFactory> *- CSocketFactory +- CTLHList<CIocpSocket> *- CIocpSocket
//
// [参照関係]
// CIocpServer <- CSocketFactory <- CIocpSocket
//
#pragma once

#include "sockbase.h"
#include "iocp.h"
#include "tlheap.h"

class CSocketFactory;	// ソケット作成クラス宣言

typedef CIocpOverlapped<CSocketFactory> CSocketOverlapped;	// クライアントクラス
typedef CIocpThread<CSocketFactory>     CSocketThread;		// スレッドクラス
typedef CIocpThreadPool<CSocketFactory> CIocpServerBase;	// サーバクラス

// 非同期(重複IO)ソケットクラス
class CSocketAsync : public CSocketBase
{
public:
	BOOL Socket(int af = AF_INET, int type = SOCK_STREAM, int protocol = IPPROTO_TCP);	// 重複IOソケット作成
	SOCKET Attach(CSocketAsync& sock);	// 外部ソケットのハンドル割付け
};

// IOCP用ソケットクラス
class CIocpSocket : public CSocketOverlapped, public CSocketAsync
{
	friend class CSocketFactory;
protected:
	enum WSA_IOEVENT	// ソケットイベントID
	{
		IOInit = 0,	// 初期状態
		IOSuspend,	// 一時停止
		IOAccept,	// 接続受入待ち
		IOConnect,	// 接続完了待ち
		IOReceive,	// 受信完了待ち
		IOSend,		// 送信完了待ち
	} m_ioState;
	
	LPWSABUF m_pWsaBuf;	// 送受信保留データ
	CSocketFactory* m_pSockFct;	// Socket作成I/F
	
	CIocpSocket();
	~CIocpSocket();
	
	BOOL OnCompletionIO(CSocketFactory* pFct);	// IO完了イベント
	operator HANDLE();	// IOCP用のハンドル取得
	
	virtual BOOL OnAccept(const CSockAddrIn& addrRemote);	// 接続受入イベント
	virtual BOOL OnConnect();	// 接続完了イベント
	virtual BOOL OnTimeout();	// タイムアウトイベント
	
	virtual int OnRead(int iIoSize);	// 受信完了イベント
	virtual int OnWrite(int iIoSize);	// 送信完了イベント

public:
	virtual BOOL Start();	// 受信開始
	BOOL Stop();			// 一時停止
	BOOL IsPending();		// イベント待機中判定
	
	int Write(LPCVOID pBuff, int iSendSize);	// 非同期送信
#if(_WIN32_WINNT >= 0x0501)
	BOOL Connect(const SOCKADDR* ai_addr, int ai_addrlen);	// クライアントの接続開始
	BOOL Connect(const CSockAddrIn& addrRemote)
	{
		return Connect(addrRemote, sizeof(addrRemote));
	}
#endif
};

// IOCP用ソケット作成クラス
class CSocketFactory : public CTLHeap<CSocketThread>
{
protected:
	typedef CTLHList<CIocpSocket, CTLHeap> CSocketList;
	CSocketList m_listSocket;	// ソケットリストコンテナ
	
public:
	CSocketFactory(DWORD dwHeapOpt = HEAP_NO_SERIALIZE);
	~CSocketFactory();
	
	BOOL FindSocket(CIocpSocket* pSocket);	// ソケット検索
	BOOL DeleteSocket(CIocpSocket* pSocket);// ソケット削除
	
	VOID OnCancelIO(CSocketOverlapped* pIO);// IOキャンセルイベント
	VOID OnTimeout(DWORD dwCurrentTime);	// タイムアウトイベント
	
	// ソケット作成(内部コンテナへの追加)
	template <class TYPE>	// TYPE: CIocpSocket派生クラス
	TYPE* CreateSocket(const CSockAddrIn* pAddr = NULL)
	{
		if (!m_pIocp)
			return NULL;
		
		ISBASE_TYPE(TYPE, CIocpSocket);
		TYPE* pSocket = m_listSocket.AddItem<TYPE>(this);	// ソケットリストに追加
		
		if (pSocket && 
			(!pSocket->Socket() || !pSocket->Bind(pAddr ? *pAddr : CSockAddrIn()) ||
			 !m_pIocp->CreateIocp(pSocket)))	// 作成したソケットをIOCPに関連付ける
		{
			CSocketList::Delete(pSocket);	// 失敗時はリストから削除
			pSocket = NULL;
		}
		return pSocket;
	}
	
	// サーバソケット追加(nPort: ポート番号)
	template <class TYPE>	// TYPE: CIocpSocket派生クラス
	CIocpSocket* AddListener(USHORT nPort, int nConnections = SOMAXCONN)
	{
		class CListenSocket : public CIocpSocket
		{
			CSocketAsync sckAcpt;	// 接続受入れソケット
			CSockAddrIn addrRemote;	// 接続先アドレス情報
			
			BOOL OnCompletionIO(CSocketFactory* pFct)	// 接続受入れ完了イベント
			{
				m_ioState = IOInit;	// イベント状態初期化
				
				// 受入れたソケットにローカルのアドレス情報を反映
				if (!sckAcpt.SetSockOpt(SO_UPDATE_ACCEPT_CONTEXT,
						reinterpret_cast<char*>(&m_hSocket), sizeof(m_hSocket)))
					return FALSE;
				
				ISBASE_TYPE(TYPE, CIocpSocket);
				CIocpSocket* pAccept = pFct->m_listSocket.AddItem<TYPE>(pFct);	// ソケットリストに追加
				if (!pAccept)
					return FALSE;
				
				pAccept->Attach(sckAcpt);	// 作成したソケットに受入れたソケットのハンドルを割当
				pAccept->m_pSockFct = pFct;
				
				// 受入れイベントを発行し、戻り値=TRUEでAccept再開
				BOOL bRet = (!pAccept->OnAccept(addrRemote) || Start());
				
				// ソケット有効時は、IOCPに関連付けて受信開始
				if (!pAccept->IsValid() || !pFct->m_pIocp->CreateIocp(pAccept) ||
					!pAccept->IsPending() && !pAccept->Start())
					CSocketList::Delete(pAccept);
				
				return bRet;	// 戻り値=FALSEで自サーバソケットを削除
			}
			BOOL OnTimeout()
			{
				return TRUE;
			}
		public:
			BOOL Start()	// 接続受入れ(Accept)開始
			{
				if (m_ioState <= IOSuspend && IsValid() && sckAcpt.Socket())	// ソケット作成成功
				{
					::ZeroMemory(&m_ovl, sizeof(m_ovl));
					if (AcceptEx(m_hSocket, sckAcpt, 
							&addrRemote, 0, 0, sizeof(addrRemote), 
							NULL, &m_ovl) ||
							WSAGetLastError() == WSA_IO_PENDING)
					{
						m_ioState = IOAccept;	// 非同期Accept成功でイベント状態更新
						return TRUE;
					}
					sckAcpt.Close();	// Accept失敗時はソケット解放
				}
				return FALSE;
			}
		} *pListener = CreateSocket<CListenSocket>(&CSockAddrIn(nPort));	// サーバソケット作成
		
		// ソケット作成に成功したら接続の受け入れ開始
		if (pListener && (!pListener->Listen(nConnections) || !pListener->Start()))
		{
			CSocketList::Delete(pListener);	// 失敗時は削除
			pListener = NULL;
		}
		return pListener;
	}
	
	// クライアントソケット追加(addrRemote: IPアドレス)
	template <class TYPE>	// TYPE: CIocpSocket派生クラス
	TYPE* AddConnection(const CSockAddrIn& addrRemote)
	{
		TYPE* pConnect = CreateSocket<TYPE>();	// クライアントソケット作成
		
		// ソケット作成に成功したら接続開始
		if (pConnect && !pConnect->Connect(addrRemote))
		{
			CSocketList::Delete(pConnect);	// 失敗時は削除
			pConnect = NULL;
		}
		return pConnect;
	}
	
#if (_WIN32_WINNT >= 0x0600)
	// クライアントソケット追加(pName:ドメイン名、pServiceName:サービス名)
	template <class TYPE>	// TYPE: CIocpSocket派生クラス
	TYPE* AddConnection(LPCTSTR pName, LPCTSTR pServiceName)
	{
		struct CAddrInfoQuery : public TYPE	// アドレス情報(ADDRINFOEX)取得用の派生クラス
		{
			PADDRINFOEX pResult;
			
			static VOID WINAPI QueryComplete(DWORD dwError, DWORD dwBytes, LPWSAOVERLAPPED lpOverlapped)
			{
				PADDRINFOEX* ppResult = static_cast<PADDRINFOEX*>(lpOverlapped->Pointer);	// アドレス情報
				TYPE* pThis = reinterpret_cast<TYPE*>(ppResult) - 1;	// 作成したソケット
				
				// 名前解決に成功したら接続開始
				::ZeroMemory(&pThis->m_ovl, sizeof(pThis->m_ovl));
				if (dwError != ERROR_SUCCESS || 
					!pThis->Connect((*ppResult)->ai_addr, (int)(*ppResult)->ai_addrlen))
					CSocketList::Delete(pThis);	// 失敗時は削除
				
				FreeAddrInfoEx(*ppResult);	// 取得したアドレス情報(ADDRINFOEX)を解放
			}
		} *pConnect = CreateSocket<CAddrInfoQuery>();	// クライアントソケット作成
		HANDLE hCancel;
		
		// ドメイン名、サービス名による名前解決を非同期(重複IO)で実行
		if (pConnect && 
			 GetAddrInfoEx(pName, pServiceName, NS_DNS, NULL, NULL, &pConnect->pResult,
						NULL, &pConnect->m_ovl, pConnect->QueryComplete, &hCancel) != WSA_IO_PENDING)
		{
			CSocketList::Delete(pConnect);	// 失敗時は削除
			pConnect = NULL;
		}
		return pConnect;
	}
#endif
};

// IOCP対応サーバークラス
// (解放時にスレッド終了を優先するため、継承順はCIocpThreadPoolが後)
class CIocpServer : public CSocketFactory, public CIocpServerBase
{
protected:
	VOID OnPostEvent(LPOVERLAPPED_ENTRY lpCPEntry);	// ユーザイベント
	
public:
	CIocpServer(DWORD nThreadCnt = 0);
	
	// CTLHeap::ApcMsgSend用パラメータクラス
	// (スレッドプール側で、非同期に永続ポートの追加と削除を実行)
	template <class TYPE = VOID>
	class AddPort	// ポート追加: src->ApcMsgSend(dst, CIocpServer::AddPort<TYPE>(nPort));
	{
		USHORT m_nPort;		// ポート番号
		int m_nConnections;	// 最大接続数(省略可)
	public:
		AddPort(USHORT nPort, int nConnections = SOMAXCONN)
		{
			m_nPort = nPort;
			m_nConnections = nConnections;
		}
		void operator ()(CIocpServer* pServer)
		{
			pServer->AddListener<TYPE>(m_nPort, m_nConnections);
		}
	};
	class DelPort	// ポート削除: src->ApcMsgSend(dst, CIocpServer::DelPort(nPort));
	{
		USHORT m_nPort;	// ポート番号(省略時(0)は全削除)
	public:
		DelPort(USHORT nPort = 0);
		void operator ()(CIocpServer* pServer);
	};
	
	// サーバ起動(TYPE: CIocpSocket派生クラス, SYNC: 同期/非同期)
	template <class TYPE, bool SYNC = TRUE>
	BOOL Listen(USHORT nPort, int nConnections = SOMAXCONN, 
				DWORD nThreadCnt = 0, DWORD nKeepAlive = MIN_KEEPALIVE)
	{
		if (!CreateIocp(NULL, nThreadCnt))	// IOCP作成
			return FALSE;
		
		// サーバソケット作成
		CIocpSocket* pListener = AddListener<TYPE>(nPort, nConnections);
		if (!pListener)
			return FALSE;
		
		// スレッドプール実行(実行直後、ユーザイベントによってスレッド割当)
		BOOL bStart = (PostEvent(SYNC) && Start(nKeepAlive, SYNC));
		if (SYNC || !bStart)
			DeleteSocket(pListener);
		
		return bStart;
	}
};
