// main.cpp : ���̃t�@�C���ɂ� 'main' �֐����܂܂�Ă��܂��B�v���O�������s�̊J�n�ƏI���������ōs���܂��B
//
#define _CRT_SECURE_NO_WARNINGS
#include "IocpServer.h"
#include <stdio.h>

// IOCP�\�P�b�g�����N���X(Echo�T�[�o)
class CIocpSocketSvr : public CIocpSocket
{
	int OnRead(DWORD dwBytes)	// ��M�����C�x���g
	{
		CHAR cBuff[64] = "";
		int iRecSize = Receive(cBuff, sizeof(cBuff));	// �f�[�^��M
		
		return (iRecSize > 0 ? Write(cBuff, iRecSize) : 0);	// ��M�f�[�^�ԑ�
	}
};

// IOCP�\�P�b�g�����N���X(�N���C�A���g)
class CIocpSocketClt : public CIocpSocket
{
	CHAR cBuff[64];
	
	BOOL OnConnect(DWORD dwError)	// �ڑ������C�x���g
	{
		if (dwError)	// �ڑ��G���[
		{
			printf("Connect Error(NTSTATUS): 0x%08X\n", dwError);
			return m_pThread->GetIocp()->Stop();	// �I��
		}
		
		printf("\nInput send text[60]: ");
		if (scanf("%61[^\n]", cBuff) != 1)	// ���M�e�L�X�g���͑҂�
			return m_pThread->GetIocp()->Stop();	// ����͂ŃT�[�o��~
		
		if (cBuff[60])
		{
			scanf("%*[^\n]");	// ���ߓ��̓J�b�g
			cBuff[60] = 0;
		}
		scanf("%*c");	// ���s�X�L�b�v
		
		if (Write(cBuff, lstrlenA(cBuff)) == SOCKET_ERROR)	// ���̓e�L�X�g���M
		{
			printf("Send Error: %d\n", WSAGetLastError());
			m_pThread->GetIocp()->Stop();	// �G���[���͏I��
		}
		return TRUE;
	}
	
	int OnRead(DWORD dwBytes)	// ��M�����C�x���g
	{
		int iRecSize = Receive(cBuff, sizeof(cBuff));	// �f�[�^��M
		if (iRecSize > 0)	// ��M�f�[�^����
		{
			printf("Receive text: %.*s\n", iRecSize, cBuff);
			OnConnect(0);	// ���M�e�L�X�g����
		}
		else
		{
			printf("\n...Disconnected");
			m_pThread->GetIocp()->Stop();	// �ؒf���͏I��
		}
		return iRecSize;
	}
};

// argv[1]:IP�A�h���X(�h���C����), argv[2]:�|�[�g�ԍ�(�v���g�R����)
int wmain(int argc, wchar_t *argv[])
{
	WSA_STARTUP	// Winsock������
	const wchar_t* addr = L"127.0.0.1";	// �p�����[�^�ȗ�����Echo�T�[�o�Ƀ��[�J���ڑ�
	USHORT port = 8090;	// Echo�T�[�o�p�|�[�g
	CIocpServerBase srv(argc > 1);	// �T�[�o�N����(�p�����[�^�ȗ�)�̂݃}���`�X���b�h
	CSocketFactory* pThread = srv.GetThread();	// �\�P�b�g�쐬�p�X���b�h�擾
	
	if (!pThread)
		return 0;
	
	if (argc > 1)
		addr = argv[1];
	else if (!pThread->AddListener<CIocpSocketSvr>(port))	// Echo�T�[�o�I�[�v��
		return 0;
	
	if (argc <= 2 || (port = _wtoi(argv[2])) ? 	// �|�[�g�ԍ��擾(0:�v���g�R����)
		!pThread->AddConnection<CIocpSocketClt>(addr, port) :	// �����[�g�ڑ�
		!pThread->AddConnection<CIocpSocketClt>(addr, argv[2]))	// �h���C��+�v���g�R���w���UNICODE����
		return 0;
	
	printf("Connecting...\n");
	return srv.Start();	// �T�[�o�N��
}

// �v���O�����̎��s: Ctrl + F5 �܂��� [�f�o�b�O] > [�f�o�b�O�Ȃ��ŊJ�n] ���j���[
// �v���O�����̃f�o�b�O: F5 �܂��� [�f�o�b�O] > [�f�o�b�O�̊J�n] ���j���[

// ��Ƃ��J�n���邽�߂̃q���g: 
//   1. �\�����[�V���� �G�N�X�v���[���[ �E�B���h�E���g�p���ăt�@�C����ǉ�/�Ǘ����܂� 
//   2. �`�[�� �G�N�X�v���[���[ �E�B���h�E���g�p���ă\�[�X�Ǘ��ɐڑ����܂�
//   3. �o�̓E�B���h�E���g�p���āA�r���h�o�͂Ƃ��̑��̃��b�Z�[�W��\�����܂�
//   4. �G���[�ꗗ�E�B���h�E���g�p���ăG���[��\�����܂�
//   5. [�v���W�F�N�g] > [�V�������ڂ̒ǉ�] �ƈړ����ĐV�����R�[�h �t�@�C�����쐬���邩�A[�v���W�F�N�g] > [�����̍��ڂ̒ǉ�] �ƈړ����Ċ����̃R�[�h �t�@�C�����v���W�F�N�g�ɒǉ����܂�
//   6. ��قǂ��̃v���W�F�N�g���ĂъJ���ꍇ�A[�t�@�C��] > [�J��] > [�v���W�F�N�g] �ƈړ����� .sln �t�@�C����I�����܂�
