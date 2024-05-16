#include <iostream>
#include <thread>
#include "lockfree_queue.h"
#include <cassert>

constexpr int TESTCOUNT = 1000;

void TestLockFreeQueMultiPushPop() {
	lock_free_queue<int> que;

	std::thread t1([&]() {
		for (int i = 0; i < TESTCOUNT; i++) {
			que.push(std::move(i));
			// std::cout << "push data is " << i << std::endl;
	    	std::this_thread::sleep_for(std::chrono::milliseconds(5));
		}
	});

    std::thread t2([&]() {
		for (int i = TESTCOUNT; i < 2 * TESTCOUNT; i++) {
			que.push(std::move(i));
			// std::cout << "push data is " << i << std::endl;
	    	std::this_thread::sleep_for(std::chrono::milliseconds(5));
		}
	});

	std::thread t3([&]() {
		for (int i = 0; i < TESTCOUNT; i++) {
            std::unique_ptr<int> ptr(que.pop());
            if (ptr.get() == nullptr) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                i--;
                continue;
            }
			std::cout << "pop data is " << *ptr << std::endl;
		}
	});

    std::thread t4([&]() {
		for (int i = 0; i < TESTCOUNT; i++) {
            std::unique_ptr<int> ptr(que.pop());
            if (ptr.get() == nullptr) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                i--;
                continue;
            }
			std::cout << "pop data is " << *ptr << std::endl;
		}
	});

    t1.join();
	t2.join();
    t3.join();
    t4.join();

    assert(que.destruct_count == 2 * TESTCOUNT);
}

int main() {
	TestLockFreeQueMultiPushPop();

    return 0;
}


