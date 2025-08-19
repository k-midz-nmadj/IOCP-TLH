// main.cpp : このファイルには 'main' 関数が含まれています。プログラム実行の開始と終了がそこで行われます。
//
#define _CRT_SECURE_NO_WARNINGS
#include "IocpServer.h"
#include <stdio.h>

// IOCPソケット実装クラス(Echoサーバ)
class CIocpSocketSvr : public CIocpSocket
{
	int OnRead(DWORD dwBytes)	// 受信完了イベント
	{
		CHAR cBuff[64] = "";
		int iRecSize = Receive(cBuff, sizeof(cBuff));	// データ受信
		
		return (iRecSize > 0 ? Write(cBuff, iRecSize) : 0);	// 受信データ返送
	}
};

// IOCPソケット実装クラス(クライアント)
class CIocpSocketClt : public CIocpSocket
{
	CHAR cBuff[64];
	
	BOOL OnConnect(DWORD dwError)	// 接続完了イベント
	{
		if (dwError)	// 接続エラー
		{
			printf("Connect Error(NTSTATUS): 0x%08X\n", dwError);
			return m_pThread->GetIocp()->Stop();	// 終了
		}
		
		printf("\nInput send text[60]: ");
		if (scanf("%61[^\n]", cBuff) != 1)	// 送信テキスト入力待ち
			return m_pThread->GetIocp()->Stop();	// 空入力でサーバ停止
		
		if (cBuff[60])
		{
			scanf("%*[^\n]");	// 超過入力カット
			cBuff[60] = 0;
		}
		scanf("%*c");	// 改行スキップ
		
		if (Write(cBuff, lstrlenA(cBuff)) == SOCKET_ERROR)	// 入力テキスト送信
		{
			printf("Send Error: %d\n", WSAGetLastError());
			m_pThread->GetIocp()->Stop();	// エラー時は終了
		}
		return TRUE;
	}
	
	int OnRead(DWORD dwBytes)	// 受信完了イベント
	{
		int iRecSize = Receive(cBuff, sizeof(cBuff));	// データ受信
		if (iRecSize > 0)	// 受信データあり
		{
			printf("Receive text: %.*s\n", iRecSize, cBuff);
			OnConnect(0);	// 送信テキスト入力
		}
		else
		{
			printf("\n...Disconnected");
			m_pThread->GetIocp()->Stop();	// 切断時は終了
		}
		return iRecSize;
	}
};

// argv[1]:IPアドレス(ドメイン名), argv[2]:ポート番号(プロトコル名)
int wmain(int argc, wchar_t *argv[])
{
	WSA_STARTUP	// Winsock初期化
	const wchar_t* addr = L"127.0.0.1";	// パラメータ省略時はEchoサーバにローカル接続
	USHORT port = 8090;	// Echoサーバ用ポート
	CIocpServerBase srv(argc > 1);	// サーバ起動時(パラメータ省略)のみマルチスレッド
	CSocketFactory* pThread = srv.GetThread();	// ソケット作成用スレッド取得
	
	if (!pThread)
		return 0;
	
	if (argc > 1)
		addr = argv[1];
	else if (!pThread->AddListener<CIocpSocketSvr>(port))	// Echoサーバオープン
		return 0;
	
	if (argc <= 2 || (port = _wtoi(argv[2])) ? 	// ポート番号取得(0:プロトコル名)
		!pThread->AddConnection<CIocpSocketClt>(addr, port) :	// リモート接続
		!pThread->AddConnection<CIocpSocketClt>(addr, argv[2]))	// ドメイン+プロトコル指定はUNICODE限定
		return 0;
	
	printf("Connecting...\n");
	return srv.Start();	// サーバ起動
}

// プログラムの実行: Ctrl + F5 または [デバッグ] > [デバッグなしで開始] メニュー
// プログラムのデバッグ: F5 または [デバッグ] > [デバッグの開始] メニュー

// 作業を開始するためのヒント: 
//   1. ソリューション エクスプローラー ウィンドウを使用してファイルを追加/管理します 
//   2. チーム エクスプローラー ウィンドウを使用してソース管理に接続します
//   3. 出力ウィンドウを使用して、ビルド出力とその他のメッセージを表示します
//   4. エラー一覧ウィンドウを使用してエラーを表示します
//   5. [プロジェクト] > [新しい項目の追加] と移動して新しいコード ファイルを作成するか、[プロジェクト] > [既存の項目の追加] と移動して既存のコード ファイルをプロジェクトに追加します
//   6. 後ほどこのプロジェクトを再び開く場合、[ファイル] > [開く] > [プロジェクト] と移動して .sln ファイルを選択します
