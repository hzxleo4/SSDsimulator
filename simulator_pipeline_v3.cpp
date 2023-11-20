#include <stdio.h>  
#include <stdlib.h>
#include <pthread.h>  
#include <sys/time.h>
#include <fstream>
#include <iostream>
#include <unistd.h>
#include <string>
#include <queue>
#include <vector>
#include <atomic>
// 16kB
#define PAGE_SIZE (16*1024)
#define MAX_LBA 34989344
#define THREAD_NUM 4
#define CHANNEL_NUM 4
#define PAGES_PER_CACHE 16
#define CHANNEL_SIZE (MAX_LBA/CHANNEL_NUM)
#define CACHE_RANGE (PAGE_SIZE*PAGES_PER_CACHE)
#define CACHE_NUM ((MAX_LBA/CACHE_RANGE)+1)
#define TRANS_LATENCY 5000
#define FLASH_LATENCY 150000
// #define CHANNEL_EXECUTE_TIME 150000
// #define PIPELINE_TIMEVAL 10000
using namespace std;
struct CommandInfo{
    char op;
    bool isCacheHit;
    int DeviceId,ComId,LBA,size;
    int ComId_victim,PageId_victim;
    // double Time_record;
};
struct CacheLineInfo{
    int ComId,PageId,isDirty;
};
atomic<int> counter(0);
queue<CommandInfo> CQ,SQ,ChQ[CHANNEL_NUM];
vector<CommandInfo> WaitList;
CacheLineInfo CachePilot[CACHE_NUM],Cache[CACHE_NUM];
int ComCnt = 0;
int CommandNum = 0;
int TotalCacheHit = 0;
int FetchFinish = 0,FTLFinish = 0,ChannelFinish = 0;
pthread_mutex_t CQMutex,WaitMutex,SQMutex,ChannelMutex[CHANNEL_NUM],FetchMutex,FILMutex,FTLMutex,FinishMutex,ChannelFinishMutex;
pthread_cond_t FetchCond,FILCond,FTLCond,FinishCond,ChannelCond[CHANNEL_NUM];
int FetchNum = 1,FILNum = 0,FTLNum = 0, FinishNum = 0;
string CommandStr[2000000];
struct timeval StartTime, FinishTime, ElapsedTime;
// timeval t[64];
void FTL(){
    usleep(TRANS_LATENCY);
}
void FlashCommandexe(){
    usleep(FLASH_LATENCY);
}
void FinishCommand(){
    usleep(10000);
} 
void CheckCachePilot(CommandInfo& ComInfo){
    int PageId = ComInfo.LBA / PAGE_SIZE;
    int CacheId = PageId % CACHE_NUM;
    // cout << "CacheId " << CacheId << endl;
    ComInfo.ComId_victim = CachePilot[CacheId].ComId;
    // cout << "PageId " << PageId << " CachePageId " << CachePilot[CacheId].PageId << endl;
    //cache miss
    if(PageId != CachePilot[CacheId].PageId){
        // cout << "cache miss " << endl;
        ComInfo.isCacheHit = 0;
        if(CachePilot[CacheId].isDirty)
            ComInfo.PageId_victim = CachePilot[CacheId].PageId;
        else
            ComInfo.PageId_victim = -1;
        CachePilot[CacheId].isDirty = 0;
        CachePilot[CacheId].PageId = PageId;
    }
    //cache hit
    else{
        // cout << "cache hit " << endl;
        TotalCacheHit++;
        ComInfo.isCacheHit = 1;
    }
    CachePilot[CacheId].ComId = ComInfo.ComId;
    // cout << "Comid " << ComInfo.ComId << " " << "CachePilot ComId " << ComInfo.ComId_victim << endl;
    if(ComInfo.op == 'W')   CachePilot[CacheId].isDirty = 1;
}
int FetchCommand(string ComStr){
    int ComNum = 0;
    CommandInfo ComInfo;
    size_t pos1 = ComStr.find(",");
    ComInfo.DeviceId = stoi(ComStr.substr(0,pos1));
    // cout << Command[i].DeviceId << endl;
    size_t pos2 = ComStr.find(",",pos1+1);
    ComInfo.LBA = stoi(ComStr.substr(pos1+1,pos2-(pos1+1)));
    // maxLBA = max(maxLBA,Command[i].LBA);
    // cout << Command[i].LBA << endl;
    size_t pos3 = ComStr.find(",",pos2+1);
    int size = stoi(ComStr.substr(pos2+1,pos3-(pos2+1)));
    ComInfo.op = ComStr[pos3+1];
    // cout << "got token" << endl;
    pthread_mutex_lock(&SQMutex);
    {
        // cout << "into SQMutex " << endl;
        while(size > PAGE_SIZE){
            ComInfo.size = PAGE_SIZE;
            // ComCnt++;
            ComInfo.ComId = ++ComCnt;
            CheckCachePilot(ComInfo);
            // cout << "finish CheckCachepilot" << endl;
            SQ.push(ComInfo);
            // cout << "finish push" << endl;
            ComInfo.LBA += PAGE_SIZE;
            size -= PAGE_SIZE;
            ComNum++;
        }
        if(size >= 0){
            ComInfo.size = size;
            ComInfo.ComId = ++ComCnt;
            CheckCachePilot(ComInfo);
            // cout << "finish CheckCachepilot" << endl;
            SQ.push(ComInfo);
            // cout << "finish push" << endl;
            ComNum++;
        }        
    }

    pthread_mutex_unlock(&SQMutex);
    // cout << Command[i].size << endl;

    // cout << Command[i].op << endl;
    return ComNum;
}

void* FetchTask(void *rank){
    int i = 0;
    gettimeofday(&StartTime, NULL);
    int TotalCommandNum = 0;
    while(i < CommandNum){
        // cout << "i " << i << endl;
        int ComNum = FetchCommand(CommandStr[i]);
        // cout << ComNum << endl;
        i++;
        pthread_mutex_lock(&FTLMutex);
        {
            // cout << "FTLNum " << FTLNum << endl;
            FTLNum += ComNum;
            TotalCommandNum += ComNum;
            // cout << "FTLNum: " << FTLNum << endl;
            pthread_cond_signal(&FTLCond);
        }
        pthread_mutex_unlock(&FTLMutex);
        // cout << "here " << endl;
    }
    FetchFinish = 1;
    cout << "Total CacheHit: " << TotalCacheHit << " CacheHitRate: " << 1.0*TotalCacheHit/TotalCommandNum<< endl;
    cout << "Fetch finish " << endl;
}
void* FTLTask(void *rank){
    int i = 0;
    CommandInfo ComInfo;
    while(i < CommandNum){
        if(FetchFinish && FTLNum <= 0)   break;
        pthread_mutex_lock(&FTLMutex);
        {
            while(FTLNum <= 0){
                pthread_cond_wait(&FTLCond,&FTLMutex);
            }
            FTLNum--;
        }
        pthread_mutex_unlock(&FTLMutex);
        // cout << "FTLNum " << FTLNum << endl;
        pthread_mutex_lock(&SQMutex);
        {
            FTL();
            ComInfo = SQ.front();
            SQ.pop();            
        }
        pthread_mutex_unlock(&SQMutex);
        // i++;
        //cache hit 
        // cout << "finish pop " << endl;
        if(ComInfo.isCacheHit){
            // cout << "hit " << endl;
            pthread_mutex_lock(&WaitMutex);
            WaitList.push_back(ComInfo);
            pthread_mutex_unlock(&WaitMutex);
        }
        //cache miss
        else{
            int ChannelId;
            //dirty page to evict
            if(ComInfo.PageId_victim != -1){
                ChannelId = ComInfo.PageId_victim * PAGE_SIZE / CHANNEL_SIZE;
                // cout << ChannelId << endl;
                pthread_mutex_lock(&ChannelMutex[ChannelId]);
                {
                    ChQ[ChannelId].push(ComInfo);
                    pthread_cond_signal(&ChannelCond[ChannelId]);
                }
                pthread_mutex_unlock(&ChannelMutex[ChannelId]);   
            }
            ComInfo.PageId_victim = -1;
            //read data from flash
            if(ComInfo.op == 'R'){
                // cout << ComInfo.LBA << endl;
                ChannelId = ComInfo.LBA / CHANNEL_SIZE;
                // cout << "ComInfo.LBA" << ComInfo.LBA << endl;
                // cout << "channelId " << ChannelId << endl;
                pthread_mutex_lock(&ChannelMutex[ChannelId]);
                {
                    ChQ[ChannelId].push(ComInfo);
                    pthread_cond_signal(&ChannelCond[ChannelId]);
                }
                pthread_mutex_unlock(&ChannelMutex[ChannelId]);                
            }
            //data can be write to cache directly
            else{
                pthread_mutex_lock(&WaitMutex);
                WaitList.push_back(ComInfo);
                pthread_mutex_unlock(&WaitMutex);
            }
            // WaitList.push(ComInfo);
        }
        // cout << ComInfo.LBA << " " << CHANNEL_SIZE << endl;
        // cout << "ChannelId " << ChannelId << endl;
    }

    FTLFinish = 1;
    cout << "FTL finish " << endl;
    //call idle FIL thread to quit
    for(int i=0;i<CHANNEL_NUM;++i){
        // cout << "i " << i << endl;
        pthread_mutex_lock(&ChannelMutex[i]);
        pthread_cond_signal(&ChannelCond[i]);
        pthread_mutex_unlock(&ChannelMutex[i]);
    }
    cout << "call Channel finish " << endl;
}
void CheckWaitList(){
    pthread_mutex_lock(&WaitMutex);
    {
        // cout << WaitList.size() << endl;
        int EraseCom[WaitList.size()];
        EraseCom[0] = 0;
        for(int i=0;i<WaitList.size();++i){
            CommandInfo ComInfo = WaitList[i];
            int PageId = ComInfo.LBA / PAGE_SIZE;
            int CacheId = PageId % CACHE_NUM;
            // cout << "ComId " << ComInfo.ComId << " ComId_victim " << ComInfo.ComId_victim << " " << "Cache ComId " << Cache[CacheId].ComId << endl;
            if(ComInfo.ComId_victim == Cache[CacheId].ComId){
                EraseCom[0]++;
                EraseCom[EraseCom[0]] = i;
            }
        }
        for(int i=1;i<=EraseCom[0];++i){
            pthread_mutex_lock(&CQMutex);
            {
                CQ.push(WaitList[EraseCom[i]-i+1]);
                // FinishNum++;
            }
            pthread_mutex_unlock(&CQMutex);
            WaitList.erase(WaitList.begin()+EraseCom[i]-i+1);
        }
        if(EraseCom[0] != 0){
            // cout << "erase here " << endl;
            pthread_mutex_lock(&FinishMutex);
            {
                FinishNum += EraseCom[0];
                pthread_cond_signal(&FinishCond);
            }
            pthread_mutex_unlock(&FinishMutex);            
        }
        // cout << "finish CheckWaitList " << endl;
    }
    pthread_mutex_unlock(&WaitMutex);

}
void* FILTask(void * rank){
    long i = (long) rank % CHANNEL_NUM;    
    // cout << "FILTask i " << i << endl;
    int ExecuteFlag = 0;
    int cnt=0;
    while(1){
        // if(breakdown)   break;
        CommandInfo ComInfo;
        pthread_mutex_lock(&ChannelMutex[i]);
        {
            ExecuteFlag = 0;
            while(ChQ[i].empty() && !FTLFinish){
                pthread_cond_wait(&ChannelCond[i],&ChannelMutex[i]);
            }
            if(!ChQ[i].empty()){
                ComInfo = ChQ[i].front();
                ChQ[i].pop();    
                ExecuteFlag = 1;
            }
        }
        pthread_mutex_unlock(&ChannelMutex[i]);
        // cout << "Rank " << i << endl;
        if(!ExecuteFlag)    break;
        FlashCommandexe();//？FlashCommandexe
        if(ComInfo.PageId_victim == -1){
            // cout << "WaitList add " << endl;
            pthread_mutex_lock(&WaitMutex);
            WaitList.push_back(ComInfo);
            pthread_mutex_unlock(&WaitMutex);
        }
        cnt++;
        //Cache data has changed, need to check wait list
        CheckWaitList();
        if(ChQ[i].empty() && FTLFinish) break;
    }
    pthread_mutex_lock(&ChannelFinishMutex);
    {
        ChannelFinish++;
    }
    pthread_mutex_unlock(&ChannelFinishMutex);
    cout << "FIL channel " << i << " finish " << endl;
}
void WriteCache(CommandInfo ComInfo){
    int PageId = ComInfo.LBA / PAGE_SIZE;
    int CacheId = PageId % CACHE_NUM;
    // cout<< "CacheId " << CacheId << endl;
    // cout << "swap " << ComInfo.ComId << endl;
    // cout << "Cache " << Cache[CacheId].ComId;
    // bool isSwap = counter.compare_exchange_strong(Cache[CacheId].ComId, ComInfo.ComId);
    // while(!isSwap)
    //     isSwap = counter.compare_exchange_strong(Cache[CacheId].ComId, ComInfo.ComId);
    Cache[CacheId].ComId = ComInfo.ComId;
    // cout << " " << Cache[CacheId].ComId << endl;
    // if(!isSwap)
        // cout << "isSwap " << isSwap << endl;
}
void* FinishTask(void * rank){
    // int i = 0;
    while(1){
        // if(breakdown)   break;
        pthread_mutex_lock(&FinishMutex);
        {
            while(FinishNum <= 0){
                pthread_cond_wait(&FinishCond,&FinishMutex);
            }
            // cout << "FinishNum " << FinishNum << endl;
            FinishNum--;
        }
        pthread_mutex_unlock(&FinishMutex);
        CommandInfo ComInfo;
        pthread_mutex_lock(&CQMutex);
        {
            // cout << "CQ.size " << CQ.size() << endl;
            ComInfo = CQ.front();
            CQ.pop();
            // cout << "ComId " << ComInfo.ComId << endl;
        }
        pthread_mutex_unlock(&CQMutex);
        WriteCache(ComInfo);
        // cout << "finish Write " << endl;
        FinishCommand();
        CheckWaitList();
        // cout << "finishCheckWaitList" << endl;
        // if(breakdown && FinishNum <= 0) break;
        // i++;
        if(ChannelFinish == CHANNEL_NUM && FinishNum <= 0) break;
    }

    cout << "Finish finish " << endl;
    gettimeofday(&FinishTime, NULL);
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
    pthread_mutex_init(&FetchMutex,NULL);
    pthread_mutex_init(&FILMutex,NULL);
    pthread_mutex_init(&FTLMutex,NULL);
    pthread_mutex_init(&FinishMutex,NULL);
    pthread_mutex_init(&ChannelFinishMutex,NULL);
    pthread_mutex_init(&SQMutex,NULL);
    pthread_mutex_init(&CQMutex,NULL);
    pthread_mutex_init(&WaitMutex,NULL);
    pthread_cond_init(&FetchCond,NULL);
    pthread_cond_init(&FTLCond,NULL);
    pthread_cond_init(&FILCond,NULL);
    pthread_cond_init(&FinishCond,NULL);
    for(int i=0;i<CHANNEL_NUM;++i){
        pthread_mutex_init(&ChannelMutex[i],NULL);
        pthread_cond_init(&ChannelCond[i],NULL);
    }
    for(int i=0;i<CACHE_NUM;++i){
        CachePilot[i].ComId = -1;
        Cache[i].ComId = -1;
    }
}   
int main( int argc, char *argv[] )  
{  
    Init();
    ReadTrace(argv[1]);
    cout << "finish ReadTrace" << endl;
    cout << "CACHE_NUM " << CACHE_NUM << endl;
    long thread;
    pthread_t* thread_handles;
    int thread_count = 8;
    cout << "threadNum: " << thread_count << endl;
    thread_handles = (pthread_t*)malloc(thread_count*sizeof(pthread_t));
    pthread_create(&thread_handles[0],NULL,FetchTask,NULL);
    pthread_create(&thread_handles[1],NULL,FTLTask,NULL);
    // pthread_create(&thread_handles[2],NULL,FILTask,NULL);
    pthread_create(&thread_handles[2],NULL,FinishTask,NULL);
    for(thread=4;thread<thread_count;thread++)
        pthread_create(&thread_handles[thread],NULL,FILTask,(void*)thread);
    // printf("Hello from the main thread.\n");
    for(thread=0;thread<thread_count;++thread)
        pthread_join(thread_handles[thread],NULL);
    // cout << "finish here" << endl;
    timersub(&FinishTime, &StartTime, &ElapsedTime);
    fprintf(stderr, "Task executed %d instructions in %d.%03d sec\n",
                CommandNum, ElapsedTime.tv_sec,ElapsedTime.tv_usec);
    // if(thread_handles == NULL)  cout << "11111 " << endl;
    free(thread_handles);
    // cout << "finish free " << endl;
    return 0;
}