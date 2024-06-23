#ifndef BARRIER_HPP
#define BARRIER_HPP

#include <condition_variable>
#include <mutex>
#include <stdexcept>
#include <thread>

class Barrier {
 public:
    Barrier(std::size_t num)
        : num_threads(num),
          wait_count(0),
          instance(0),
          mut(),
          cv()
    {
        if (num == 0) {
            throw std::invalid_argument("Barrier thread count cannot be 0");
        }
    }

    Barrier(const Barrier&) = delete;
    Barrier& operator =(const Barrier&) = delete;

    void wait() {
        std::unique_lock<std::mutex> lock(mut);
        std::size_t inst = instance; 

        if (++wait_count == num_threads) { 
            wait_count = 0; 
            instance++; 
            cv.notify_all();
        } else {
            cv.wait(lock, [this, &inst]() { return instance != inst; });
        }
    }
 private:
    std::size_t num_threads; // number of threads using barrier
    std::size_t wait_count; // counter to keep track of waiting threads
    std::size_t instance; // counter to keep track of barrier use count
    std::mutex mut; // mutex used to protect resources
    std::condition_variable cv; // condition variable used to block threads
};

#endif