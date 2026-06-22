#include "NXProcess.h"
#include "System/Console/Trace.h"
#include <pthread.h>
#include <semaphore.h>

void *_NXStartThread(void *p) {
    SysThread *play = (SysThread *)p;
    play->startExecution();
    return NULL;
}

bool NXProcessFactory::BeginThread(SysThread &thread) {
    pthread_t pthread;
    pthread_create(&pthread, 0, _NXStartThread, &thread);
    return true;
}

SysSemaphore *NXProcessFactory::CreateNewSemaphore(int initialcount, int maxcount) {
    return new NXSysSemaphore(initialcount, maxcount);
}

NXSysSemaphore::NXSysSemaphore(int initialcount, int maxcount) {
    sem_init(&sem_, 0, initialcount);
}

NXSysSemaphore::~NXSysSemaphore() {
    sem_destroy(&sem_);
}

SysSemaphoreResult NXSysSemaphore::Wait() {
    sem_wait(&sem_);
    return SSR_NO_ERROR;
}

SysSemaphoreResult NXSysSemaphore::TryWait() {
    return SSR_INVALID;
}

SysSemaphoreResult NXSysSemaphore::WaitTimeout(unsigned long timeout) {
    return SSR_INVALID;
}

SysSemaphoreResult NXSysSemaphore::Post() {
    sem_post(&sem_);
    return SSR_NO_ERROR;
}
