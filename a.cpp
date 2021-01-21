#include <assert.h>
#include <emscripten.h>
#include <pthread.h>

#include <functional>
#include <iostream>

class SyncToAsync {
public:
  using Callback = std::function<void()>;

  SyncToAsync() : childLock(mutex) {
std::cout << "a1\n";
    // The child lock is associated with the mutex, which takes the lock, and
    // we free it here. Only the child will lock/unlock it from now on.
    childLock.unlock();
std::cout << "a1.5\n";
    int rc = pthread_create(&pthread, NULL, threadMain, (void*)this);
    assert(rc == 0);
std::cout << "a2\n";
  }

  ~SyncToAsync() {
std::cout << "a3\n";
    quit = true;
    // Wake up the child with an empty task.
    doWork([](Callback func){
      func();
    });
    void* status;
    int rc = pthread_join(pthread, &status);
    assert(rc == 0);
std::cout << "a4\n";
  }

  // Run some work. This is a synchronous call, but the thread can do async work
  // for us. To allow us to know when the async work finishes, the worker is
  // given a function to call at that time.
  void doWork(std::function<void(Callback)> work_) {
std::cout << "a5\n";
    // Send the work over.
    {
      std::lock_guard<std::mutex> lock(mutex);
      work = work_;
      finishedWork = false;
      readyToWork = true;
    }
    condition.notify_one();
std::cout << "a5.3\n";
    // Wait for it to be complete.
    std::unique_lock<std::mutex> lock(mutex);
std::cout << "a5.4\n";
    condition.wait(lock, [&]() {
      return finishedWork;
    });
std::cout << "a6\n";
  }

private:
  pthread_t pthread;
  std::mutex mutex;
  std::condition_variable condition;
  std::function<void(Callback)> work;
  bool readyToWork = false;
  bool finishedWork;
  bool quit = false;

  // The child will be asynchronous, and therefore we cannot rely on RAII to
  // unlock for us, we must do it manually.
  std::unique_lock<std::mutex> childLock;

  static void* threadMain(void* arg) {
std::cout << "b0\n";
    // Take the lock that the child will have all through it's lifetime.
    auto* parent = (SyncToAsync*)arg;
    parent->childLock.lock();
    emscripten_async_call(threadIter, arg, 0);
    return 0;
  }

  static void threadIter(void* arg) {
    auto* parent = (SyncToAsync*)arg;
std::cout << "b1, " << arg << " owns? " <<  parent->childLock.owns_lock() << "\n";
    // Wait until we get something to do.
std::cout << "b1.5\n";
    parent->condition.wait(parent->childLock, [&]() {
      return parent->readyToWork;
    });
std::cout << "b2\n";
    auto work = parent->work;
    parent->readyToWork = false;
std::cout << "b2.3\n";
    // Do the work.
    work([parent, arg]() {
std::cout << "b2.4\n";
      // We are called, so the work was finished. Notify the caller.
      parent->finishedWork = true;
      parent->childLock.unlock();
std::cout << "b2.5\n";
      parent->condition.notify_one();
std::cout << "b2.6\n";
      if (parent->quit) {
std::cout << "b2.7\n";
        pthread_exit(0);
      } else {
std::cout << "b3\n";
        parent->childLock.lock();
std::cout << "b4, " << arg << " owns? " << parent->childLock.owns_lock() << "\n";
        // Look for more work. (We do this asynchronously to avoid nesting of
        // the stack, and to keep this function simple without a loop.)
        emscripten_async_call(threadIter, arg, 0);
      }
    });
  }
};

// Testcase

int main() {
  SyncToAsync helper;

  std::cout << "Perform a synchronous task.\n";

  helper.doWork([](SyncToAsync::Callback func) {
    std::cout << "  Hello from sync C++\n";
    func();
  });

  std::cout << "Perform an async task.\n";

  // We need to be very careful about the lifetime of |func| below, and the
  // callback we construct from it - things whose lifetime is that of the lambda
  // will not live long enough for the async callback. We ourselves run
  // synchronously, so our stack is definitely alive while the thread works.
  SyncToAsync::Callback keepFuncAlive;
  helper.doWork([&keepFuncAlive](SyncToAsync::Callback resume) {
    std::cout << "  Hello from sync C++ before the async\n";
    keepFuncAlive = [resume]() {
      std::cout << "  Hello from async C++\n";
      resume();
    };
    // Create a simple function pointer to pass to emscripten_async_call.
    struct Caller {
      static void call(void* arg) {
        auto* funcAddr = (SyncToAsync::Callback*)arg;
        (*funcAddr)();
      }
    };
    emscripten_async_call(Caller::call, &keepFuncAlive, 1000);
  });

  std::cout << "Perform another synchronous task.\n";
  helper.doWork([](SyncToAsync::Callback func) {
    std::cout << "  Hello again from sync C++\n";
    func();
  });
}
