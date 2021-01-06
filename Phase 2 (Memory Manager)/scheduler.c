#include "linked_list.h"

void init();
void processTerminatedHandler(int signum);

// --------ALgorithms------
void highestPriorityFirst();
void shortestRemainingTimeNext();
void roundRobin(int quantum);
// ------------------------

int initSem(int id);

/*shared memory handlers*/
int initShm(int id);
int getShmValue(int shmid);

Node *readProcessesData();
Node *storeProcessData();
int forkNewProcess(Node *processNode);

void allocateMemoryFor(Node *processNode);

void resumeProcess(Node *processNode);
void stopProcess(Node *processNode);
void removeProcess(Node *processNode);
void updateProcessTime(Node *processNode);

void sortNewProcessesWithPriority(Node *processNode);

int shmid, msqid;
int sem_id_scheduler, sem_id_generator;
process *shm_proc_addr;
Node *head;

int remainingProcesses = 0;
bool processIsComming = true;

Node *runningProcessNode;

int now; // current clock

memoryBlock *memoryBlocks[MEMORY_SIZE + 1];

int main(int argc, char *argv[])
{
    // initialize all
    init();

    printf("scheduler starting...\n");

    head = createLinkedList();

    //readProcessesData();

    int schAlgo = atoi(argv[1]), quantum = atoi(argv[2]);

    switch (schAlgo)
    {
    case HPF:
        highestPriorityFirst();
        break;
    case SRTN:
        shortestRemainingTimeNext();
        break;
    case RR:
        roundRobin(quantum);
        break;
    }

    //TODO implement the scheduler :)
    //upon termination release the clock resources.

    //TODO Start a new process. (Fork it and give it its parameters.)
    //Switch between two processes according to the scheduling algorithm. (Stop the old process and save its state and start/resume another one.)
    //Keep a process control block (PCB) for each process in the system.
    //Delete the data of a process when it gets noties that it nished.
    printf("scheduler terminates...\n");
    destroyClk(true);
}

void init()
{
    /* Creating Semaphores */
    key_t key_id_sem_scheduler = ftok("keyFile", 's');
    key_t key_id_sem_generator = ftok("keyFile", 'g');

    sem_id_scheduler = semget(key_id_sem_scheduler, 1, 0660 | IPC_CREAT);
    sem_id_generator = semget(key_id_sem_generator, 1, 0660 | IPC_CREAT);
    /* Creating Shared Memory Segment */
    shmid = shmget(PROC_SH_KEY, sizeof(process), IPC_CREAT | 0644);
    if ((long)shmid == -1)
    {
        perror("Error in creating shm!");
        exit(-1);
    }
    shm_proc_addr = (process *)shmat(shmid, (void *)0, 0);
    if ((long)shm_proc_addr == -1)
    {
        perror("Error in attaching the shm in process generator!");
        exit(-1);
    }
    msqid = msgget(PROC_MSQ_KEY, IPC_CREAT | 0644);
    if (msqid == -1)
    {
        perror("Error in create msqid");
        exit(-1);
    }

    // initialize clock
    initClk();

    // init signal handler
    signal(SIGUSR1, processTerminatedHandler);

    // init memory
    memoryBlocks[MEMORY_SIZE] = (memoryBlock *)malloc(sizeof(memoryBlock));
    memoryBlocks[MEMORY_SIZE]->start = 0;
    memoryBlocks[MEMORY_SIZE]->end = 1023;
}

int forkNewProcess(Node *processNode)
{
    int processPID = fork();
    if (processPID == 0)
    {
        allocateMemoryFor(processNode);

        int id = processNode->process.id;
        int remainingTime = processNode->PCB.remainingTime;
        // convert args to char*
        char execStr1[MAX_DIGITS + 1];
        char execStr2[MAX_DIGITS + 1];
        snprintf(execStr1, MAX_DIGITS + 1, "%d", id);
        snprintf(execStr2, MAX_DIGITS + 1, "%d", remainingTime);

        execl("./process.out", "process.out", execStr1, execStr2, (char *)0);
        printf("Error in executing process.out\n");
        destroyClk(true);
        exit(1);
    }
    else if (processPID == -1)
    {
        perror("error in forking process");
        destroyClk(true);
        exit(1);
    }

    return processPID;
}

Node *storeProcessData()
{
    process p = *shm_proc_addr;
    printf("process ID=%d Arrived Time = %d\n", p.id, getClk());
    Node *newNode = (Node *)malloc(sizeof(Node));
    // initialize newNode
    newNode->process = p;
    newNode->PCB.executionTime = 0;
    newNode->PCB.processState = WAITING;
    newNode->PCB.remainingTime = p.runningtime;
    newNode->PCB.waitingTime = 0;
    newNode->PCB.PID = -1; // -1 means the process not created
    newNode->PCB.shmid = -1;
    newNode->PCB.semid = -1;
    // insert new process to the linked list
    insert(&head, &newNode);

    remainingProcesses++;

    return newNode;
}

/* read new processes data and return poiter to the first one (if no new process returns NULL) */
Node *readProcessesData()
{
    Node *newNode = NULL;
    struct msgbuf message;
    while (processIsComming)
    {
        down(sem_id_scheduler);
        int recValue = msgrcv(msqid, &message, sizeof(message.mtext), 0, !IPC_NOWAIT);
        if (recValue == -1)
            perror("Error in recieve");

        if (message.mtext == FINISHED)
        {
            processIsComming = false;
            break;
        }

        if (message.mtext == NO_PROCESSES)
            break;

        Node *tmpNode = storeProcessData();

        if (newNode == NULL)
            newNode = tmpNode;

        if (message.mtext == COMPLETE)
            break;

        up(sem_id_generator);
    }
    return newNode;
}

void resumeProcess(Node *processNode)
{
    if (!processNode)
        return;

    if (processNode->PCB.processState == RUNNING)
    {
        up(processNode->PCB.semid);
        down(sem_id_scheduler);
        return;
    }

    // set process state to running
    processNode->PCB.processState = RUNNING;

    // if this is the first time i run the process
    if (processNode->PCB.PID == -1)
    {
        processNode->PCB.PID = forkNewProcess(processNode);
        processNode->PCB.shmid = initShm(processNode->process.id);
        processNode->PCB.semid = initSem(processNode->process.id);
    }
    up(processNode->PCB.semid);
    printf("running process id = %d Time = %d\n", runningProcessNode->process.id, getClk());
    down(sem_id_scheduler);
}

void stopProcess(Node *processNode)
{
    if (!processNode)
        return;
    processNode->PCB.processState = WAITING;
}
void removeProcess(Node *processNode)
{
    Node *deletedProcess = removeNodeWithID(&head, processNode->process.id);
    if (deletedProcess)
    {
        // TODO : output the results
        free(deletedProcess);
        remainingProcesses--;
    }
}

/* Update process remaining/execution/waiting time */
void updateProcessTime(Node *processNode)
{
    if (processNode->PCB.processState == TERMINATED)
        return;

    int remainingTime = getShmValue(processNode->PCB.shmid);
    int dif = processNode->PCB.remainingTime - remainingTime;

    processNode->PCB.remainingTime = remainingTime;
    processNode->PCB.executionTime += dif;

    int arrivalTime = processNode->process.arrivaltime;
    int executionTime = processNode->PCB.executionTime;

    processNode->PCB.waitingTime = getClk() - arrivalTime - executionTime;

    if (remainingTime <= 0)
        processNode->PCB.processState = TERMINATED;
}

void highestPriorityFirst()
{
    runningProcessNode = NULL;
    bool processIsRunning = 0;
    while (remainingProcesses || processIsComming)
    {
        now = getClk();
        Node *newNode = readProcessesData();

        sortNewProcessesWithPriority(newNode);

        if (remainingProcesses && processIsRunning == 0)
        {
            if (runningProcessNode == NULL)
                runningProcessNode = head;

            processIsRunning = 1;
        }

        resumeProcess(runningProcessNode);

        //sleep(1);
        while (now == getClk())
            ;

        if (remainingProcesses)
        {
            updateProcessTime(runningProcessNode);
            /*if the process terminated give the turn to the next node*/
            if (runningProcessNode->PCB.processState == TERMINATED)
            {
                removeProcess(runningProcessNode);
                runningProcessNode = NULL;
                processIsRunning = 0;
            }
        }
        up(sem_id_generator);
    }
}
void shortestRemainingTimeNext() {}
void roundRobin(int quantum)
{
    runningProcessNode = NULL;
    Node *lastRunningProcessNode = NULL;
    int currentQuantum = 0;
    while (remainingProcesses || processIsComming)
    {
        now = getClk();
        readProcessesData();

        if (remainingProcesses && currentQuantum <= 0)
        {
            lastRunningProcessNode = runningProcessNode;
            // if runningProcess is at the end of the linked list run the first process
            if (!runningProcessNode || !runningProcessNode->next)
                runningProcessNode = head;
            else
                runningProcessNode = runningProcessNode->next;

            if (runningProcessNode->PCB.processState == WAITING)
            {
                stopProcess(lastRunningProcessNode);
            }

            currentQuantum = quantum;
        }

        resumeProcess(runningProcessNode);

        //sleep(1);
        while (now == getClk())
            ;

        if (remainingProcesses)
        {
            currentQuantum -= (getClk() - now);

            updateProcessTime(runningProcessNode);

            if (runningProcessNode && runningProcessNode->PCB.processState == TERMINATED)
            {
                removeProcess(runningProcessNode);
                currentQuantum = 0;
            }
        }
        up(sem_id_generator);
    }
}

int initSem(int id)
{
    int semKey = ftok("keyFile", id);
    int semid = semget(semKey, 1, 0660 | IPC_CREAT);
    union Semun semun;
    semun.val = 0;
    if (semctl(semid, 0, SETVAL, semun) == -1)
    {
        perror("Error in semctl");
        exit(-1);
    }
    return semid;
}

/*initlizer remainging time as a shared memory varible*/
int initShm(int id)
{
    /*creating shared memory variable*/
    int shmKey = ftok("keyFile", id);
    int shmid = shmget(shmKey, 4, IPC_CREAT | 0666);
    if ((long)shmid == -1)
    {
        perror("Error in creating shm!");
        exit(-1);
    }

    return shmid;
}

/*get shared memory current value*/
int getShmValue(int shmid)
{
    //attach shared memory
    int *shmaddr = (int *)shmat(shmid, (void *)0, 0);

    if ((long)shmaddr == -1)
    {
        printf("Error in attaching the shm in process with id: %d\n", shmid);
        perror("");
        exit(-1);
    }
    // deatach shared memory
    int value = *shmaddr;
    shmdt(shmaddr);
    return value;
}

void processTerminatedHandler(int signum)
{
    int stat_loc, pid;
    pid = wait(&stat_loc);
    int id = stat_loc >> 8;
    Node *terminatedProcess = findNodeWithID(head, id);

    if (terminatedProcess)
    {
        terminatedProcess->PCB.processState = TERMINATED;
        terminatedProcess->PCB.remainingTime = 0;
        terminatedProcess->PCB.executionTime = terminatedProcess->process.runningtime;
        removeProcess(terminatedProcess);
    }

    printf("process ID=%d terminated  Time = %d\n", id, getClk());
}

/*insert new nodes in a sorted way according to there pirorities*/
void sortNewProcessesWithPriority(Node *processNode)
{
    if (processNode == NULL)
        return;
    //disjointing the new nodes form the linked list
    if (head == processNode)
    {
        head = NULL;
    }
    else
    {
        processNode->prev->next = NULL;
        processNode->prev = NULL;
    }
    //joining them in a sorted way
    while (processNode)
    {
        //printf("id = %d, p = %d\n", processNode->process.id, processNode->process.priority);
        Node *nextNode = processNode->next;
        processNode->next = NULL;
        insertionSortWithPriority(&head, &processNode);
        processNode = nextNode;
    }
}

// Calculates log2 of number.
int Log2(int n)
{
    int res = 0;
    n--;
    while (n)
    {
        res++;
        n >>= 1;
    }
    return res;
}

/* allocate memory for a process */
void allocateMemoryFor(Node *processNode)
{
    int mem = Log2(processNode->process.memsize);
    printf("%d\n", mem);
    if (memoryBlocks[mem])
    {
        allocateMemory(mem);
        return;
    }

    int memIdx;
    for (memIdx = mem + 1; memIdx <= MEMORY_SIZE; memIdx++)
    {
        if (memoryBlocks[memIdx])
            break;
    }

    if (memIdx > MEMORY_SIZE)
    {
        perror("You Don't have enough memory!!\n");
        exit(1);
    }

    while (memIdx != mem)
    {
        splitMemory(memIdx);
        memIdx--;
    }

    allocateMemory(mem);
}

void allocateMemory(int mem)
{
    printf("allocated memory from %d to %d\n", memoryBlocks[mem]->start, memoryBlocks[mem]->end);
    memoryBlock *deletedMemoryBlock = memoryBlocks[mem];
    memoryBlocks[mem] = deletedMemoryBlock->next;
    free(deletedMemoryBlock);
}

void splitMemory(int mem)
{
}