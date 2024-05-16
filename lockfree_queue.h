#pragma once

#include <atomic>

template <typename T>
class lock_free_queue {
public:
    lock_free_queue() {
        counted_node_ptr new_next;
		new_next.ptr = new node();
		new_next.external_count = 1;
		tail.store(new_next);
		head.store(new_next);
    }
    lock_free_queue(const lock_free_queue&) = delete;
    lock_free_queue& operator=(const lock_free_queue&) = delete;
    ~lock_free_queue() {
        while (pop());
        auto head_counted_node = head.load();
        delete head_counted_node.ptr;   
    }

    void push(T&& new_value) {
        std::unique_ptr<T> new_data(new T(new_value));
        counted_node_ptr new_next;
        new_next.ptr = new node;
        new_next.external_count = 1;
        counted_node_ptr old_tail = tail.load(std::memory_order_relaxed);
        for (;;) {
            increase_external_count(tail, old_tail);
            T* old_data = nullptr;
            if (old_tail.ptr->data.compare_exchange_strong(old_data, new_data.get())) {
                counted_node_ptr old_next;
                if (!old_tail.ptr->next.compare_exchange_strong(old_next, new_next)) {
                    delete new_next.ptr;
                    new_next = old_next;    // update to current old_tail.ptr->next
                }
                set_new_tail(old_tail, new_next);
                new_data.release();
                break;
            } else {
                counted_node_ptr old_next;
                if (old_tail.ptr->next.compare_exchange_strong(old_next, new_next)) {
                    old_next = new_next;    // update to current old_tail.ptr->next
                    new_next.ptr = new node;    // for next iteration
                }
                set_new_tail(old_tail, old_next);
            }
        }
    }

    // std::unique_ptr<T> pop() {
    T* pop() {
        counted_node_ptr old_head = head.load(std::memory_order_relaxed);
        for (;;) {
            increase_external_count(head, old_head);
            node* const ptr = old_head.ptr;
            if (ptr == tail.load().ptr) {
                ptr->release_ref();
                // return std::unique_ptr<T>();
                return nullptr;
            }
            counted_node_ptr next = ptr->next.load();
            if (head.compare_exchange_strong(old_head, next)) {
                T* const res = ptr->data.exchange(nullptr);
                free_external_counter(old_head);
                // return std::unique_ptr<T>(res);
                return res;
            }
            ptr->release_ref();
        }
    }

    static std::atomic<int> destruct_count;

private:
    struct node;
    struct counted_node_ptr {
        int external_count;
        node* ptr;
        counted_node_ptr() noexcept: external_count(0), ptr(nullptr) {}
    };

    struct node_counter {
        unsigned internal_counters:30;
        unsigned external_counters:2;
    };

    struct node {
        std::atomic<T*> data;
        std::atomic<node_counter> count;
        std::atomic<counted_node_ptr> next;

        node() {
            data.store(nullptr);

            node_counter new_count;
            new_count.internal_counters = 0;
            new_count.external_counters = 2;    // tail and next
            count.store(new_count);

            counted_node_ptr new_next;
            new_next.ptr = nullptr;
            new_next.external_count = 0;
            next.store(new_next);
        }

        void release_ref() {
            node_counter old_counter = count.load(std::memory_order_relaxed);
            node_counter new_counter;
            do {
                new_counter = old_counter;
                new_counter.internal_counters--;
            } while(!count.compare_exchange_strong(old_counter, new_counter, std::memory_order_acquire, std::memory_order_relaxed));  // RMW
            if (!new_counter.internal_counters && !new_counter.external_counters) {
                delete this;
                destruct_count.fetch_add(1);
            }
        }
    };

    std::atomic<counted_node_ptr> head;
    std::atomic<counted_node_ptr> tail;

    static void increase_external_count(std::atomic<counted_node_ptr>& counter, counted_node_ptr& old_counter) {
        counted_node_ptr new_counter;
        do {
            // old_counter = counter.load(std::memory_order_acquire);   // if cas fail, old_counter will be updated automatically
            new_counter = old_counter;
            new_counter.external_count++;
        } while (!counter.compare_exchange_strong(old_counter, new_counter, std::memory_order_acquire, std::memory_order_relaxed));
        old_counter.external_count = new_counter.external_count;
    }

    static void free_external_counter(counted_node_ptr& old_node_ptr) {
        node* const ptr = old_node_ptr.ptr;
        int count_increase = old_node_ptr.external_count - 2;   // original: 1, itself: 1
        node_counter old_counter = ptr->count.load(std::memory_order_relaxed);
        node_counter new_counter;
        do {
            new_counter = old_counter;
            new_counter.external_counters--;
            new_counter.internal_counters += count_increase;    // update from external_count
        } while (!ptr->count.compare_exchange_strong(old_counter, new_counter, std::memory_order_acquire, std::memory_order_relaxed));
        if (!new_counter.internal_counters && !new_counter.external_counters) {
            delete ptr;
            destruct_count.fetch_add(1);
        }
    }

    void set_new_tail(counted_node_ptr& old_tail, counted_node_ptr const &new_tail) {
        node* const cur_tail_ptr = old_tail.ptr;
        // prevent `increase_external_count` overwritten
        while (!tail.compare_exchange_strong(old_tail, new_tail) && old_tail.ptr == cur_tail_ptr);
        
        if (cur_tail_ptr == old_tail.ptr) {
            free_external_counter(old_tail);
        } else {
            cur_tail_ptr->release_ref();    // tail has been updated by others
        }
    }
};

template<typename T>
std::atomic<int> lock_free_queue<T>::destruct_count = 0;