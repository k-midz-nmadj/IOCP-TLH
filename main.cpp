// main.cpp : このファイルには 'main' 関数が含まれています。プログラム実行の開始と終了がそこで行われます。
//
#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include "IocpServer.h"

#define PORT_NUM 8090
#define ACP_MSG "IOCP Server Accepted."
#define CON_MSG "IOCP Client Connected."

// IOCPソケット実装クラス(Echoサーバ)
class CIocpSocketSvr : public CIocpSocket
{
	BOOL OnAccept(const CSockAddrIn* pAddrRemote)	// 接続受入イベント
	{
		if (Write(ACP_MSG, sizeof(ACP_MSG)) < 0)	// メッセージ送信
			Close();
		
		return TRUE;
	}
	
	int OnRead(DWORD dwBytes)	// 受信完了イベント
	{
		CHAR cBuff[64] = "";
		int iRecSize = Receive(cBuff, sizeof(cBuff));	// データ受信
		
		return <iRecSize > 0 ? Write(cBuff, iRecSize) : 0);	// 受信データ返送
	}
};

// IOCPソケット実装クラス(クライアント)
class CIocpSocketClt : public CIocpSocket
{
	BOOL OnConnect(DWORD dwError)	// 接続完了イベント
	{
		if (dwError)	// 接続エラー
			printf("Connect Error(NTSTATUS): 0x%08X\n", dwError);
		
		return !dwError;
	}
	
	int OnRead(DWORD dwBytes)	// 受信完了イベント
	{
		CHAR cBuff[64] = "";
		int iRecSize = Receive(cBuff, sizeof(cBuff));	// データ受信
		if (iRecSize <= 0)	// 受信データなし
			return iRecSize;// 切断
		
		if (lstrcmpA(cBuff, "connect"))	// 接続追加
		{
			printf("Receive text: %.*s\n\nInput send text[16]: ", iRecSize, cBuff);
			if (scanf("%17[^\n]", cBuff) != 1)	// 送信テキスト入力待ち
			{
				m_pThread->GetIocp()->Stop();	// 空入力でサーバ停止
				return 0;
			}
			
			if (cBuff[16])
			{
				scanf("%*[^\n]");	// 超過入力カット
				cBuff[16] = 0;
			}
			scanf("%*c");	// 改行スキップ
			
			if (Write(cBuff, lstrlenA(cBuff)) != SOCKET_ERROR)	// 入力テキスト送信
				return iRecSize;
			
			printf("Socket Error: %d\n", WSAGetLastError());	// エラー時は新規接続追加
		}
		m_pThread->AddConnection<CIocpSocketClt>(CSockAddrIn("127.0.0.1", PORT_NUM));
		//m_pThread->AddConnection<CIocpSocketClt>(TEXT("www.google.com"), TEXT("https"));
		
		return 0;	// 切断⇒新規接続
	}
};

int main()
{
	WSA_STARTUP	// Winsock初期化
	CIocpServer svr;
	
	if (!svr.AddListener<CIocpSocketSvr>(PORT_NUM))	// ポートオープン
		return -1;
	
	if (!svr.AddConnection<CIocpSocketClt>("127.0.0.1", PORT_NUM))	// ローカル接続
		return -1;
	
	return svr.Start();	// サーバ起動
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
