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
union CSockAddrIn
{
	struct	// Inet共通メンバ
	{
		ADDRESS_FAMILY sin_family;
		USHORT sin_port;
		CHAR sin_data[28];	// AcceptExで使用するための必須サイズ(32byte)定義
	};
	SOCKADDR_IN Ipv4;
	SOCKADDR_IN6 Ipv6;
	
	CSockAddrIn(USHORT port = 0, LPCTSTR addr = NULL) : sin_port(htons(port)), sin_data{}
	{
		sin_family = (addr &&
			InetPton(AF_INET,  addr, &Ipv4.sin_addr) <= 0 && // アドレス文字列をバイナリへ変換
			InetPton(AF_INET6, addr, &Ipv6.sin6_addr) > 0 ?  // IPv4で不正時はIPv6で再試行
				AF_INET6 : AF_INET);
	}
	
	// SOCKADDR構造体による初期化
	CSockAddrIn(const SOCKADDR_IN& addr)
	{
		Ipv4 = addr;
	}
	CSockAddrIn(const SOCKADDR_IN6& addr)
	{
		Ipv6 = addr;
	}
	
	// キャスト演算子によるポートとIPv4アドレスの取得
	operator USHORT() const
	{
		return ntohs(sin_port);
	}
	operator ULONG() const
	{
		return (sin_family == AF_INET ? Ipv4.sin_addr.s_addr : INADDR_NONE);
	}
	
	// SOCKADDR構造体へのキャスト
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
		return (sin_family == AF_INET ? &Ipv4 : 0);
	}
	operator SOCKADDR_IN6*()
	{
		return (sin_family == AF_INET6 ? &Ipv6 : 0);
	}
	operator SOCKADDR_IN&()
	{
		return Ipv4;
	}
	operator SOCKADDR_IN6&()
	{
		return Ipv6;
	}
	
	// 代入演算子によるSOCKADDR構造体のコピー
	const SOCKADDR_IN& operator =(const SOCKADDR_IN& addr)
	{
		return (Ipv4 = addr);
	}
	const SOCKADDR_IN6& operator =(const SOCKADDR_IN6& addr)
	{
		return (Ipv6 = addr);
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
		return (m_hSocket != INVALID_SOCKET);
	}
	
	BOOL Socket(int af = AF_INET, int type = SOCK_STREAM, int protocol = 0)
	{
		return (m_hSocket == INVALID_SOCKET ?
			((m_hSocket = socket(af, type, protocol)) != INVALID_SOCKET) : FALSE);
	}
	BOOL Bind(const CSockAddrIn& addr)
	{
		return (SOCKET_ERROR != bind(m_hSocket, addr, sizeof(addr)));
	}
	
	BOOL Listen(int nConnectionBacklog = SOMAXCONN)
	{
		return (SOCKET_ERROR != listen(m_hSocket, nConnectionBacklog));
	}
	BOOL Connect(const CSockAddrIn& addr)
	{
		return (SOCKET_ERROR != connect(m_hSocket, addr, sizeof(addr)));
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
		return (hSocket != INVALID_SOCKET ? (SOCKET_ERROR != closesocket(hSocket)) : FALSE);
	}
	BOOL Shutdown(int iHow = SD_SEND)
	{
		return (SOCKET_ERROR != shutdown(m_hSocket, iHow));
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
		return (SOCKET_ERROR != getpeername(m_hSocket, addr, &iNameLen));
	}
	BOOL GetSockName(CSockAddrIn& addr)
	{
		int iNameLen = sizeof(addr);
		return (SOCKET_ERROR != getsockname(m_hSocket, addr, &iNameLen));
	}
	
	BOOL SetSockOpt(int optname, const char* optval, int optlen)
	{
		return (SOCKET_ERROR != setsockopt(m_hSocket, SOL_SOCKET, optname, optval, optlen));
	}
	BOOL GetSockOpt(int optname, char* optval, int* optlen)
	{
		return (SOCKET_ERROR != getsockopt(m_hSocket, SOL_SOCKET, optname, optval, optlen));
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
