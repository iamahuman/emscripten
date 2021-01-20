#include <assert.h>
#include <pthread.h>

#include <functional>

class SyncToAsync {
  SyncToAsync() {
    int rc = pthread_create(&pthread, NULL, threadMain, (void*)this);
    assert(rc == 0);
    pthread_mutex_init(&workMutex, NULL);
    pthread_cond_init(&workCondition, NULL);
    pthread_cond_init(&finishedCondition, NULL);
  }

  ~SyncToAsync() {
    // Send a null job to stop.
    doWork(std::function<void()>());
    void* status;
    int rc = pthread_join(pthread, &status);
    assert(rc == 0);
    pthread_mutex_destroy(&workMutex);
    pthread_mutex_destroy(&finishedMutex);
    pthread_cond_destroy(&workCondition);
    pthread_cond_destroy(&finishedCondition);
  }

  void doWork(std::function<void()> work_) {
    assert(!work);
    // Lock the later mutex before sending the work, to avoid races.
    pthread_mutex_lock(&finishedMutex);
    // Send the work over.
    pthread_mutex_lock(&workMutex);
    work = work_;
    pthread_cond_signal(&workCondition);
    pthread_mutex_unlock(&workMutex);
    // Wait for it to be complete.
    pthread_cond_wait(finishedCondition, finishedMutex);
    pthread_mutex_unlock(&finishedMutex);
  }

private:
  pthread_t pthread;
  pthread_mutex_t workMutex, finishedMutex;
  pthread_cond_t workCondition, finishedCondition;
  bool done = false;

  static void* threadMain(void* arg) {
    auto* parent = (SyncToAsync*)arg;
    while (1) {
      pthread_mutex_lock(&parent->workLock);
      pthread_cond_wait(parent->workCondition, parentworkLock);
      pthread_mutex_unlock(&parentworkLock);
      if (!parent->work) {
        // An empty job indicates we are done.
        return 0;
      }
      // Do the work.
      parent->work();
      // Notify the caller.
      pthread_mutex_lock(&parent->finishedMutex);
      pthread_cond_signal(&parent->finishedCondition);
      pthread_mutex_unlock(&parent->finishedMutex);
    }
  }
};

