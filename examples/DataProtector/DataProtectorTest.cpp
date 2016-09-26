#include "DataGuardian.h"
#include "DataProtector.h"

#include <iostream>
#include <thread>
#include <vector>
#include <time.h>

#define T 10
#define maxN 64

using namespace std;

struct DataToBeProtected {
  DataToBeProtected(int i) : nr(i), isValid(true) {
  }
  ~DataToBeProtected() {
    isValid = false;
  }
  int nr;
  bool isValid;
};

DataToBeProtected const* unprotected = nullptr;
DataGuardian<DataToBeProtected, maxN> guardian;

atomic<DataToBeProtected*> pointerToData(nullptr);

DataProtector<64> protector;

mutex mut;

uint64_t total = 0;

atomic<uint64_t> nullptrsSeen;
atomic<uint64_t> alarmsSeen;

shared_ptr<DataToBeProtected> global_shared_ptr;
thread_local shared_ptr<DataToBeProtected> thread_local_shared_ptr;

void reader_guardian (int id) {
  uint64_t count = 0;
  time_t start = time(nullptr);
  while (time(nullptr) < start + T) {
    for (int i = 0; i < 1000; i++) {
      count++;
      DataToBeProtected const* p = guardian.lease(id);
      if (p == nullptr) {
        nullptrsSeen++;
      }
      else {
        if (! p->isValid) {
          alarmsSeen++;
        }
      }
      guardian.unlease(id);
    }
  }
  lock_guard<mutex> locker(mut);
  total += count;
}

void reader_protector (int id) {
  uint64_t count = 0;
  time_t start = time(nullptr);
  while (time(nullptr) < start + T) {
    for (int i = 0; i < 1000; i++) {
      count++;
      auto unuser(protector.use());
      DataToBeProtected const* p = pointerToData;
      if (p == nullptr) {
        nullptrsSeen++;
      }
      else {
        if (! p->isValid) {
          alarmsSeen++;
        }
      }
    }
  }
  lock_guard<mutex> locker(mut);
  total += count;
}

void reader_unprotected (int) {
  uint64_t count = 0;
  time_t start = time(nullptr);
  while (time(nullptr) < start + T) {
    for (int i = 0; i < 1000; i++) {
      count++;
      DataToBeProtected const* p = unprotected;
      if (p == nullptr) {
        nullptrsSeen++;
      }
      else {
        if (! p->isValid) {
          alarmsSeen++;
        }
      }
    }
  }
  lock_guard<mutex> locker(mut);
  total += count;
}

void reader_mutex (int) {
  uint64_t count = 0;
  time_t start = time(nullptr);
  while (time(nullptr) < start + T) {
    for (int i = 0; i < 1000; i++) {
      count++;
      lock_guard<mutex> locker(mut);
      DataToBeProtected const* p = unprotected;
      if (p == nullptr) {
        nullptrsSeen++;
      }
      else {
        if (! p->isValid) {
          alarmsSeen++;
        }
      }
    }
  }
  lock_guard<mutex> locker(mut);
  total += count;
}

void reader_shared_ptr(int) {
  uint64_t count = 0;
  time_t start = time(nullptr);
  while (time(nullptr) < start + T) {
    for (int i = 0; i < 1000; i++) {
      count++;
      atomic_thread_fence(memory_order_consume);
      if (thread_local_shared_ptr != global_shared_ptr) {
        thread_local_shared_ptr = atomic_load(&global_shared_ptr);
      }
      if (thread_local_shared_ptr == nullptr) {
        nullptrsSeen++;
      }
      else {
        if (! thread_local_shared_ptr->isValid) {
          alarmsSeen++;
        }
      }
    }
  }
  lock_guard<mutex> locker(mut);
  total += count;
}


void writer_guardian () {
  DataToBeProtected* p;
  for (int i = 0; i < T+2; i++) {
    p = new DataToBeProtected(i);
    guardian.exchange(p);
    usleep(1000000);
  }
  guardian.exchange(nullptr);
}

void writer_protector () {
  DataToBeProtected* p;
  DataToBeProtected* q;
  for (int i = 0; i < T+2; i++) {
    p = new DataToBeProtected(i);
    q = pointerToData;
    pointerToData = p;
    protector.scan();
    delete q;
    usleep(1000000);
  }
  q = pointerToData;
  pointerToData = nullptr;
  protector.scan();
  delete q;
}

void writer_unprotected () {
  DataToBeProtected* p;
  for (int i = 0; i < T+2; i++) {
    DataToBeProtected const* q = unprotected;
    p = new DataToBeProtected(i);
    unprotected = p;
    usleep(1000000);
    delete q;
  }
  delete unprotected;
  unprotected = nullptr;
}

void writer_mutex () {
  DataToBeProtected* p;
  for (int i = 0; i < T+2; i++) {
    p = new DataToBeProtected(i);
    {
      lock_guard<mutex> locker(mut);
      delete unprotected;
      unprotected = p;
    }
    usleep(1000000);
  }
  delete unprotected;
  unprotected = nullptr;
}

void writer_shared_ptr () {
  for (int i = 0; i < T+2; i++) {
    atomic_store(&global_shared_ptr, make_shared<DataToBeProtected>(i));
    usleep(1000000);
  }
}

char const* modes[] = {"guardian", "unprotected", "std::mutex", "std::shared_ptr",
                       "protector"};

int main (int argc, char* argv[]) {
  std::vector<double> totals;
  std::vector<double> perthread;
  std::vector<int> nrThreads;

  for (int mode = 0; mode < 5; mode++) {
    for (int j = 1; j < argc; j++) {
      nullptrsSeen = 0;
      alarmsSeen = 0;
      total = 0;
      int N = atoi(argv[j]);
      cout << "Mode: " << modes[mode] << endl;
      cout << "Nr of threads: " << N << endl;
      vector<thread> readerThreads;
      readerThreads.reserve(N);
      thread* writerThread;

      switch (mode) {
        case 0: writerThread = new thread(writer_guardian); break;
        case 1: writerThread = new thread(writer_unprotected); break;
        case 2: writerThread = new thread(writer_mutex); break;
        case 3: writerThread = new thread(writer_shared_ptr); break;
        case 4: writerThread = new thread(writer_protector); break;
      }
      
      usleep(500000);
      for (int i = 0; i < N; i++) {
        switch (mode) {
          case 0: readerThreads.emplace_back(reader_guardian, i); break;
          case 1: readerThreads.emplace_back(reader_unprotected, i); break;
          case 2: readerThreads.emplace_back(reader_mutex, i); break;
          case 3: readerThreads.emplace_back(reader_shared_ptr, i); break;
          case 4: readerThreads.emplace_back(reader_protector, i); break;
        }
      }
      writerThread->join();
      for (int i = 0; i < N; i++) {
        readerThreads[i].join();
      }
      delete writerThread;
      writerThread = nullptr;
      cout << "Total: " << total/1000000.0/T << "M/s, per thread: "
                        << total/1000000.0/N/T << "M/(thread*s)" << endl;
      cout << "nullptr values seen: " << nullptrsSeen
           << ", alarms seen: " << alarmsSeen << endl << endl;
      totals.push_back(total/1000000.0/T);
      perthread.push_back(total/1000000.0/N/T);
      nrThreads.push_back(N);
    }
  }
  for (size_t i = 0; i < totals.size(); i++) {
    std::cout << i << "\t" << nrThreads[i] << "\t" << totals[i] << "\t"
              << perthread[i] << std::endl;
  }
  return 0;
}

