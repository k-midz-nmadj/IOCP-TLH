/////////////////////////////////////////////////////////////////////////////
// tlheap.h - �X���b�h���[�J���q�[�v(TLH)
//
#pragma once

#include "thread.h"
#include <new>

#define ISBASE_TYPE(D, B) static_assert(std::is_base_of_v<B, D>, #D " is not derived of " #B)

// �X���b�h���[�J���q�[�v(TLH)�����N���X
template <class THRD = CAPCThread>	// THRD: �X���b�h�����N���X
class CTLHeap : public THRD
{
	ISBASE_TYPE(THRD, CAPCThread);
protected:
	HANDLE m_hHeap;	// ���[�J���q�[�v�n���h��
	
	// �q�[�v�������m��(�擪��TLH�̃|�C���^�i�[�̈��ǉ�)
	static CTLHeap** AllocLH(HANDLE hHeap, SIZE_T dwSize)
	{
		return static_cast<CTLHeap**>(hHeap && dwSize ? 
					::HeapAlloc(hHeap, HEAP_ZERO_MEMORY, sizeof(CTLHeap*) + dwSize) : NULL);
	}
	
	// APC�ɂ��q�[�v�������̔񓯊����
	template <class TYPE>	// �N���X����p
	static VOID NTAPI FreeAPC(ULONG_PTR dwParam)
	{
		reinterpret_cast<TYPE*>(dwParam)->~TYPE();	// �N���X������̓f�X�g���N�^�ďo��
		
		FreeAPC<VOID>(dwParam);
	}
	template <>	// ��ʉ���p��VOID�œ��ꉻ
	static VOID NTAPI FreeAPC<VOID>(ULONG_PTR dwParam)
	{
		CTLHeap** pHead = reinterpret_cast<CTLHeap**>(dwParam) - 1;	// �擪����TLH�擾
		
		::HeapFree(*pHead ? (*pHead)->m_hHeap : ::GetProcessHeap(), 0, pHead);	// �q�[�v���������
	}
public:
	CTLHeap(DWORD dwHeapOpt = HEAP_NO_SERIALIZE, DWORD dwInitSize = 0, DWORD dwMaxSize = 0)
		: m_hHeap(::HeapCreate(dwHeapOpt, dwInitSize, dwMaxSize))	// �f�t�H���g��NoLock�q�[�v�쐬
	{
	}
	~CTLHeap()
	{
		if (m_hHeap)
			::HeapDestroy(m_hHeap);	// �q�[�v�j��
	}
	
	// ���[�J���q�[�v����̃������m��
	LPVOID Alloc(SIZE_T dwSize)	// �T�C�Y�w��
	{
		CTLHeap** pHead = AllocLH(m_hHeap, dwSize);
		if (!pHead)
			return NULL;
		
		*pHead = this;	// �擪��TLH�̃|�C���^�Z�b�g
		return (pHead + 1);	// �㑱�̃A�h���X�ԋp
	}
	template <class TYPE>
	TYPE* Alloc(int n = 1)	// �^�A���w��
	{
		return static_cast<TYPE*>(Alloc(sizeof(TYPE) * n));
	}
	template <class TYPE>
	TYPE* New()	// placement new�ɂ��N���X����(�R���X�g���N�^�N��)
	{
		LPVOID pMem = Alloc(sizeof(TYPE));
		
		return (pMem ? new(pMem) TYPE : NULL);
	}
	
	// �v���Z�X�q�[�v����̃������m��(static��)
	static LPVOID AllocT(SIZE_T dwSize)	// �T�C�Y�w��
	{
		CTLHeap** pHead = AllocLH(::GetProcessHeap(), dwSize);
		
		return (pHead ? pHead + 1 : NULL);
	}
	template <class TYPE>
	static TYPE* AllocT(int n = 1)	// �^�A���w��
	{
		return static_cast<TYPE*>(AllocT(sizeof(TYPE) * n));
	}
	template <class TYPE>
	static TYPE* NewT()	// placement new�ɂ��N���X����(�R���X�g���N�^�N��)
	{
		LPVOID pMem = AllocT(sizeof(TYPE));
		
		return (pMem ? new(pMem) TYPE : NULL);
	}
	
	// ���[�J���q�[�v����̔񓯊����������
	template <class TYPE = VOID>	// TYPE: ����N���X
	static BOOL Free(TYPE* pMem)
	{
		if (!pMem)
			return FALSE;
		
		CTLHeap** pHead = reinterpret_cast<CTLHeap**>(pMem) - 1;	// TLH����X���b�h�Q��
		if (*pHead)
			return (*pHead)->QueueAPC(FreeAPC<TYPE>, pMem);	// �������m�ی��X���b�h�ւ�APC�ɂ�����v��
		
		FreeAPC<TYPE>(reinterpret_cast<ULONG_PTR>(pMem));	// static�łŊm�ۂ����������͓������
		return TRUE;
	}
	
	// TLH�|�C���^�擾
	static CTLHeap* GetHeap(LPVOID pMem)
	{
		return (pMem ? *(static_cast<CTLHeap**>(pMem) - 1) : NULL);
	}
	
	// APC�ɂ��w��X���b�h�ւ̃��b�Z�[�W����
	template <class TYPE, class TPRM>	// TYPE: ���M��X���b�h�ATPRM: ���b�Z�[�W�p�����[�^
	BOOL ApcMsgSend(TYPE* pToTlh, const TPRM& prm)
	{
		ISBASE_TYPE(TYPE, CTLHeap);
		struct CApcParam : public TPRM	// �p�����[�^��`
		{
			TYPE* m_pTlh;	// ���M��X���b�h
			
			static VOID NTAPI ApcTlh(ULONG_PTR dwParam)
			{
				CApcParam* pThis = reinterpret_cast<CApcParam*>(dwParam);
				
				(*pThis)(pThis->m_pTlh);	// �֐��I�u�W�F�N�g�ďo��
				
				CTLHeap::Free<VOID>(pThis);	// ���M���X���b�h�ւ̉���v��
			}
		} *pParam = Alloc<CApcParam>();	// ���b�Z�[�W�쐬
		
		if (!pParam || !pToTlh)
			return FALSE;
		
		*static_cast<TPRM*>(pParam) = prm;	// �p�����[�^�Z�b�g
		pParam->m_pTlh = pToTlh;	// ���M��Z�b�g
		
		return pToTlh->QueueAPC(CApcParam::ApcTlh, pParam);	// ���b�Z�[�W���M
	}
};

// TLH�ɂ�郊�X�g�R���e�i
template <class TYPE, class TALC = CTLHeap<>>	// TYPE: �\���v�f�̊�{�^�ATALC: TLH�A���P�[�^
class CTLHList
{
public:
	class Node	// �A���m�[�h(�����q)��`
	{
		friend class CTLHList;
		Node *pPrev, *pNext;
		
		Node() : pPrev(this), pNext(this)
		{
		}
	public:
		virtual ~Node()	// ������ɑO��̗v�f�ŃV���[�g
		{
			pNext->pPrev = pPrev;
			pPrev->pNext = pNext;
		}
	};
protected:
	Node m_Ring;	// ���X�g�̐擪�Ɩ����ւ̃|�C���^�Ń����O�\�����`��
	
	void AddToHead(Node* pNode)	// �擪�ւ̗v�f�}��
	{
		pNode->pPrev = &m_Ring;
		pNode->pNext = m_Ring.pNext;
		
		m_Ring.pNext->pPrev = pNode;
		m_Ring.pNext = pNode;
	}
	void AddToTail(Node* pNode)	// �����ւ̗v�f�}��
	{
		pNode->pPrev = m_Ring.pPrev;
		pNode->pNext = &m_Ring;
		
		m_Ring.pPrev->pNext = pNode;
		m_Ring.pPrev = pNode;
	}
public:
	~CTLHList()
	{
		RemoveAll();
	}
	
	// �^�ƃA���P�[�^(TLH)�w��ɂ��v�f�ǉ�
	template <class ITEM = TYPE>	// ITEM: �\���v�f�̔h���^
	ITEM* AddItem(TALC* pAlc = NULL, BOOL bToHead = FALSE)
	{
		ISBASE_TYPE(ITEM, TYPE);
		struct NodeT : public Node, public ITEM {	// �ǉ��v�f�̐擪�Ƀm�[�h���t��
		} *pNode = (pAlc ? pAlc->New<NodeT>() : TALC::template NewT<NodeT>());
		
		if (pNode)	// ���������v�f�����X�g�֒ǉ�
			bToHead ? AddToHead(pNode) : AddToTail(pNode);
		
		return pNode;	// �ǉ��v�f�ԋp
	}
	
	// �w��v�f�̍폜(bFind: �w��v�f�̌����L��)
	BOOL DeleteItem(TYPE* pItem, BOOL bFind = FALSE)
	{
		if (!pItem)
			return FALSE;
		
		Node *pNode = reinterpret_cast<Node*>(pItem) - 1, *pFind;
		if (bFind)
		{
			for (pFind = m_Ring.pNext; pFind != &m_Ring && pFind != pNode; pFind = pFind->pNext);
			
			if (pFind == &m_Ring)
				return FALSE;	// �w��v�f�����o�s�Ȃ�G���[�ԋp
		}
		return TALC::Free(pNode);
	}
	static BOOL Delete(TYPE* pItem)	// static��(�����s�v��)
	{
		return (pItem ? TALC::Free(reinterpret_cast<Node*>(pItem) - 1) : FALSE);
	}
	
	void RemoveAll()	// �S�v�f�̍폜
	{
		Node* pNode = m_Ring.pNext;
		while (pNode != &m_Ring)
		{
			Node* pDel = pNode;
			pNode = pNode->pNext;
			TALC::Free(pDel);
		}
	}
	int GetCount()	// �S�v�f���擾(Node�폜���ɐ�����List�̃J�E���^�Q�ƕs��)
	{
		int nCount = 0;
		for (Node* pFind = m_Ring.pNext; pFind != &m_Ring; pFind = pFind->pNext)
			++nCount;
		
		return nCount;
	}
	
	BOOL IsEmpty()	// �󔻒�:TRUE
	{
		return (m_Ring.pNext == &m_Ring);
	}
	Node* GetHead()	// �擪�̔����q�擾
	{
		return m_Ring.pNext;
	}
	Node* GetTail()	// �����̔����q�擾
	{
		return m_Ring.pPrev;
	}
	
	TYPE* GetNext(Node*& pNode)	// �����q����̗v�f�擾(���v�f�ֈړ�)
	{
		TYPE* pItem = NULL;
		if (pNode != &m_Ring)
		{
			pItem = reinterpret_cast<TYPE*>(pNode + 1);
			pNode = pNode->pNext;
		}
		return pItem;	// �������B����NULL�ԋp
	}
	TYPE* GetPrev(Node*& pNode)	// �����q����̗v�f�擾(�O�v�f�ֈړ�)
	{
		TYPE* pItem = NULL;
		if (pNode != &m_Ring)
		{
			pItem = reinterpret_cast<TYPE*>(pNode + 1);
			pNode = pNode->pPrev;
		}
		return pItem;	// �擪���B����NULL�ԋp
	}
};
