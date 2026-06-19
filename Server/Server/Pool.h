#pragma once
#include "pch.h"
#include "Ebr.h"

// =========================== EBR 기반 고정 객체 Pool =====================================
// 단일 FreeList(vector, lock 사용) + 객체의 retired_epoch stamp 로 안전 재사용
//   - T 는 public 멤버 `unsigned long long retired_epoch` 를 가져야 한다(GameObject 가 보유)
//   - acquire(): retired_epoch < ebr::min_reserv() 인(=참조하는 워커가 없는) 슬롯만 재사용
//   - retire():  stamp 후 프리리스트에 반납, epoch bump
template<typename T, size_t N>
class Pool
{
public:
	Pool()
	{
		m_free.reserve(N);
		for (uint32_t i = static_cast<uint32_t>(N); i-- > 0; )
		{
			m_obj[i].retired_epoch = 0;   // 초기 슬롯은 즉시 사용 가능
			m_free.push_back(i);
		}
	}

	T* Get(uint32_t idx) { return &m_obj[idx]; }

	bool Acquire(uint32_t& out)
	{
		unsigned long long safe = ebr::min_reserv();
		uint32_t found = INVALID_SESSION_INDEX;
		{
			std::lock_guard lock(m_mutex);
			// 뒤에서부터 epoch-안전한 슬롯을 찾아 꺼낸다
			for (int i = static_cast<int>(m_free.size()) - 1; i >= 0; --i)
			{
				uint32_t idx = m_free[i];
				if (m_obj[idx].retired_epoch < safe)
				{
					m_free[i] = m_free.back();
					m_free.pop_back();
					found = idx;
					break;
				}
			}
		}
		if (found == INVALID_SESSION_INDEX)
			return false;   // 가용 슬롯 없음(전부 점유 또는 아직 회수 불안전)

		m_obj[found].Clear();   // 재사용 슬롯 상태 초기화(네트워크+게임 상태) — 잠금 밖
		out = found;
		return true;
	}

	void Retire(uint32_t idx)
	{
		m_obj[idx].retired_epoch = ebr::now();
		{
			std::lock_guard lock(m_mutex);
			m_free.push_back(idx);
		}
		ebr::bump();
	}

private:
	T                     m_obj[N];
	std::vector<uint32_t> m_free;
	std::mutex            m_mutex;
};
