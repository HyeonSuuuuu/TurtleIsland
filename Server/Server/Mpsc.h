#pragma once
#include "pch.h"
#include <new>
#include "Define.h"

// ====================== MPSC Queue ======================
// 다수 생산자(Worker Thread), 단일 소비자(db_thread) Vyukov MPSC
// 생산자: Treiber free stack, EBR로 회수
// 세션풀과 mq의 ebr 분리
namespace mq
{
	constexpr int MAX_THREAD_ID = MAX_WORKER_THREADS + 3;   // 워커 + db/timer 등 보조 스레드

	constexpr std::size_t CACHE_LINE = std::hardware_destructive_interference_size;

	inline std::atomic_ullong g_epoch{ 1 };

	// 캐시라인 패딩: 스레드별 예약 슬롯을 독립 라인에 둬 false sharing 방지
	struct alignas(CACHE_LINE) Reserv { std::atomic_ullong v{ 0 }; };
	inline Reserv             g_reserv[MAX_THREAD_ID];
	inline std::atomic_int    g_nextTid{ 0 };
	inline thread_local int   tl_tid = -1;

	inline void ensure_registered()
	{
		if (tl_tid < 0)
		{
			tl_tid = g_nextTid.fetch_add(1);
			g_reserv[tl_tid].v.store(ULLONG_MAX);
		}
	}
	inline void start() { ensure_registered(); g_reserv[tl_tid].v.store(g_epoch.load()); }
	inline void end()   { g_reserv[tl_tid].v.store(ULLONG_MAX); }
	inline unsigned long long min_reserv()
	{
		unsigned long long m = ULLONG_MAX;
		for (int i = 0; i < MAX_THREAD_ID; ++i)
		{
			unsigned long long r = g_reserv[i].v.load();
			if (r != 0) m = std::min(m, r);
		}
		return m;
	}
	inline unsigned long long now()  { return g_epoch.load(); }
	inline void               bump() { g_epoch.fetch_add(1); }
}

template<typename T>
class Mpsc
{
	struct Node
	{
		std::atomic<Node*>  next{ nullptr };   // 큐 링크
		Node*               free_next = nullptr;
		unsigned long long  retired_epoch = 0;
		T                   value{};
	};

public:
	Mpsc()
	{
		Node* dummy = new Node();
		m_head = dummy;
		m_tail.store(dummy);
	}

	// 다수 생산자 락프리
	void push(const T& v)
	{
		mq::start();
		Node* n = alloc();
		n->value = v;
		n->next.store(nullptr);
		Node* prev = m_tail.exchange(n);   
		prev->next.store(n);         
		mq::end();
	}

	// 단일 소비자 true 면 out 에 값
	bool try_pop(T& out)
	{
		mq::start();
		Node* h  = m_head;
		Node* nx = h->next.load();
		if (!nx) { mq::end(); return false; }
		out = nx->value;
		m_head = nx;
		retire(h);
		mq::end();
		return true;
	}

private:
	Node* alloc()
	{
		unsigned long long safe = mq::min_reserv();
		Node* t = m_free.load();
		while (t)
		{
			if (t->retired_epoch < safe)
			{
				// epoch-안전한 top 만 재사용 시도(안되면 새로 할당)
				if (m_free.compare_exchange_weak(t, t->free_next))
					return t;
			}
			else break;
		}
		return new Node();
	}

	void retire(Node* n)
	{
		n->retired_epoch = mq::now();
		Node* t = m_free.load();
		do { n->free_next = t; } while (!m_free.compare_exchange_weak(t, n));
		mq::bump();
	}

	std::atomic<Node*> m_tail;        // 생산자 끝
	Node*              m_head;        // 소비자 끝(단일 스레드만 접근)
	std::atomic<Node*> m_free{ nullptr };   // 회수 노드 프리스택(Treiber)
};
