#include <stdio.h>  
#include <stdlib.h>
#include <pthread.h>  
#include <sys/time.h>
#include <fstream>
#include <iostream>
#include <unistd.h>
#include <string>
#define PAGE_SIZE (16*1024)
#define MAX_LBA 34989344
#define PAGES_PER_CACHE 16
#define CHANNEL_NUM 4
#define CACHE_RANGE (PAGE_SIZE*PAGES_PER_CACHE)
#define CACHE_NUM ((MAX_LBA/CACHE_RANGE)+1)
#define CHANNEL_SIZE (MAX_LBA/CHANNEL_NUM)
using namespace std;
struct CommandInfo{
    int DeviceId,LBA,size,ComId;
    double Time_record;
    char op;
};
struct CacheLineInfo{
    int ComId,PageId,isDirty;
};
int CommandNum = 0;
CacheLineInfo Cache[CACHE_NUM];
pthread_mutex_t RWmutex[CACHE_NUM];
pthread_mutex_t ChannelMutex[CHANNEL_NUM];
pthread_mutex_t Idmutex;
string CommandStr[2000000];
int GlobalId = 0;
// timeval t[64];
// return 1 2 (io num)
int FTL(int CacheId,int PageId){
    int IsCacheHit = 0;
    if(Cache[CacheId].PageId == PageId) IsCacheHit = 1;
    else  Cache[CacheId].PageId = PageId;
    usleep(5000);
    return IsCacheHit;
}
void FlashCommandexe(){
    usleep(150000);
}
CommandInfo FetchCommand(string ComStr){
    CommandInfo ComInfo;
    size_t pos1 = ComStr.find(",");
    ComInfo.DeviceId = stoi(ComStr.substr(0,pos1));
    // cout << Command[i].DeviceId << endl;
    size_t pos2 = ComStr.find(",",pos1+1);
    ComInfo.LBA = stoi(ComStr.substr(pos1+1,pos2-(pos1+1)));
    // maxLBA = max(maxLBA,Command[i].LBA);
    // cout << Command[i].LBA << endl;
    size_t pos3 = ComStr.find(",",pos2+1);
    ComInfo.size = stoi(ComStr.substr(pos2+1,pos3-(pos2+1)));
    // cout << Command[i].size << endl;
    ComInfo.op = ComStr[pos3+1];
    // cout << Command[i].op << endl;
    return ComInfo;
}
void FinishCommand(){
    usleep(10000);
} 
void* TaskProgram( void *rank ){
    long my_rank = (long) rank;
    int CacheHit = 0;
    struct timeval StartTime, FinishTime, ElapsedTime;
    struct timeval LocalElapsedTime;
    LocalElapsedTime.tv_sec = 0;
    LocalElapsedTime.tv_usec = 0;
    gettimeofday(&StartTime, NULL);
    CommandInfo ComInfo;
    int LocalId = 0,LocalCnt = 0;
    int SegSum = 0;
    while(LocalId < CommandNum){
        // cout << "LocalCnt " << endl;
        pthread_mutex_lock(&Idmutex);
        {
            LocalId = GlobalId;
            GlobalId++;
            LocalCnt++;
        }
        pthread_mutex_unlock(&Idmutex);
        if(LocalId >= CommandNum)   break;
        ComInfo = FetchCommand(CommandStr[LocalId]);
        // cout << "LBA " << ComInfo.LBA << " MutexId " << MutexId << endl; 
        //syn
        int SegCnt = ComInfo.size / PAGE_SIZE;
        SegSum += SegCnt;
        if(ComInfo.size % PAGE_SIZE) SegCnt++;
        for(int i=0;i<SegCnt;++i){
            int PageId = ComInfo.LBA / PAGE_SIZE;
            int CacheId = PageId % CACHE_NUM;
            // cout << "PageId " << PageId << " CacheId " << CacheId << endl;
            if(pthread_mutex_trylock(&RWmutex[CacheId]) == 0)
            {
                int IsCacheHit = FTL(CacheId,PageId);
                if(!IsCacheHit){
                    if(Cache[CacheId].isDirty){
                        int ChannelId = (PAGE_SIZE * Cache[CacheId].PageId) / CHANNEL_SIZE;
                        pthread_mutex_lock(&ChannelMutex[ChannelId]);
                        FlashCommandexe();
                        pthread_mutex_unlock(&ChannelMutex[ChannelId]);                           
                    }
                    if(ComInfo.op == 'R'){
                        int ChannelId = ComInfo.LBA / CHANNEL_SIZE;
                        pthread_mutex_lock(&ChannelMutex[ChannelId]);
                        FlashCommandexe();
                        pthread_mutex_unlock(&ChannelMutex[ChannelId]);
                        Cache[CacheId].isDirty = 0;                          
                    }
                }
                else{
                    // cout << "hit" << endl;
                    CacheHit++;
                }
                if(ComInfo.op == 'W'){
                    Cache[CacheId].isDirty = 1;
                }
            pthread_mutex_unlock(&RWmutex[CacheId]);
            }
            else{
                int ChannelId = (PAGE_SIZE * Cache[CacheId].PageId) / CHANNEL_SIZE;
                pthread_mutex_lock(&ChannelMutex[ChannelId]);
                FlashCommandexe();
                pthread_mutex_unlock(&ChannelMutex[ChannelId]);
            }
            ComInfo.LBA += PAGE_SIZE;
            FinishCommand();  
        }

    }
    double CacheHitRate = 1.0*CacheHit/(LocalCnt+SegSum);
    gettimeofday(&FinishTime, NULL);
    timersub(&FinishTime, &StartTime, &ElapsedTime);
    // cout << "ElapsedTime " << ElapsedTime.tv_sec << "." << ElapsedTime.tv_usec <<endl;
    timeradd(&ElapsedTime,&LocalElapsedTime,&LocalElapsedTime);
    // timeradd(&ElapsedTime,&t[my_rank],&t[my_rank]);
    fprintf(stderr, "Task %ld (thread %ld) executed %d in %d.%03d sec. Hit:%d CacheHitRate: %.3lf\n",
               my_rank,(long)pthread_self(), (LocalCnt+SegSum), LocalElapsedTime.tv_sec, LocalElapsedTime.tv_usec,CacheHit,CacheHitRate);

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
        getline(infile,CommandStr[i]);
        // cout << buffer << endl;
        if(!CommandStr[i].length())    break;
        i++;
    }
    // cout << "maxLBA: " << maxLBA << endl; 
    CommandNum = i;
    cout << "finish ReadTrace CommandNum: " << CommandNum << endl;
}
void Init(){
    pthread_mutex_init(&Idmutex,NULL);
    for(int i=0;i<CACHE_NUM;++i){
        pthread_mutex_init(&RWmutex[i],NULL);
    }
    for(int i=0;i<CHANNEL_NUM;++i){
        pthread_mutex_init(&ChannelMutex[i],NULL);
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
    cout << "CACHE_NUM: " << CACHE_NUM << endl;
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