/////////////////////////////////////////////////////////////////////////////
// sockbase.h - ソケット関連のラッパークラス群
//
#pragma once

#define _WINSOCK_DEPRECATED_NO_WARNINGS
//#include <winsock2.h>
#include <ws2tcpip.h>
#include <mswsock.h>

// WinSockの初期化と解放
struct CWSAStartup
{
	CWSAStartup(WSADATA* pWSAData = NULL)
	{
		WSADATA WSAData;
		if (!pWSAData)	// パラメータ省略時
			pWSAData = &WSAData;
		
		WSAStartup(WINSOCK_VERSION, pWSAData);	// 最新バージョンで初期化
	}
	~CWSAStartup()
	{
		WSACleanup();
	}
};
#define WSA_STARTUP CWSAStartup __wsu__;	// WinSock初期化マクロ

// ソケットのアドレス情報(IPv4,IPv6共通)
struct CSockAddrIn : public SOCKADDR_IN6
{
	CSockAddrIn(USHORT port = 0, LPCTSTR addr = NULL)
	{
		::ZeroMemory(this, sizeof(*this));
		sin6_family = AF_INET;
		sin6_port = htons(port);
		
		if (addr)	// アドレス文字列をバイナリへ変換
		{	 // IPv6の最短表記"::"(2) / IPv4の最短表記"x.x.x.x"(7)
			if (lstrlen(addr) >= 7 && (addr[3] == '.' || addr[2] == '.' || addr[1] == '.'))
				InetPton(AF_INET, addr, &sin6_flowinfo);	// flowinfo: SOCKADDR_IN::sin_addrと同位置
			else
				InetPton(sin6_family = AF_INET6, addr, &sin6_addr);
		}
	}
	
	// SOCKADDR構造体による初期化
	CSockAddrIn(const SOCKADDR_IN& addr)
	{
		::CopyMemory(this, &addr, sizeof(addr));
	}
	
	// キャスト演算子によるポートとアドレスの取得
	operator USHORT() const
	{
		return ntohs(sin6_port);
	}
	operator ULONG() const
	{
		return (sin6_family == AF_INET ? sin6_flowinfo : INADDR_NONE);
	}
	
	operator SOCKADDR*()
	{
		return reinterpret_cast<SOCKADDR*>(this);
	}
	operator const SOCKADDR*() const
	{
		return reinterpret_cast<const SOCKADDR*>(this);
	}
	
	operator SOCKADDR_IN*()
	{
		return (sin6_family == AF_INET ? reinterpret_cast<SOCKADDR_IN*>(this) : 0);
	}
	operator const SOCKADDR_IN*() const
	{
		return (sin6_family == AF_INET ? reinterpret_cast<const SOCKADDR_IN*>(this) : 0);
	}
	
	operator SOCKADDR_IN6*()
	{
		return (sin6_family == AF_INET6 ? reinterpret_cast<SOCKADDR_IN6*>(this) : 0);
	}
	operator const SOCKADDR_IN6*() const
	{
		return (sin6_family == AF_INET6 ? reinterpret_cast<const SOCKADDR_IN6*>(this) : 0);
	}
	
	// 代入演算子によるSOCKADDR構造体のコピー
	const SOCKADDR_IN& operator =(const SOCKADDR_IN& addr)
	{
		::CopyMemory(this, &addr, sizeof(addr));
		
		return *(reinterpret_cast<SOCKADDR_IN*>(this));
	}
};

// ソケットAPIのラッパークラス
template <bool t_bManaged = true>	// t_bManaged: デストラクタによるソケットハンドル解放の有無
class CSocketBaseT
{
protected:
	SOCKET m_hSocket;	// ソケットハンドル

public:
	CSocketBaseT(SOCKET hSocket = INVALID_SOCKET) : m_hSocket(hSocket)
	{
	}
	CSocketBaseT(int af, int type = SOCK_STREAM, int protocol = 0)
	{
		Socket(af, type, protocol);
	}
	virtual ~CSocketBaseT()
	{
		if (t_bManaged)
			Close();
	}
	operator SOCKET() const	// キャスト演算子によるソケットハンドル取得
	{
		return m_hSocket;
	}
	
	BOOL IsValid()	// ソケット有効:TRUE
	{
		return m_hSocket != INVALID_SOCKET;
	}
	
	BOOL Socket(int af = AF_INET, int type = SOCK_STREAM, int protocol = 0)
	{
		return m_hSocket == INVALID_SOCKET ?
			((m_hSocket = socket(af, type, protocol)) != INVALID_SOCKET) : FALSE;
	}
	BOOL Bind(const CSockAddrIn& addr)
	{
		return SOCKET_ERROR != bind(m_hSocket, addr, sizeof(addr));
	}
	
	BOOL Listen(int nConnectionBacklog = SOMAXCONN)
	{
		return SOCKET_ERROR != listen(m_hSocket, nConnectionBacklog);
	}
	BOOL Connect(const CSockAddrIn& addr)
	{
		return SOCKET_ERROR != connect(m_hSocket, addr, sizeof(addr));
	}
	SOCKET Accept(CSockAddrIn& addr)
	{
		int iAddrLen = sizeof(addr);
		return accept(m_hSocket, addr, &iAddrLen);
	}
	BOOL Close()
	{
		SOCKET hSocket = m_hSocket;
		m_hSocket = INVALID_SOCKET;	// 解放時はハンドルを初期値に戻す
		return hSocket != INVALID_SOCKET ? (SOCKET_ERROR != closesocket(hSocket)) : FALSE;
	}
	BOOL Shutdown(int iHow = SD_SEND)
	{
		return SOCKET_ERROR != shutdown(m_hSocket, iHow);
	}
	
	int Send(LPCVOID pBuf, int iLen)
	{
		return send(m_hSocket, static_cast<const char*>(pBuf), iLen, 0);
	}
	int Receive(LPVOID pBuf, int iLen)
	{
		return recv(m_hSocket, static_cast<char*>(pBuf), iLen, 0);
	}
	
	int SendTo(LPCVOID pBuf, int iLen, const CSockAddrIn& addr)
	{
		return sendto(m_hSocket, static_cast<const char*>(pBuf), iLen, 0, addr, sizeof(addr));
	}
	int RecvFrom(LPVOID pBuf, int iLen, CSockAddrIn& addr)
	{
		int iFromLen = sizeof(addr);
		return recvfrom(m_hSocket, static_cast<char*>(pBuf), iLen, 0, addr, &iFromLen);
	}
	
	BOOL GetPeerName(CSockAddrIn& addr)
	{
		int iNameLen = sizeof(addr);
		return SOCKET_ERROR != getpeername(m_hSocket, addr, &iNameLen);
	}
	BOOL GetSockName(CSockAddrIn& addr)
	{
		int iNameLen = sizeof(addr);
		return SOCKET_ERROR != getsockname(m_hSocket, addr, &iNameLen);
	}
	
	BOOL SetSockOpt(int optname, const char* optval, int optlen)
	{
		return SOCKET_ERROR != setsockopt(m_hSocket, SOL_SOCKET, optname, optval, optlen);
	}
	BOOL GetSockOpt(int optname, char* optval, int* optlen)
	{
		return SOCKET_ERROR != getsockopt(m_hSocket, SOL_SOCKET, optname, optval, optlen);
	}
	
	BOOL Ioctl(long cmd, u_long* argp)
	{
		return (SOCKET_ERROR != ioctlsocket(m_hSocket, cmd, argp));
	}
	static int GetLastError()
	{
		return WSAGetLastError();
	}
};

typedef CSocketBaseT<false> CSocketHandle;
typedef CSocketBaseT<true> CSocketBase;
