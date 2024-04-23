#ifndef TRANSFORMER_TASKS_HPP
#define TRANSFORMER_TASKS_HPP

#include "utils.hpp"
#include "transformer.hpp"

#define TASK_N_TYPES 2
#define TASK_TYPE_INFERENCE 0
#define TASK_TYPE_TRANSFER 1

struct TransformerContext {
    Transformer* transformer;
    Socket* socket;
    SocketPool* socketPool;
    bool finalize;
    unsigned int currentBlockIndex;
};

class Inference {
private:
    Transformer* transformer;
    TransformerContext context;
    TaskLoop *taskLoop;
public:
    static TaskLoopTask* tasks;
    static int nTasks;

    Inference(unsigned int nThreads, Transformer* transformer, SocketPool* socketPool);
    ~Inference();
    float* infer(int token, int pos);
    void getStats(unsigned long* inferenceTime, unsigned long* transferTime);
    void getDetailedStats(unsigned long* inferenceTime, unsigned long* transferTime, unsigned long* detailedTime);
};

class Worker {
private:
    Transformer* transformer;
    TransformerContext context;
    TaskLoop *taskLoop;
public:
    static TaskLoopTask* tasks;
    static int nTasks;

    Worker(unsigned int nThreads, Transformer* transformer, Socket* socket);
    ~Worker();
    void work();
};

#endif