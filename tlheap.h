/////////////////////////////////////////////////////////////////////////////
// tlheap.h - スレッドローカルヒープ(TLH)
//
#pragma once

#include "thread.h"
#include <new>

#define ISBASE_TYPE(D, B) static_assert(std::is_base_of_v<B, D>, #D " is not derived of " #B)

// スレッドローカルヒープ(TLH)実装クラス
template <class THRD = CAPCThread>	// THRD: スレッド実装クラス
class CTLHeap : public THRD
{
	ISBASE_TYPE(THRD, CAPCThread);
protected:
	HANDLE m_hHeap;	// ローカルヒープハンドル
	
	// ヒープメモリ確保(先頭にTLHのポインタ格納領域を追加)
	static CTLHeap** AllocLH(HANDLE hHeap, SIZE_T dwSize)
	{
		return static_cast<CTLHeap**>(hHeap && dwSize ? 
					::HeapAlloc(hHeap, HEAP_ZERO_MEMORY, sizeof(CTLHeap*) + dwSize) : NULL);
	}
	
	// APCによるヒープメモリの非同期解放
	template <class TYPE>	// クラス解放用
	static VOID NTAPI FreeAPC(ULONG_PTR dwParam)
	{
		reinterpret_cast<TYPE*>(dwParam)->~TYPE();	// クラス解放時はデストラクタ呼出し
		
		FreeAPC<VOID>(dwParam);
	}
	template <>	// 一般解放用にVOIDで特殊化
	static VOID NTAPI FreeAPC<VOID>(ULONG_PTR dwParam)
	{
		CTLHeap** pHead = reinterpret_cast<CTLHeap**>(dwParam) - 1;	// 先頭からTLH取得
		
		::HeapFree(*pHead ? (*pHead)->m_hHeap : ::GetProcessHeap(), 0, pHead);	// ヒープメモリ解放
	}
public:
	CTLHeap(DWORD dwHeapOpt = HEAP_NO_SERIALIZE, DWORD dwInitSize = 0, DWORD dwMaxSize = 0)
		: m_hHeap(::HeapCreate(dwHeapOpt, dwInitSize, dwMaxSize))	// デフォルトでNoLockヒープ作成
	{
	}
	~CTLHeap()
	{
		if (m_hHeap)
			::HeapDestroy(m_hHeap);	// ヒープ破棄
	}
	
	// ローカルヒープからのメモリ確保
	LPVOID Alloc(SIZE_T dwSize)	// サイズ指定
	{
		CTLHeap** pHead = AllocLH(m_hHeap, dwSize);
		if (!pHead)
			return NULL;
		
		*pHead = this;	// 先頭にTLHのポインタセット
		return (pHead + 1);	// 後続のアドレス返却
	}
	template <class TYPE>
	TYPE* Alloc(int n = 1)	// 型、個数指定
	{
		return static_cast<TYPE*>(Alloc(sizeof(TYPE) * n));
	}
	template <class TYPE>
	TYPE* New()	// placement newによるクラス生成(コンストラクタ起動)
	{
		LPVOID pMem = Alloc(sizeof(TYPE));
		
		return (pMem ? new(pMem) TYPE : NULL);
	}
	
	// プロセスヒープからのメモリ確保(static版)
	static LPVOID AllocT(SIZE_T dwSize)	// サイズ指定
	{
		CTLHeap** pHead = AllocLH(::GetProcessHeap(), dwSize);
		
		return (pHead ? pHead + 1 : NULL);
	}
	template <class TYPE>
	static TYPE* AllocT(int n = 1)	// 型、個数指定
	{
		return static_cast<TYPE*>(AllocT(sizeof(TYPE) * n));
	}
	template <class TYPE>
	static TYPE* NewT()	// placement newによるクラス生成(コンストラクタ起動)
	{
		LPVOID pMem = AllocT(sizeof(TYPE));
		
		return (pMem ? new(pMem) TYPE : NULL);
	}
	
	// ローカルヒープからの非同期メモリ解放
	template <class TYPE = VOID>	// TYPE: 解放クラス
	static BOOL Free(TYPE* pMem)
	{
		if (!pMem)
			return FALSE;
		
		CTLHeap** pHead = reinterpret_cast<CTLHeap**>(pMem) - 1;	// TLHからスレッド参照
		if (*pHead)
			return (*pHead)->QueueAPC(FreeAPC<TYPE>, pMem);	// メモリ確保元スレッドへのAPCによる解放要求
		
		FreeAPC<TYPE>(reinterpret_cast<ULONG_PTR>(pMem));	// static版で確保したメモリは同期解放
		return TRUE;
	}
	
	// TLHポインタ取得
	static CTLHeap* GetHeap(LPVOID pMem)
	{
		return (pMem ? *(static_cast<CTLHeap**>(pMem) - 1) : NULL);
	}
	
	// APCによる指定スレッドへのメッセージ処理
	template <class TYPE, class TPRM>	// TYPE: 送信先スレッド、TPRM: メッセージパラメータ
	BOOL ApcMsgSend(TYPE* pToTlh, const TPRM& prm)
	{
		ISBASE_TYPE(TYPE, CTLHeap);
		struct CApcParam : public TPRM	// パラメータ定義
		{
			TYPE* m_pTlh;	// 送信先スレッド
			
			static VOID NTAPI ApcTlh(ULONG_PTR dwParam)
			{
				CApcParam* pThis = reinterpret_cast<CApcParam*>(dwParam);
				
				(*pThis)(pThis->m_pTlh);	// 関数オブジェクト呼出し
				
				CTLHeap::Free<VOID>(pThis);	// 送信元スレッドへの解放要求
			}
		} *pParam = Alloc<CApcParam>();	// メッセージ作成
		
		if (!pParam || !pToTlh)
			return FALSE;
		
		*static_cast<TPRM*>(pParam) = prm;	// パラメータセット
		pParam->m_pTlh = pToTlh;	// 送信先セット
		
		return pToTlh->QueueAPC(CApcParam::ApcTlh, pParam);	// メッセージ送信
	}
};

// TLHによるリストコンテナ
template <class TYPE, class TALC = CTLHeap<>>	// TYPE: 構成要素の基本型、TALC: TLHアロケータ
class CTLHList
{
public:
	class Node	// 連結ノード(反復子)定義
	{
		friend class CTLHList;
		Node *pPrev, *pNext;
		
		Node() : pPrev(this), pNext(this)
		{
		}
	public:
		virtual ~Node()	// 解放時に前後の要素でショート
		{
			pNext->pPrev = pPrev;
			pPrev->pNext = pNext;
		}
	};
protected:
	Node m_Ring;	// リストの先頭と末尾へのポインタでリング構造を形成
	
	void AddToHead(Node* pNode)	// 先頭への要素挿入
	{
		pNode->pPrev = &m_Ring;
		pNode->pNext = m_Ring.pNext;
		
		m_Ring.pNext->pPrev = pNode;
		m_Ring.pNext = pNode;
	}
	void AddToTail(Node* pNode)	// 末尾への要素挿入
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
	
	// 型とアロケータ(TLH)指定による要素追加
	template <class ITEM = TYPE>	// ITEM: 構成要素の派生型
	ITEM* AddItem(TALC* pAlc = NULL, BOOL bToHead = FALSE)
	{
		ISBASE_TYPE(ITEM, TYPE);
		struct NodeT : public Node, public ITEM {	// 追加要素の先頭にノード情報付加
		} *pNode = (pAlc ? pAlc->New<NodeT>() : TALC::template NewT<NodeT>());
		
		if (pNode)	// 生成した要素をリストへ追加
			bToHead ? AddToHead(pNode) : AddToTail(pNode);
		
		return pNode;	// 追加要素返却
	}
	
	// 指定要素の削除(bFind: 指定要素の検索有無)
	BOOL DeleteItem(TYPE* pItem, BOOL bFind = FALSE)
	{
		if (!pItem)
			return FALSE;
		
		Node *pNode = reinterpret_cast<Node*>(pItem) - 1, *pFind;
		if (bFind)
		{
			for (pFind = m_Ring.pNext; pFind != &m_Ring && pFind != pNode; pFind = pFind->pNext);
			
			if (pFind == &m_Ring)
				return FALSE;	// 指定要素が検出不可ならエラー返却
		}
		return TALC::Free(pNode);
	}
	static BOOL Delete(TYPE* pItem)	// static版(検索不要時)
	{
		return (pItem ? TALC::Free(reinterpret_cast<Node*>(pItem) - 1) : FALSE);
	}
	
	void RemoveAll()	// 全要素の削除
	{
		Node* pNode = m_Ring.pNext;
		while (pNode != &m_Ring)
		{
			Node* pDel = pNode;
			pNode = pNode->pNext;
			TALC::Free(pDel);
		}
	}
	int GetCount()	// 全要素数取得(Node削除時に生成元Listのカウンタ参照不可)
	{
		int nCount = 0;
		for (Node* pFind = m_Ring.pNext; pFind != &m_Ring; pFind = pFind->pNext)
			++nCount;
		
		return nCount;
	}
	
	BOOL IsEmpty()	// 空判定:TRUE
	{
		return (m_Ring.pNext == &m_Ring);
	}
	Node* GetHead()	// 先頭の反復子取得
	{
		return m_Ring.pNext;
	}
	Node* GetTail()	// 末尾の反復子取得
	{
		return m_Ring.pPrev;
	}
	
	TYPE* GetNext(Node*& pNode)	// 反復子からの要素取得(次要素へ移動)
	{
		TYPE* pItem = NULL;
		if (pNode != &m_Ring)
		{
			pItem = reinterpret_cast<TYPE*>(pNode + 1);
			pNode = pNode->pNext;
		}
		return pItem;	// 末尾到達時はNULL返却
	}
	TYPE* GetPrev(Node*& pNode)	// 反復子からの要素取得(前要素へ移動)
	{
		TYPE* pItem = NULL;
		if (pNode != &m_Ring)
		{
			pItem = reinterpret_cast<TYPE*>(pNode + 1);
			pNode = pNode->pPrev;
		}
		return pItem;	// 先頭到達時はNULL返却
	}
};
