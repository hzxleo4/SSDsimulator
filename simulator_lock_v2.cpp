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
using namespace std;
struct InstructionInfo{
    int DeviceId,LBA,size;
    double Time_record;
    char op;
};
int InstructionNum = 0;
int LockNum = 0;
pthread_mutex_t RWmutex[200];
pthread_mutex_t Idmutex;
string InstructionStr[2000000];
int GlobalId = 0;
// timeval t[64];
void FTL(){
    usleep(5000);
}
void FIL(){
    usleep(150000);
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
    while(LocalId < InstructionNum){
        pthread_mutex_lock(&Idmutex);
        {
            LocalId = GlobalId;
            GlobalId++;
            LocalCnt++;
        }
        pthread_mutex_unlock(&Idmutex);
        if(LocalId >= InstructionNum)   break;
        int MutexId;
        InsInfo = FetchInstruction(InstructionStr[LocalId]);
        MutexId = InsInfo.LBA / (16*PAGESIZE);
        // cout << "LBA " << InsInfo.LBA << " MutexId " << MutexId << endl; 
        //syn

        pthread_mutex_lock(&RWmutex[MutexId]);
        {
            FTL();
            FIL();           
        }
        pthread_mutex_unlock(&RWmutex[MutexId]);

        FinishInstruction();
    }

    gettimeofday(&FinishTime, NULL);
    timersub(&FinishTime, &StartTime, &ElapsedTime);
    // cout << "ElapsedTime " << ElapsedTime.tv_sec << "." << ElapsedTime.tv_usec <<endl;
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
    int cnt = 1e4;
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
    pthread_mutex_init(&Idmutex,NULL);
    for(int i=0;i<LockNum;++i){
        pthread_mutex_init(&RWmutex[i],NULL);
    }
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