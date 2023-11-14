#include <stdio.h>  
#include <stdlib.h>
#include <pthread.h>  
#include <sys/time.h>
#include <fstream>
#include <iostream>
#include <unistd.h>
#include <string>
#include <queue>
// 16kB
#define PAGE_SIZE (16*1024)
#define MAX_LBA 34975984
#define THREAD_NUM 4
#define CHANNEL_NUM 4
#define CHANNEL_SIZE (MAX_LBA/CHANNEL_NUM)
// #define CHANNEL_EXECUTE_TIME 150000
// #define PIPELINE_TIMEVAL 10000
using namespace std;
struct CommandInfo{
    int DeviceId,LBA,size;
    double Time_record;
    char op;
};
queue<CommandInfo> SQ,ChQ[CHANNEL_SIZE];
int CommandNum = 0;
int FetchFinish = 0,FTLFinish = 0,ChannelFinish = 0;
pthread_mutex_t SQMutex,ChannelMutex[CHANNEL_SIZE],FetchMutex,FILMutex,FTLMutex,FinishMutex,ChannelFinishMutex;
pthread_cond_t FetchCond,FILCond,FTLCond,FinishCond,ChannelCond[CHANNEL_SIZE];
int FetchNum = 1,FILNum = 0,FTLNum = 0, FinishNum = 0;
string CommandStr[2000000];
struct timeval StartTime, FinishTime, ElapsedTime;
// timeval t[64];
void FTL(){
    usleep(5000);
}
void FIL(){
    usleep(150000);
}
void FinishCommand(){
    usleep(10000);
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
    pthread_mutex_lock(&SQMutex);
    while(size > PAGE_SIZE){
        ComInfo.size = PAGE_SIZE;
        SQ.push(ComInfo);
        ComInfo.LBA += PAGE_SIZE;
        size -= PAGE_SIZE;
        ComNum++;
    }
    if(size){
        ComInfo.size = size;
        SQ.push(ComInfo);
        ComNum++;
    }
    pthread_mutex_unlock(&SQMutex);
    // cout << Command[i].size << endl;

    // cout << Command[i].op << endl;
    return ComNum;
}

void* FetchTask(void *rank){
    int i = 0;
    gettimeofday(&StartTime, NULL);
    while(i < CommandNum){
        int ComNum = FetchCommand(CommandStr[i]);
        i++;
        pthread_mutex_lock(&FTLMutex);
        {
            FTLNum += ComNum;
            // cout << "FTLNum: " << FTLNum << endl;
            pthread_cond_signal(&FTLCond);
        }
        pthread_mutex_unlock(&FTLMutex);
    }
    FetchFinish = 1;
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
        pthread_mutex_lock(&SQMutex);
        {
            FTL();
            ComInfo = SQ.front();
            SQ.pop();            
        }
        pthread_mutex_unlock(&SQMutex);
        i++;
        int ChannelId = ComInfo.LBA / CHANNEL_SIZE;
        // cout << ComInfo.LBA << " " << CHANNEL_SIZE << endl;
        // cout << "ChannelId " << ChannelId << endl;
        pthread_mutex_lock(&ChannelMutex[ChannelId]);
        {
            ChQ[ChannelId].push(ComInfo);
            pthread_cond_signal(&ChannelCond[ChannelId]);
        }
        pthread_mutex_unlock(&ChannelMutex[ChannelId]);         
    }
    FTLFinish = 1;
    cout << "FTL finish " << endl;
}
void* FILTask(void * rank){
    long i = (long) rank % CHANNEL_NUM;    
    // cout << "FILTask i " << i << endl;
    int ExecuteFlag = 0;
    int cnt=0;
    while(1){
        // if(breakdown)   break;
        pthread_mutex_lock(&ChannelMutex[i]);
        {
            ExecuteFlag = 0;
            while(ChQ[i].empty() && !FTLFinish){
                pthread_cond_wait(&ChannelCond[i],&ChannelMutex[i]);
            }
            if(!ChQ[i].empty()){
               CommandInfo ComInfo = ChQ[i].front();
                ChQ[i].pop();    
                ExecuteFlag = 1;
            }
        }
        pthread_mutex_unlock(&ChannelMutex[i]);
        // cout << "Rank " << i << endl;
        if(!ExecuteFlag)    break;
        FIL();
        cnt++;
        pthread_mutex_lock(&FinishMutex);
        {
            FinishNum++;
            // cout << "Rank " << i << endl;
            pthread_cond_signal(&FinishCond);
        }
        pthread_mutex_unlock(&FinishMutex);
        if(ChQ[i].empty() && FTLFinish) break;
    }
    pthread_mutex_lock(&ChannelFinishMutex);
    {
        ChannelFinish++;
    }
    pthread_mutex_unlock(&ChannelFinishMutex);
    cout << "FIL channel " << i << " finish " << cnt << endl;
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

        FinishCommand();
        // if(breakdown && FinishNum <= 0) break;
        // i++;
        if(ChannelFinish == CHANNEL_NUM && FinishNum <= 0) break;
    }

    cout << "Finish finish " << endl;
    for(int i=0;i<CHANNEL_NUM;++i){
        // cout << "i " << i << endl;
        pthread_mutex_lock(&ChannelMutex[i]);
        pthread_cond_signal(&ChannelCond[i]);
        pthread_mutex_unlock(&ChannelMutex[i]);
    }
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
    pthread_cond_init(&FetchCond,NULL);
    pthread_cond_init(&FTLCond,NULL);
    pthread_cond_init(&FILCond,NULL);
    pthread_cond_init(&FinishCond,NULL);
    for(int i=0;i<CHANNEL_SIZE;++i){
        pthread_mutex_init(&ChannelMutex[i],NULL);
        pthread_cond_init(&ChannelCond[i],NULL);
    }
}   
int main( int argc, char *argv[] )  
{  
    Init();
    ReadTrace(argv[1]);
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
    free(thread_handles);
    return 0;
}