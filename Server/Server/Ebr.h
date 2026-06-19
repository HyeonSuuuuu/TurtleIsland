#pragma once
#include "pch.h"
#include <new>
#include "Define.h"

// 전역 배열 + free함수 + 객체의 retired_epoch 필드 1개로 안전 회수
//
// 사용 규약:
//   - 각 워커 스레드는 시작 시 register_thread(tid) 1회
//   - IO 완료 1건 처리를 start_op()/end_op() 로 감싼다(임계구간)
//   - 풀(Pool)은 retire 시 객체에 now() 를 stamp, 재사용은 stamp < min_reserv() 일 때만
namespace ebr
{
	inline std::atomic_ullong g_epoch{ 1 };

	// 워커별 예약 슬롯을 캐시라인(보통 64B)마다 하나씩 두어 false sharing 방지
	// start_op/end_op 가 IO 완료마다 자기 슬롯을 쓰는 핫패스라, 인접 슬롯이 같은 라인을
	// 공유하면 워커끼리 캐시 무효화 트래픽이 발생한다
	constexpr std::size_t CACHE_LINE = std::hardware_destructive_interference_size;
	struct alignas(CACHE_LINE) Reserv
	{
		std::atomic_ullong v{ 0 };   // 0 = 미예약(무시), ULLONG_MAX = 임계구간 밖
	};
	inline Reserv             g_reserv[MAX_WORKER_THREADS];
	inline thread_local int   tl_tid = -1;

	inline void register_thread(int tid)
	{
		tl_tid = tid;
		g_reserv[tid].v.store(ULLONG_MAX);
	}

	inline void start_op()
	{
		g_reserv[tl_tid].v.store(g_epoch.load());
	}

	inline void end_op()
	{
		g_reserv[tl_tid].v.store(ULLONG_MAX);
	}

	// 모든 워커가 예약한 epoch 중 최소값 0(미등록/미예약) 항목은 무시
	inline unsigned long long min_reserv()
	{
		unsigned long long m = ULLONG_MAX;
		for (uint32_t i = 0; i < MAX_WORKER_THREADS; ++i)
		{
			unsigned long long r = g_reserv[i].v.load();
			if (r != 0) m = std::min(m, r);
		}
		return m;
	}

	inline unsigned long long now()  { return g_epoch.load(); }
	inline void               bump() { g_epoch.fetch_add(1); }
}
