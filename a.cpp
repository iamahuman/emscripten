#include <assert.h>
#include <pthread.h>

#include <functional>
#include <iostream>

class SyncToAsync {
public:
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
    // Send an empty job to wake up the child.
    doWork([](){});
    void* status;
    int rc = pthread_join(pthread, &status);
    assert(rc == 0);
    pthread_mutex_destroy(&workMutex);
    pthread_mutex_destroy(&finishedMutex);
    pthread_cond_destroy(&workCondition);
    pthread_cond_destroy(&finishedCondition);
std::cout << "a4\n";
  }

  void doWork(std::function<void()> work_) {
std::cout << "a5\n";
    // Busy-wait until the child is ready.
    bool ready;
    do {
      pthread_mutex_lock(&workMutex);
      ready = threadReady;
      pthread_mutex_unlock(&workMutex);
    } while (!ready);
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
  std::function<void()> work;
  bool threadReady = false;
  bool done = false;

  static void* threadMain(void* arg) {
std::cout << "b1\n";
    auto* parent = (SyncToAsync*)arg;
    while (1) {
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
      work();
      // Notify the caller.
      pthread_mutex_lock(&parent->finishedMutex);
      pthread_cond_signal(&parent->finishedCondition);
      pthread_mutex_unlock(&parent->finishedMutex);
      if (done) {
std::cout << "b2.4\n";
        return 0;
      }
std::cout << "b3\n";
    }
  }
};

int main() {
  SyncToAsync helper;
  helper.doWork([]() {
    std::cout << "hello, world\n";
  });
  helper.doWork([]() {
    std::cout << "goodbye, world\n";
  });
}

