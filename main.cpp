// main.cpp : このファイルには 'main' 関数が含まれています。プログラム実行の開始と終了がそこで行われます。
//
#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include "IocpServer.h"

#define PORT_NUM 8090

// IOCPソケット実装クラス(サンプル)
class CIocpSocketImpl : public CIocpSocket
{
	BOOL OnAccept(const CSockAddrIn* pAddrRemote)	// 接続受入イベント
	{
	#define ACP_MSG "IOCP Server Accepted."
		if (Write(ACP_MSG, sizeof(ACP_MSG)) < 0)	// メッセージ送信
			Close();
		
		return TRUE;
	}
	BOOL OnConnect(DWORD dwError)	// 接続完了イベント
	{
	#define CON_MSG "IOCP Client Connected."

		return (!dwError && Write(CON_MSG, sizeof(CON_MSG)) > 0);	// メッセージ送信
	}
	
	int OnRead(DWORD dwBytes)	// 受信完了イベント
	{
		CHAR cBuff[64] = "";
		int iRecSize = Receive(cBuff, sizeof(cBuff));	// データ受信
		
		if (iRecSize > 0)	// 受信データあり
		{	// サーバオブジェクト取得
			CIocpServer* pServer = static_cast<CIocpServer*>(m_pThread->GetIocp());
			if (lstrcmpA(cBuff, "connect") == 0)	// 接続追加
			{
				//m_pThread->ApcMsgSend(pServer, CIocpServer::AddPort<CIocpSocketImpl>(PORT_NUM));
				//m_pThread->ApcMsgSend(pServer, CIocpServer::DelPort(PORT_NUM));
				
				//m_pThread->AddConnection<CIocpSocketImpl>(TEXT("www.google.com"), TEXT("443"));
				m_pThread->AddConnection<CIocpSocketImpl>(CSockAddrIn("127.0.0.1", PORT_NUM));
			}
			else if (lstrcmpA(cBuff, "stop") == 0)	// サーバ停止
				pServer->Stop();
			else
			{
				//iRecSize = Write(cBuff, iRecSize);
				printf("Receive text: %.*s\n", iRecSize, cBuff);
			}
		}
		return iRecSize;
	}
};

int main()
{
	WSA_STARTUP	// Winsock初期化
	CIocpServer svr;
	
	if (!svr.Listen<CIocpSocketImpl, false>(PORT_NUM))	// サーバ起動
		return -1;
	
	// クライアント接続開始
	//svr.ApcMsgSend(&svr, CIocpServer::DelPort(PORT_NUM));
	CIocpSocket* pClient = svr.AddConnection<CIocpSocketImpl>(CSockAddrIn("127.0.0.1", PORT_NUM));
	if (pClient == NULL)
		return -1;
	
	for (;;)
	{
		char cBuff[18] = "";
		
		::Sleep(500);
		if (svr.IsRunning() == FALSE)
			break;	// サーバ停止=>終了
		
		printf("Input send text[16]: ");
		if (scanf("%17[^\n]", cBuff) != 1 || !cBuff[0])	// 送信テキスト入力
			break;	// 入力なし=>終了
		
		if (cBuff[16])
		{
			scanf("%*[^\n]");	// 超過入力カット
			cBuff[16] = 0;
		}
		scanf("%*c");	// 改行スキップ
		
		if (svr.FindSocket(pClient) == FALSE)	// クライアント接続なし(切断済み)
		{
			printf("Timeout: Server Shutdown\n");
			break;
		}
		if (pClient->Send(cBuff, lstrlenA(cBuff)) == SOCKET_ERROR)	// 入力テキスト送信
		{
			printf("Socket Error: %d\n", WSAGetLastError());
			break;
		}
	}
	return 0;
}

// プログラムの実行: Ctrl + F5 または [デバッグ] > [デバッグなしで開始] メニュー
// プログラムのデバッグ: F5 または [デバッグ] > [デバッグの開始] メニュー

// 作業を開始するためのヒント: 
//    1. ソリューション エクスプローラー ウィンドウを使用してファイルを追加/管理します 
//   2. チーム エクスプローラー ウィンドウを使用してソース管理に接続します
//   3. 出力ウィンドウを使用して、ビルド出力とその他のメッセージを表示します
//   4. エラー一覧ウィンドウを使用してエラーを表示します
//   5. [プロジェクト] > [新しい項目の追加] と移動して新しいコード ファイルを作成するか、[プロジェクト] > [既存の項目の追加] と移動して既存のコード ファイルをプロジェクトに追加します
//   6. 後ほどこのプロジェクトを再び開く場合、[ファイル] > [開く] > [プロジェクト] と移動して .sln ファイルを選択します