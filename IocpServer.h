/////////////////////////////////////////////////////////////////////////////
// IocpServer.h - IOCPクラスのソケット拡張
//
// [継承関係]
// CIocpOverlapped <= 
// CSocketBase     <= CIocpSocket
// CAPCThread      <= CIocpThread  <= CTLHeap <= CSocketFactory
// CIocpThreadPool <= CIocpServer
// 
// [包含関係]
// CIocpThreadPool<CSocketFactory> *- CSocketFactory +- CTLHList<CIocpSocket> *- CIocpSocket
// 
// [参照関係]
// CIocpServer <- CSocketFactory <- CIocpSocket
// 
#pragma once

#include "iocp.h"
#include "tlheap.h"
#include "sockbase.h"

class CSocketFactory;	// ソケット作成クラス宣言

typedef CIocpOverlapped<CSocketFactory> CSocketOverlapped;	// クライアントクラス
typedef CIocpThread<CSocketFactory>     CSocketThread;		// スレッドクラス
typedef CIocpThreadPool<CSocketFactory> CIocpServerBase;	// サーバクラス

// IOCP用ソケットクラス
class CIocpSocket : public CSocketOverlapped, public CSocketBase
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
	
	CIocpSocket();
	~CIocpSocket();
	
	BOOL OnCompletionIO(LPOVERLAPPED_ENTRY lpCPEntry);	// IO完了イベント
	
	virtual BOOL OnAccept(const CSockAddrIn* pAddrRemote = NULL);	// 接続受入イベント
	virtual BOOL OnConnect(DWORD dwError = 0);	// 接続完了イベント
	virtual BOOL OnTimeout();	// タイムアウトイベント
	
	virtual int OnRead(DWORD dwBytes);	// 受信完了イベント
	virtual int OnWrite(DWORD dwBytes);	// 送信完了イベント
	
public:
	operator HANDLE();	// IOCP用のハンドル取得
	
	virtual BOOL Start();	// 受信開始
	BOOL Stop();			// 一時停止
	int IsPending();		// イベント待機中判定
	
	BOOL Socket(int af = AF_INET, int type = SOCK_STREAM, int protocol = IPPROTO_TCP);	// 重複IOソケット作成
	BOOL Connect(const SOCKADDR* ai_addr, int ai_addrlen = sizeof(SOCKADDR));	// クライアントの接続開始
	int Write(LPCVOID pBuff, int iSendSize);	// 非同期送信
};

// IOCP用ソケット作成クラス
class CSocketFactory : public CTLHeap<CSocketThread>
{
	friend class CIocpThreadPool<CSocketFactory>;
protected:
	typedef CTLHList<CIocpSocket, CTLHeap> CSocketList;
	CSocketList m_listSocket;	// ソケットリストコンテナ
	
	CSocketFactory(DWORD dwHeapOpt = HEAP_NO_SERIALIZE);
	~CSocketFactory();
	
	VOID OnCancelIO(CSocketOverlapped* pIO);// IOキャンセルイベント
	VOID OnTimeout(DWORD dwCurrentTime);	// タイムアウトイベント
	
public:
	BOOL FindSocket(CIocpSocket* pSocket);	// ソケット検索
	BOOL DeleteSocket(CIocpSocket* pSocket, BOOL bFind = TRUE);// ソケット削除
	
	// ソケット作成(内部コンテナへの追加)
	template <class TYPE>	// TYPE: CIocpSocket派生クラス
	TYPE* CreateSocket(const CSockAddrIn* pAddr = NULL, int iType = SOCK_STREAM, int iProto = IPPROTO_TCP)
	{
		TYPE* pSocket = m_listSocket.AddItem<TYPE>(this);	// ソケットを作成しリストに追加
		
		if (pSocket)
		{
			if (pSocket->Socket(pAddr ? pAddr->sin_family : AF_INET, iType, iProto) && 
				pSocket->Bind(pAddr ? *pAddr : CSockAddrIn()) &&
				m_pIocp->CreateIocp(pSocket))	// 作成したソケットをIOCPに関連付ける
				pSocket->m_pThread = this;
			else
			{
				m_listSocket.DeleteItem(pSocket);	// 失敗時はリストから削除
				pSocket = NULL;
			}
		}
		return pSocket;
	}
	
	// サーバソケット追加(nPort: ポート番号)
	template <class TYPE>	// TYPE: CIocpSocket派生クラス
	CIocpSocket* AddListener(USHORT nPort, int nConnections = SOMAXCONN)
	{
		class CListenSocket : public CIocpSocket
		{
			CIocpSocket* m_pAccept;	// 接続受入れソケット
			CSockAddrIn m_addrRemote;	// 接続先アドレス情報
			
			BOOL OnAccept(const CSockAddrIn* pAddrRemote)	// 接続受入れ完了イベント
			{
				// 受入れたソケットにローカルのアドレス情報を反映
				if (m_pAccept->SetSockOpt(SO_UPDATE_ACCEPT_CONTEXT,
								reinterpret_cast<char*>(&m_hSocket), sizeof(m_hSocket)))
				{
					m_pAccept->m_dwLastTime = m_dwLastTime;
					m_pAccept->m_pThread = m_pThread;
					if (!m_pAccept->OnAccept(&m_addrRemote))	// 受入れイベントを発行
						Stop();	// 戻り値=FALSEでAccept停止
					
					// ソケット有効時は、IOCPに関連付けて受信開始
					if (m_pAccept->IsValid() && m_pThread->m_pIocp->CreateIocp(m_pAccept) &&
						(m_pAccept->IsPending() || m_pAccept->Start()))
						return TRUE;
				}
				CSocketList::Delete(m_pAccept);
				
				return TRUE;	// (戻り値=FALSEで自サーバソケットを削除)
			}
		public:
			BOOL Start()	// 接続受入れ(Accept)開始
			{
				// ソケットを作成しリストに追加
				if (IsPending() >= 0 && 	// 一時停止(IOSuspend)は受入れ再開
					(m_pAccept = m_pThread->m_listSocket.AddItem<TYPE>(m_pThread)))
				{
					m_ioState = IOAccept;	// イベント状態更新
					if (m_pAccept->Socket() && 	// ソケット作成成功
						(AcceptEx(m_hSocket, *m_pAccept, 
								&m_addrRemote, 0, 0, sizeof(m_addrRemote), 
								NULL, &m_ovl) ||
								WSAGetLastError() == WSA_IO_PENDING))
						return TRUE;
					
					m_ioState = IOInit;	// エラー時はイベント状態初期化
					CSocketList::Delete(m_pAccept);
				}
				return FALSE;
			}
		} *pListener = CreateSocket<CListenSocket>(&CSockAddrIn(nPort));	// サーバソケット作成
		
		// ソケット作成に成功したら接続の受け入れ開始
		if (pListener && (!pListener->Listen(nConnections) || !pListener->Start()))
		{
			DeleteSocket(pListener, FALSE);	// 失敗時は削除
			pListener = NULL;
		}
		return pListener;
	}
	
	// クライアントソケット追加
	template <class TYPE>	// TYPE: CIocpSocket派生クラス
	TYPE* AddConnection(LPCTSTR pAddr, USHORT nPort)
	{
		TYPE* pConnect = CreateSocket<TYPE>();	// クライアントソケット作成
		
		// ソケット作成に成功したら接続開始
		if (pConnect && !pConnect->Connect(CSockAddrIn(nPort, pAddr), sizeof(CSockAddrIn)))
		{
			DeleteSocket(pConnect, FALSE);	// 失敗時は削除
			pConnect = NULL;
		}
		return pConnect;
	}
	
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
		
		// ドメイン名、サービス名による名前解決を非同期(重複IO)で実行(UNICODE版限定)
		if (pConnect && 
			 GetAddrInfoEx(pName, pServiceName, NS_DNS, NULL, NULL, &pConnect->pResult,
						NULL, &pConnect->m_ovl, pConnect->QueryComplete, &hCancel) != WSA_IO_PENDING)
		{
			DeleteSocket(pConnect, FALSE);	// 失敗時は削除
			pConnect = NULL;
		}
		return pConnect;
	}
};

// IOCP対応サーバークラス(非同期IF拡張)
class CIocpServer : public CIocpServerBase
{
protected:
	struct CSocketCreator	// ソケット作成インターフェース
	{
		virtual CIocpSocket* Create(CSocketFactory* pFactory) = 0;
	};
	template <class PRM1, class PRM2>	// パラメータ格納用テンプレート
	struct CSocketCreatorT : public CSocketCreator
	{
		CSocketCreatorT(const PRM1& param1, const PRM2& param2) : m_param1(param1), m_param2(param2)
		{
		}
	protected:
		PRM1 m_param1;	// 第1パラメータ
		PRM2 m_param2;	// 第2パラメータ
	};
	
	BOOL PostCreator(CSocketCreator* pCreator);	// ソケット作成イベント発行
	VOID OnPostEvent(LPOVERLAPPED_ENTRY lpCPEntry);	// PostEventによるユーザイベント
public:
	CIocpServer(DWORD nThreadCnt = 0);
	
	// サーバソケット作成(スレッドプール外から実行)
	template <class TYPE>	// TYPE: CIocpSocket派生クラス
	BOOL AddListener(USHORT nPort, int nConnections = SOMAXCONN)
	{
		struct CSocketCreatorListen : public CSocketCreatorT<USHORT, int>
		{
			CSocketCreatorListen(USHORT nPort, int nConnections) : CSocketCreatorT(nPort, nConnections)
			{
			}
			CIocpSocket* Create(CSocketFactory* pFactory)	// (スレッドプール内から呼出)
			{
				return pFactory->AddListener<TYPE>(m_param1, m_param2);
			}
		private:
			~CSocketCreatorListen();	// new以外の生成を禁止
		};
		return PostCreator(new CSocketCreatorListen(nPort, nConnections));	// Listenイベント発行
	}
	
	// クライアントソケット作成(スレッドプール外から実行)
	template <class TYPE, class HOST, class PORT>	// TYPE: CIocpSocket派生クラス
	BOOL AddConnection(HOST host, PORT port)	// host: ホスト名, port: ポート番号(プロトコル名)
	{
		struct CSocketCreatorConnect : public CSocketCreatorT<HOST, PORT>
		{
			CSocketCreatorConnect(const HOST& host, const PORT& port) : CSocketCreatorT(host, port)
			{
			}
			CIocpSocket* Create(CSocketFactory* pFactory)	// (スレッドプール内から呼出)
			{
				return pFactory->AddConnection<TYPE>(m_param1, m_param2);
			}
		private:
			~CSocketCreatorConnect();	// new以外の生成を禁止
		};
		return PostCreator(new CSocketCreatorConnect(host, port));	// Connectイベント発行
	}
};
