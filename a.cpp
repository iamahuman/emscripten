#include <assert.h>
#include <emscripten.h>
#include <pthread.h>

#include <functional>
#include <iostream>

class SyncToAsync {
public:
  using Callback = std::function<void()>;

  SyncToAsync() {
std::cout << "a1\n";
    int rc = pthread_create(&pthread, NULL, threadMain, (void*)this);
    assert(rc == 0);
    pthread_mutex_init(&workMutex, NULL);
    pthread_cond_init(&workCondition, NULL);
    pthread_cond_init(&finishedCondition, NULL);
std::cout << "a2\n";
  }

  ~SyncToAsync() {
std::cout << "a3\n";
    done = true;
    // Wake up the child with an empty task.
    doWork([](Callback func){
      func();
    });
    void* status;
    int rc = pthread_join(pthread, &status);
    assert(rc == 0);
    pthread_mutex_destroy(&workMutex);
    pthread_mutex_destroy(&finishedMutex);
    pthread_cond_destroy(&workCondition);
    pthread_cond_destroy(&finishedCondition);
std::cout << "a4\n";
  }

  // Run some work. This is a synchronous call, but the thread can do async work
  // for us. To allow us to know when the async work finishes, the worker is
  // given a function to call at that time.
  void doWork(std::function<void(Callback)> work_) {
std::cout << "a5\n";
    // Busy-wait until the child is ready.
    while (!threadReady) {}
std::cout << "a5.05\n";
    // Lock the later mutex before sending the work, to avoid races.
    pthread_mutex_lock(&finishedMutex);
std::cout << "a5.1\n";
    // Send the work over.
    pthread_mutex_lock(&workMutex);
std::cout << "a5.2\n";
    work = work_;
    pthread_cond_signal(&workCondition);
std::cout << "a5.3\n";
    pthread_mutex_unlock(&workMutex);
std::cout << "a5.4\n";
    // Wait for it to be complete.
    pthread_cond_wait(&finishedCondition, &finishedMutex);
std::cout << "a5.5\n";
    pthread_mutex_unlock(&finishedMutex);
std::cout << "a6\n";
  }

private:
  pthread_t pthread;
  pthread_mutex_t workMutex, finishedMutex;
  pthread_cond_t workCondition, finishedCondition;
  std::function<void(Callback)> work;
  std::atomic<bool> threadReady;
  bool done = false;

  static void* threadMain(void* arg) {
std::cout << "b0\n";
    emscripten_async_call(threadIter, arg, 0);
    return 0;
  }

  static void threadIter(void* arg) {
std::cout << "b1\n";
    auto* parent = (SyncToAsync*)arg;
std::cout << "b1.1\n";
    if (!pthread_mutex_trylock(&parent->workMutex)) {
std::cout << "b1.2\n";
      pthread_mutex_unlock(&parent->workMutex);
    }
std::cout << "b2\n";
    pthread_mutex_lock(&parent->workMutex);
    parent->threadReady = true;
std::cout << "b2.1\n";
    pthread_cond_wait(&parent->workCondition, &parent->workMutex);
std::cout << "b2.2\n";
    auto work = parent->work;
    auto done = parent->done;
    parent->threadReady = false;
    pthread_mutex_unlock(&parent->workMutex);
std::cout << "b2.3\n";
    // Do the work.
    work([&]() {
      // We are called, so the work was finished. Notify the caller.
      pthread_mutex_lock(&parent->finishedMutex);
      pthread_cond_signal(&parent->finishedCondition);
      pthread_mutex_unlock(&parent->finishedMutex);
      if (done) {
std::cout << "b2.4\n";
        pthread_exit(0);
      } else {
std::cout << "b3\n";
        // Look for more work. (We do this asynchronously to avoid nesting of
        // the stack, and to keep this function simple without a loop.)
        emscripten_async_call(threadIter, arg, 0);
      }
    });
  }
};

// Testcase

static SyncToAsync::Callback __somethingToCallLater;

void prepareToCallLater(SyncToAsync::Callback callback) {
  __somethingToCallLater = callback;
}

extern "C" EMSCRIPTEN_KEEPALIVE void callWhatWasPrepared() {
  __somethingToCallLater();
}

int main() {
  SyncToAsync helper;
  // Perform an actually synchronous task.
  helper.doWork([](SyncToAsync::Callback func) {
    std::cout << "hello from sync C++\n";
    func();
  });

  // Perform an async task.
  helper.doWork([](SyncToAsync::Callback func) {
    // We can't just pass SyncToAsync::Callback over to JS through the C ABI, so
    // handle the JS->C++ call carefully, by calling into C and then C calling
    // into C++.
    prepareToCallLater(func);
    EM_ASM({
      console.log("hello from sync JS");
      setTimeout(function() {
        console.log("hello from async JS");
        _callWhatWasPrepared();
      }, 1);
    });
  });

  // Perform another synchronous task.
  helper.doWork([](SyncToAsync::Callback func) {
    std::cout << "hello again from sync C++\n";
    func();
  });
}
