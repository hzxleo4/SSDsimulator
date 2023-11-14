#include <stdio.h>  
#include <stdlib.h>
#include <pthread.h>  
#include <sys/time.h>
#include <fstream>
#include <iostream>
#include <unistd.h>
#include <string>
using namespace std;
struct Instruction{
    int DeviceId,LBA,size;
    double Time_record;
    char op;
}Instruction[10000005];
int InstructionNum = 0;
pthread_mutex_t RWmutex=PTHREAD_MUTEX_INITIALIZER;
// timeval t[64];
int gcd(int x, int y){
    if(x % y == 0)  return y;
    else            return gcd(y,x%y);
}
void FTL(){
    usleep(500);
}
void FIL(){
    usleep(100000);
}
void FetchInstruction(){
    usleep(50);
}
void ExecuteInstruction(int size, char op){
    usleep(size/1024*1000);
}
void FinishInstruction(){
    usleep(50);
}
void* TaskProgram( void *rank ){
    long my_rank = (long) rank;
    struct timeval StartTime, FinishTime, ElapsedTime;

    gettimeofday(&StartTime, NULL);

    for(int i=0;i<InstructionNum;++i){
        FetchInstruction();
        //syn
        pthread_mutex_lock(&RWmutex);
        FTL();
        FIL();
        pthread_mutex_unlock(&RWmutex);
        //ExecuteInstruction(Instruction[i].size,Instruction[i].op);
        FinishInstruction();
    }

    gettimeofday(&FinishTime, NULL);

    timersub(&FinishTime, &StartTime, &ElapsedTime);
    // timeradd(&ElapsedTime,&t[my_rank],&t[my_rank]);
    fprintf(stderr, "Task %ld (thread %ld) took %d.%03d sec\n",
               my_rank,(long)pthread_self(), ElapsedTime.tv_sec, ElapsedTime.tv_usec);

}
void ReadTrace(string File){
    // int DeviceId,LBA,size;
    // double Time_record;
    // char op;
    string buffer;
    ifstream infile(File);
    int cnt = 1e4;
    int i = 0;
    cout << File << endl;
    int LBA_gcd = -1;
    int total_size = 0;
    while(!infile.eof() && i < cnt ){
        getline(infile,buffer);
        // cout << buffer << endl;
        size_t pos1 = buffer.find(",");
        Instruction[i].DeviceId = stoi(buffer.substr(0,pos1));
        // cout << Instruction[i].DeviceId << endl;
        size_t pos2 = buffer.find(",",pos1+1);
        Instruction[i].LBA = stoi(buffer.substr(pos1+1,pos2-(pos1+1)));
        // cout << Instruction[i].LBA / 16 << endl;
        if(LBA_gcd == -1)   LBA_gcd = Instruction[i].LBA;
        else                LBA_gcd = gcd(Instruction[i].LBA,LBA_gcd);

        int CacheId = (Instruction[i].LBA/16) % 134;
        cout << CacheId << endl;
        // cout << Instruction[i].LBA << endl;
        size_t pos3 = buffer.find(",",pos2+1);
        Instruction[i].size = stoi(buffer.substr(pos2+1,pos3-(pos2+1)));
        total_size += Instruction[i].size;
        // cout << Instruction[i].size << endl;
        Instruction[i].op = buffer[pos3+1];
        // cout << Instruction[i].op << endl;
        i++;
    }
    InstructionNum = i;
    cout << "LBA_gcd " << LBA_gcd << endl;
    cout << "finish ReadTrace InstructionNum: " << InstructionNum << endl;
    cout << "total_size: " << total_size << endl;

}
int main( int argc, char *argv[] )  
{  
    ReadTrace(argv[1]);
    long thread;
    pthread_t* thread_handles;
    int thread_count = strtol(argv[2],NULL,10);
    cout << "threadNum: " << thread_count << endl;
    thread_handles = (pthread_t*)malloc(thread_count*sizeof(pthread_t));
    for(thread=0;thread<thread_count;thread++)
        pthread_create(&thread_handles[thread],NULL,TaskProgram,(void*)thread);
    printf("Hello from the main thread.\n");
    for(thread=0;thread<thread_count;++thread)
        pthread_join(thread_handles[thread],NULL);
    cout << "finish here" << endl;
    free(thread_handles);
    return 0;
}  