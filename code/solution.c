#include <stdlib.h>
#include <sys/mman.h>
#include <errno.h>
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <string.h>
#include <signal.h>

#include <linux/futex.h>
#include <sys/syscall.h>
#include <sys/time.h>

#include <sys/ioctl.h>

#include <stdatomic.h>

#include "nyan_cat.c"

// Wrapper for futex
int futex(int* uaddr, int futex_op, int val, const struct timespec* timeout, int* uaddr2, int val3) {
  return syscall(SYS_futex, uaddr, futex_op, val, timeout, uaddr2, val3);
}

#define _GNU_SOURCE

#define MMAP_ADDRESS        (void*) 0xC024CD24
#define MMAP_LENGTH         160000000 // 160 MB
#define MMAP_PROTECTION     PROT_READ | PROT_WRITE
#define MMAP_FLAGS          MAP_PRIVATE | MAP_ANONYMOUS
#define MMAP_FD             -1 // This is ignored because of flags = MAP_ANONYMOUS, but some systems require this to be -1
#define MMAP_OFFSET         0

#define RANDOM_SRC          "/dev/urandom"

#define FILL_THREADS        94

#define WRITE_FILES_SIZE    68000000 // 68 MB
#define IO_BLOCK_SIZE       143
#define FILE_READ_THREADS   37
#define FILE_NAME_PREFIX    "file"
#define FILE_MODE           S_IRWXU | S_IRGRP | S_IROTH

#define FILES_NUM           MMAP_LENGTH/WRITE_FILES_SIZE + (MMAP_LENGTH % WRITE_FILES_SIZE == 0 ? 0 : 1)

void startFileAnalyzingJob(int);
void* fillThreadHandler(void*);
void* fileAnalyzeHandler(void*);
void analyzeFileContent(char*, int, int);
void generateData();
void writeData();
void writeRegionToFile(char*, void*, int);
void drawFileWritingProgress(char*, int, int);
void clearFileWritingProgress();
void freeData();

void fileWriteLock(int);
void fileReadLock(int);
void fileWriteUnlock(int);
void fileReadUnlock(int);

void waitOnFutexValue(int* addr, int value);
void wakeFutexBlocking(int* addr);

void signalHandler(int);

void printAhh();
void printVan();

void* targetRegionPtr;
int randomFd;

struct RWLock {
    int fileCreated;
    int syncLock;
    int writeLock;
    int readers; // To change this value, the syncLock must be acquired
} rwLocks[FILES_NUM] = {0};

int main() {
    signal(SIGINT, signalHandler);
    signal(SIGSEGV, signalHandler);

    printVan();
    startFileAnalyzingJob(FILE_READ_THREADS);
    while (1) {
        generateData();
        writeData();
        freeData();
        // printAhh();
    }
}

void generateData() {
    printf("\n---------- GENERATING \033[1;35m♂️DATA♂️\033[0m ----------\n\n");
    printf("Trying to pass \033[1;35m♂️fingers♂️\033[0m into computer memory's \033[1;35m♂️ass♂️\033[0m\n");
    targetRegionPtr = mmap(MMAP_ADDRESS, MMAP_LENGTH, MMAP_PROTECTION, MMAP_FLAGS, MMAP_FD, MMAP_OFFSET);
    if (targetRegionPtr == MAP_FAILED) {
        printf("\033[1;35mOh shit, I'm sorry\033[0m\n");
        printf("Could not do mmap, errno = %d\n", errno);
        exit(-1);
    } else {
        printf("mmap is done successfully, region created at %p\n", targetRegionPtr);
    }

    printf("Filling up memory's \033[1;35m♂️ass♂️\033[0m with \033[1;35m♂️data♂️\033[0m using %d threads\n", FILL_THREADS);

    randomFd = open(RANDOM_SRC, O_RDONLY);
    if (randomFd < 0) {
        printf("\033[1;35m- Oh shit, I'm sorry\033[0m\n");
        printf("\033[1;35m- Sorry for what?\033[0m\n");
        printf("- Could not open file %s, errno = %d\n", RANDOM_SRC, errno);
        exit(-1);
    }

    pthread_t threads[FILL_THREADS];
    int threadArg[FILL_THREADS];

    for (int i = 0; i < FILL_THREADS; i++) {
        threadArg[i] = i;
        pthread_create(&threads[i], NULL, fillThreadHandler, &threadArg[i]);
    }

    printf("Waiting for threads to \033[1;35m♂️join♂️\033[0m our \033[1;35m♂️leather club♂️\033[0m...\n");
    for (int i = 0; i < FILL_THREADS; i++)
        pthread_join(threads[i], NULL); 

    close(randomFd);
    printf("Memory \033[1;35m♂️fisting♂️\033[0m is done successfully with %d \033[1;35m♂️fingers♂️\033[0m\n", FILL_THREADS);
}

void writeData() {
    printf("\n---------- WRITING \033[1;35m♂️DATA♂️\033[0m ----------\n\n");

    for (int i = 0; i < FILES_NUM; i++) {
        char iString[] = {'0' + i, '\0'};
        char filename[strlen(FILE_NAME_PREFIX) + strlen(iString)];
        strcpy(filename, FILE_NAME_PREFIX);
        strcat(filename, iString);

        int actualSize = WRITE_FILES_SIZE;
        if (i == FILES_NUM - 1) // Is this the last file?
            actualSize = MMAP_LENGTH - WRITE_FILES_SIZE*(FILES_NUM - 1);

        fileWriteLock(i);
        writeRegionToFile(filename, targetRegionPtr + WRITE_FILES_SIZE * i, actualSize);

        rwLocks[i].fileCreated = 1;
        wakeFutexBlocking(&rwLocks[i].fileCreated); // Wake if someone waits for file creation

        fileWriteUnlock(i);
    }
}

void writeRegionToFile(char* name, void* regionStart, int size) {
    int blocksNum = size/IO_BLOCK_SIZE;
    if (IO_BLOCK_SIZE*blocksNum < size)
        blocksNum++;
    
    printf("Writing file '%s' from region at %p, size = %d, will write %d blocks\n", name, regionStart, size, blocksNum);

    int fd = open(name, O_CREAT | O_WRONLY, FILE_MODE);

    if (fd < 0) {
        printf("Could not open file '%s', errno = %d\n", name, errno);
        exit(-1);
    }
    
    for (int i = 0; i < blocksNum; i++) {
        int actualBlockSize = IO_BLOCK_SIZE;
        if (i == blocksNum - 1) // Is this the last block?
            actualBlockSize = size - IO_BLOCK_SIZE*(blocksNum - 1);

        void* writeStartPtr = regionStart + i*IO_BLOCK_SIZE;

        ssize_t wrote = write(fd, writeStartPtr, actualBlockSize);
        drawFileWritingProgress(name, i, blocksNum);
        fflush(stdout);

        if (wrote == -1) {
            printf("\n");
            printf("Could not write to file '%s' from memory at %p, size = %d, errno = %d\n", name, writeStartPtr, actualBlockSize, errno);
            exit(-1);
        }
    }
    // printf("\r\n");

    clearFileWritingProgress();
    close(fd);
    // printf("%c[2K", 27);
    printf("File \033[1;35m♂️filling♂️\033[0m took %d \033[1;35m♂️blocks♂️\033[0m\n\n", blocksNum);
}

void freeData() {
    munmap(targetRegionPtr, MMAP_LENGTH);
}

void startFileAnalyzingJob(int threadNum) {
    printf("Started to analyze output \033[1;35m♂️files♂️\033[0m with %d \033[1;35m♂️dicks♂️\033[0m in background\n", FILE_READ_THREADS);

    pthread_t* threads = (pthread_t*)malloc(threadNum*sizeof(pthread_t));
    int* threadIndices = (int*)malloc(threadNum*sizeof(int)); 
    
    for (int i = 0; i < threadNum; i++) {
        threadIndices[i] = i;
        pthread_create(&threads[i], NULL, fileAnalyzeHandler, &threadIndices[i]);
    }
}

void* fileAnalyzeHandler(void* vargPtr) {
    int index = *(int*)vargPtr;
    while(1) {
        for (int i = 0; i < FILES_NUM; i++) {
            char iString[] = {'0' + i, '\0'};
            char filename[strlen(FILE_NAME_PREFIX) + strlen(iString)];
            strcpy(filename, FILE_NAME_PREFIX);
            strcat(filename, iString);

            analyzeFileContent(filename, i, index);
        }
    }
}

void analyzeFileContent(char* fileName, int fileRwLockStructIdx, int analyzerIdx) {
    fileReadLock(fileRwLockStructIdx);
    int fd = open(fileName, O_RDONLY, FILE_MODE);

    if (fd < 0 && errno == ENOENT) { // File does not exist
        printf("\033[2K\033[2;49;37m[Analyzer %d] File '%s' doesn't exist yet, read skipped\033[0m\n", analyzerIdx, fileName);
        fileReadUnlock(fileRwLockStructIdx);
        waitOnFutexValue(&rwLocks[fileRwLockStructIdx].fileCreated, 0); // Wait for file creation
        return;
    } else if (fd < 0) {
        printf("Error reading file '%s', errno = %d\n", fileName, errno);
        exit(1);
    }

    off_t size = lseek(fd, 0L, SEEK_END);
    lseek(fd, 0, SEEK_SET);
    
    __uint32_t* content = (__uint32_t*) malloc(size);
    ssize_t readBytes = read(fd, content, size);

    close(fd);
    fileReadUnlock(fileRwLockStructIdx);

    __uint32_t min = content[0];
    for (size_t i = 0; i < readBytes/sizeof(__uint32_t); i += 1) {
        if (content[i] < min)
            min = content[i];
    }

    // http://www.climagic.org/mirrors/VT100_Escape_Codes.html
    printf("\033[2K\033[2;49;37m[Analyzer %d] Minimal 4-byte uint of file '%s' is %d\033[0m\n", analyzerIdx, fileName, min);

    free(content);
}

void fileReadLock(int fileIdx) {
    // printf("read lock %d\n", fileIdx);
    struct RWLock* lock = &rwLocks[fileIdx];

    int zero;
    do {
        zero = 0;
        waitOnFutexValue(&lock->syncLock, 1);
    } while (!atomic_compare_exchange_strong(&lock->syncLock, &zero, 1));
    // function sync area start

    lock->readers++;
    if (lock->readers == 1) { // We must acquire the write lock here
        do {
            zero = 0;
            waitOnFutexValue(&lock->writeLock, 1);
        } while (!atomic_compare_exchange_strong(&lock->writeLock, &zero, 1));
    }
    
    // function sync area end
    lock->syncLock = 0;
    wakeFutexBlocking(&lock->syncLock);
    // printf("read locked %d\n", fileIdx);
}

void fileReadUnlock(int fileIdx) {
    // printf("read unlock %d\n", fileIdx);
    struct RWLock* lock = &rwLocks[fileIdx];
    int zero;
    do {
        zero = 0;
        waitOnFutexValue(&lock->syncLock, 1);
    } while (!atomic_compare_exchange_strong(&lock->syncLock, &zero, 1));
    // function sync area start

    lock->readers--;
    if (lock->readers == 0) { // We must acquire the write lock here
        lock->writeLock = 0;
        wakeFutexBlocking(&lock->writeLock);
    }
    
    // function sync area end
    lock->syncLock = 0;
    wakeFutexBlocking(&lock->syncLock);
    // printf("read unlocked %d\n", fileIdx);
}

void fileWriteLock(int fileIdx) {
    struct RWLock* lock = &rwLocks[fileIdx];
    // printf("write lock %d\n", fileIdx);
    int zero;
    do {
        zero = 0;
        waitOnFutexValue(&lock->writeLock, 1);
    } while (!atomic_compare_exchange_strong(&lock->writeLock, &zero, 1));
    // printf("write locked %d\n", fileIdx);
}

void fileWriteUnlock(int fileIdx) {
    // printf("write unlock %d\n", fileIdx);
    struct RWLock* lock = &rwLocks[fileIdx];
    lock->writeLock = 0;
    wakeFutexBlocking(&lock->writeLock);
    // printf("write unlocked %d\n", fileIdx);
}

void waitOnFutexValue(int* addr, int value) {
    while (1) {
        int futexCode = futex(addr, FUTEX_PRIVATE_FLAG | FUTEX_WAIT, value, NULL, NULL, 0);
        if (futexCode == -1) {
            if (errno != EAGAIN) {
                printf("\033[1;35mOh shit, I'm sorry\033[0m\n");
                printf("Error during futex waiting");
                exit(1);
            } else if (*addr != value) {
                // Value doesn't match
                return;
            }
        } else if (futexCode == 0) {
            if (*addr != value) {
                // Wake up!
                return;
            }
        } else {
            abort();
        }
    }
}

void wakeFutexBlocking(int* addr) {
    while (1) {
        int futexCode = futex(addr, FUTEX_WAKE, 1, NULL, NULL, 0);
        if (futexCode == -1) {
            printf("\033[1;35mOh shit, I'm sorry\033[0m\n");
            printf("Error during futex blocking");
            exit(1);
        } else if (futexCode >= 0) { // 0 or more waiters woke up
            return;
        }
    }
}

void* fillThreadHandler(void* vargPtr) {
    int chunkSize = MMAP_LENGTH/FILL_THREADS;
    int threadIndex = *(int*)vargPtr;
    void* ptrStart = (void*)(targetRegionPtr + threadIndex * chunkSize);
    if (*(int*)vargPtr == FILL_THREADS - 1) // Am I filling the last chunk?
        chunkSize += MMAP_LENGTH - MMAP_LENGTH/FILL_THREADS*FILL_THREADS;

    ssize_t result = read(randomFd, ptrStart, chunkSize);

    if (result == -1) {
        printf("Could not fill computer memory's \033[1;35m♂️ass♂️\033[0m at pointer %p, size = %d, errno = %d\n", ptrStart, chunkSize, errno);
        exit(-1);
    }
}

void printAhh() {
    printf("                                            _____                   _____                   _____                   _____          \n");
    printf("                                           /\\    \\                 /\\    \\                 /\\    \\                 /\\    \\         \n");
    printf("                                          /::\\    \\               /::\\____\\               /::\\____\\               /::\\____\\        \n");
    printf("                                         /::::\\    \\             /:::/    /              /:::/    /              /:::/    /        \n");
    printf("     ░░░▓▓▓▓▓▓▒▒▒▒▒▒▓▓░░░░░░░           /::::::\\    \\           /:::/    /              /:::/    /              /:::/    /         \n");
    printf("     ░▓▓▓▓▒░░▓▓▓▒▄▓░▒▄▄▄▓░░░░          /:::/\\:::\\    \\         /:::/    /              /:::/    /              /:::/    /          \n");
    printf("     ▓▓▓▓▓▒░░▒▀▀▀▀▒░▄░▄▒▓▓░░░         /:::/__\\:::\\    \\       /:::/____/              /:::/____/              /:::/____/           \n");
    printf("     ▓▓▓▓▓▒░░▒▒▒▒▒▓▒▀▒▀▒▓▒▓░░        /::::\\   \\:::\\    \\     /::::\\    \\             /::::\\    \\             /::::\\    \\           \n");
    printf("     ▓▓▓▓▓▒▒░░░▒▒▒░░▄▀▀▀▄▓▒▓░       /::::::\\   \\:::\\    \\   /::::::\\    \\   _____   /::::::\\    \\   _____   /::::::\\    \\   _____  \n");
    printf("     ▓▓▓▓▓▓▒▒░░░▒▒▓▀▄▄▄▄▓▒▒▒▓      /:::/\\:::\\   \\:::\\    \\ /:::/\\:::\\    \\ /\\    \\ /:::/\\:::\\    \\ /\\    \\ /:::/\\:::\\    \\ /\\    \\ \n");
    printf("     ░▓█▀▄▒▓▒▒░░░▒▒░░▀▀▀▒▒▒▒░     /:::/  \\:::\\   \\:::\\____/:::/  \\:::\\    /::\\____/:::/  \\:::\\    /::\\____/:::/  \\:::\\    /::\\____\\\n");
    printf("     ░░▓█▒▒▄▒▒▒▒▒▒▒░░▒▒▒▒▒▒▓░     \\::/    \\:::\\  /:::/    \\::/    \\:::\\  /:::/    \\::/    \\:::\\  /:::/    \\::/    \\:::\\  /:::/    /\n");
    printf("     ░░░▓▓▓▓▒▒▒▒▒▒▒▒░░░▒▒▒▓▓░      \\/____/ \\:::\\/:::/    / \\/____/ \\:::\\/:::/    / \\/____/ \\:::\\/:::/    / \\/____/ \\:::\\/:::/    / \n");
    printf("     ░░░░░▓▓▒░░▒▒▒▒▒▒▒▒▒▒▒▓▓░               \\::::::/    /           \\::::::/    /           \\::::::/    /           \\::::::/    /  \n");
    printf("     ░░░░░░▓▒▒░░░░▒▒▒▒▒▒▒▓▓░░                \\::::/    /             \\::::/    /             \\::::/    /             \\::::/    /   \n");
    printf("                                             /:::/    /              /:::/    /              /:::/    /              /:::/    /    \n");
    printf("                                            /:::/    /              /:::/    /              /:::/    /              /:::/    /     \n");
    printf("                                           /:::/    /              /:::/    /              /:::/    /              /:::/    /      \n");
    printf("                                          /:::/    /              /:::/    /              /:::/    /              /:::/    /       \n");
    printf("                                          \\::/    /               \\::/    /               \\::/    /               \\::/    /        \n");
    printf("                                           \\/____/                 \\/____/                 \\/____/                 \\/____/         \n");
}

void printVan() {
    printf("\n");
    printf("..........................-+*=@#*:.............................................................\n");
    printf("........................*@@@@@@@@@@#-..........................................................\n");
    printf(".......................#@@@@@@@@@@@@@=.........._____________________________________..........\n");
    printf("......................:##@@@@@@@@@@@@@:........|                                     |.........\n");
    printf("......................-#*+++****=#@@@@-........|      Fisting is                     |.........\n");
    printf(".......................**:::::::+=@@@+.........|            three hundred            |.........\n");
    printf("......................-++:+:+*+++*##*:.........|                        bucks        |.........\n");
    printf(".......................::::--::::***-..........|  ___________________________________|.........\n");
    printf(".......................:::+:::::+*:............| /.............................................\n");
    printf("......................+:::+++::+*:.............|/..............................................\n");
    printf(".................-:::::*=:++::+==:.............................................................\n");
    printf("...........-:::::::::::++=##==###=+............................................................\n");
    printf(".........-:---:::+:+:-:::+*+*#@=:*=*=+-........................................................\n");
    printf(".......:::------:---*=*:::::*+*=======*=+......................................................\n");
    printf("......-:-------::----:=+*:++++***=####****-....................................................\n");
    printf("......-:::::-::::------+++*+:::+*#=#===****-...................................................\n");
    printf("......:::::::++:--------:*:=+-+=*#=+**=****+...................................................\n");
    printf(".....:::::::+*:-----------*==+=#=*+++***=**=-..................................................\n");
    printf("....-:::::--*+::----------::*:-:+:+++**=#**=+..................................................\n");
    printf("....::::::::=*++::::::::::=###+::+++**===**=+..................................................\n");
    printf("...-:::::::+@#===***++++++=##=++++**===#***=+..................................................\n");
    printf("...::::::::*=***=+#:=-===+-:=####==##=*=***=+..................................................\n");
    printf("...:::::::-.-:::----:::++-::+###@@@#-.-****=+..................................................\n");
    printf("..::-::::-...:::----------:::++**==:...**+***..................................................\n");
    printf(".-:--::+:....::::-----------::+*===+...**++*=-.................................................\n");
    printf(".-:--:::.....:++:::::::::::::+*====+...+*++*=:.................................................\n");
    printf("\n");
}

void drawFileWritingProgress(char* fileName, int currentProgress, int maxProgress) {
    static int previousFrame = -1;
    int frame = (currentProgress/750) % NYAN_CAT_FRAMES;

    char progressBar[41];
    for (int j = 0; j < 40; j++)
        progressBar[j] = j/40.0f < 1.0f*currentProgress/maxProgress ? '>' : ' ';
    progressBar[40] = '\0';
    printf("\033[2K");
    printf("Writing '%s': [%s] %d/%d blocks", fileName, progressBar, currentProgress, maxProgress);

    if (frame != previousFrame) {
        printf("\n");

        for (int row = 0; row < NYAN_CAT_HEIGHT; row++) {
            int startingColumn = NYAN_CAT_WIDTH - NYAN_CAT_WIDTH*currentProgress/maxProgress;
            for (int column = startingColumn; column < NYAN_CAT_WIDTH; column++) {
                char symbol = nyanCatColorData[frame][row][column];
                printf("\033[%sm%s\033[0m", nyanCatColorMap[symbol], NYAN_CAT_SYMBOL);
            }
            printf("\r\n");
            fflush(stdout);
        }

        for (int i = 0; i < NYAN_CAT_HEIGHT+1; i++)
            printf("\033[A");
    } else {
        printf("\r");
    }
    
    fflush(stdout);
    previousFrame = frame;
}

void clearFileWritingProgress() {
    // Used buffering to prevent concurrent prints
    char buffer[5*(NYAN_CAT_HEIGHT+1) + 3*(NYAN_CAT_HEIGHT+1) + 1] = {0};
    size_t ptr = 0;
    for (int i = 0; i < NYAN_CAT_HEIGHT+1; i++) {
        buffer[ptr++] = '\033';
        buffer[ptr++] = '[';
        buffer[ptr++] = '2';
        buffer[ptr++] = 'K';
        buffer[ptr++] = '\n';
    }
    for (int i = 0; i < NYAN_CAT_HEIGHT+1; i++) {
        buffer[ptr++] = '\033';
        buffer[ptr++] = '[';
        buffer[ptr++] = 'A';
    }
    puts(buffer);
}

void signalHandler(int signum) {
    if (signum == SIGINT) {
        printf("\n");
        exit(signum);
    } else if (signum == SIGSEGV) {
        printf("♂️Fisting♂️ fault (♂️ass♂️ dumped)\n");
        exit(signum);
    }
}