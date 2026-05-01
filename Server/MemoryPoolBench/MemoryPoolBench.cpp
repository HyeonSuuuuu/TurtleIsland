// MemoryPoolBench.cpp : 이 파일에는 'main' 함수가 포함됩니다. 거기서 프로그램 실행이 시작되고 종료됩니다.
//

import Common;
import QueuePool;
import ListPool;
import User;

template<typename T>
void Benchmark(const char* name, T& pool, int count)
{
	auto start = std::chrono::high_resolution_clock::now();
	for (int i = 0; i < count; ++i)
	{
		pool.Free(pool.Alloc());
	}
	auto end = std::chrono::high_resolution_clock::now();

	auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
	std::println("{}: {}ns", name, ns);
}

// data segment에 미리 할당
std::array<User, 10000> g_users;

int main()
{
	
	// session_id or index 비교
	std::println("uint32 비교");
	QueuePool queuePool(sizeof(uint32), 10000);
	ListPool listPool(sizeof(uint32), 10000);

	Benchmark("QueuePool", queuePool, 100'0000);
	Benchmark("ListPool", listPool, 100'0000);

	std::println("User 비교");
	// Session 객체 비교
	QueuePool queueSessionPool(sizeof(User), 10000, reinterpret_cast<byte*>(g_users.data()));
	ListPool  listSessionPool (sizeof(User), 10000, reinterpret_cast<byte*>(g_users.data()));
	
	Benchmark("QueueSessionPool", queueSessionPool, 100'0000);
	Benchmark("ListSessionPool", listSessionPool, 100'0000);
	// OverlappedEx 비교

}

