#include <stdio.h>  
#include <stdlib.h>
#include <pthread.h>  
#include <sys/time.h>
#include <fstream>
#include <iostream>
#include <unistd.h>
#include <string>
#define PAGESIZE (16*1048)
#define MAXLBA 34967792
#define THREADNUM 4
using namespace std;
struct InstructionInfo{
    int DeviceId,LBA,size;
    double Time_record;
    char op;
};
int InstructionNum = 0;
int LockNum = 0;
pthread_mutex_t FetchMutex,FILMutex,FTLMutex,FinishMutex;
pthread_cond_t FetchCond,FILCond,FTLCond,FinishCond;
int FetchNum = 1,FILNum = 1,FTLNum = 1, FinishNum = 1;
string InstructionStr[2000000];
int GlobalId = 0;
// timeval t[64];
void FTL(){
    usleep(500);
}
void FIL(){
    usleep(100000);
}
InstructionInfo FetchInstruction(string InsStr){
    InstructionInfo InsInfo;
    size_t pos1 = InsStr.find(",");
    InsInfo.DeviceId = stoi(InsStr.substr(0,pos1));
    // cout << Instruction[i].DeviceId << endl;
    size_t pos2 = InsStr.find(",",pos1+1);
    InsInfo.LBA = stoi(InsStr.substr(pos1+1,pos2-(pos1+1)));
    // maxLBA = max(maxLBA,Instruction[i].LBA);
    // cout << Instruction[i].LBA << endl;
    size_t pos3 = InsStr.find(",",pos2+1);
    InsInfo.size = stoi(InsStr.substr(pos2+1,pos3-(pos2+1)));
    // cout << Instruction[i].size << endl;
    InsInfo.op = InsStr[pos3+1];
    // cout << Instruction[i].op << endl;
    return InsInfo;
}
void FinishInstruction(){
    usleep(500);
} 
void* TaskProgram( void *rank ){
    long my_rank = (long) rank;
    struct timeval StartTime, FinishTime, ElapsedTime;
    struct timeval LocalElapsedTime;
    LocalElapsedTime.tv_sec = 0;
    LocalElapsedTime.tv_usec = 0;
    gettimeofday(&StartTime, NULL);
    InstructionInfo InsInfo;
    int LocalId = 0,LocalCnt = 0;
    for(int i=my_rank;i<InstructionNum;i+=THREADNUM){
        pthread_mutex_lock(&FetchMutex);
        {
            FetchNum--;
            while(FetchNum < 0){
                pthread_cond_wait(&FetchCond,&FetchMutex);
            }
            InsInfo = FetchInstruction(InstructionStr[i]);
            FetchNum++;        
        }
        pthread_mutex_unlock(&FetchMutex);
        
        pthread_cond_signal(&FetchCond);

        pthread_mutex_lock(&FTLMutex);
        {
            FTLNum--;
            while(FTLNum < 0){
                pthread_cond_wait(&FTLCond,&FTLMutex);
            }
            FTL();
            FTLNum++;
        }
        pthread_mutex_unlock(&FTLMutex);

        pthread_cond_signal(&FTLCond);

        pthread_mutex_lock(&FILMutex);
        {
            FILNum--;
            while(FILNum < 0){
                pthread_cond_wait(&FILCond,&FILMutex);
            }
            FIL();
            FILNum++;
        }
        pthread_mutex_unlock(&FILMutex);   

        pthread_cond_signal(&FILCond);

        pthread_mutex_lock(&FinishMutex);
        {
            FinishNum--;
            while(FinishNum < 0){
                pthread_cond_wait(&FinishCond,&FinishMutex);
            }
            FinishInstruction();
            FinishNum++;
        }
        pthread_mutex_unlock(&FinishMutex);   

        pthread_cond_signal(&FinishCond);

    }
    gettimeofday(&FinishTime, NULL);
    timersub(&FinishTime, &StartTime, &ElapsedTime);
    timeradd(&ElapsedTime,&LocalElapsedTime,&LocalElapsedTime);
    // timeradd(&ElapsedTime,&t[my_rank],&t[my_rank]);
    fprintf(stderr, "Task %ld (thread %ld) executed %d in %d.%03d sec\n",
               my_rank,(long)pthread_self(), LocalCnt, LocalElapsedTime.tv_sec, LocalElapsedTime.tv_usec);

}
void ReadTrace(string File){
    // int DeviceId,LBA,size;
    // double Time_record;
    // char op;
    // int maxLBA = 0;
    // string buffer;
    ifstream infile(File);
    int cnt = 1e2;
    int i = 0;
    cout << File << endl;
    while(!infile.eof() && i < cnt){
        getline(infile,InstructionStr[i]);
        // cout << buffer << endl;
        if(!InstructionStr[i].length())    break;
        i++;
    }
    // cout << "maxLBA: " << maxLBA << endl; 
    InstructionNum = i;
    cout << "finish ReadTrace InstructionNum: " << InstructionNum << endl;
}
void Init(){
    pthread_mutex_init(&FetchMutex,NULL);
    pthread_mutex_init(&FILMutex,NULL);
    pthread_mutex_init(&FTLMutex,NULL);
    pthread_mutex_init(&FinishMutex,NULL);
    pthread_cond_init(&FetchCond,NULL);
    pthread_cond_init(&FTLCond,NULL);
    pthread_cond_init(&FILCond,NULL);
    pthread_cond_init(&FinishCond,NULL);
}   
int main( int argc, char *argv[] )  
{  
    Init();
    ReadTrace(argv[1]);
    long thread;
    pthread_t* thread_handles;
    int thread_count = strtol(argv[2],NULL,10);
    cout << "threadNum: " << thread_count << endl;
    thread_handles = (pthread_t*)malloc(thread_count*sizeof(pthread_t));
    for(thread=0;thread<thread_count;thread++)
        pthread_create(&thread_handles[thread],NULL,TaskProgram,(void*)thread);
    // printf("Hello from the main thread.\n");
    for(thread=0;thread<thread_count;++thread)
        pthread_join(thread_handles[thread],NULL);
    // cout << "finish here" << endl;
    free(thread_handles);
    return 0;
}  