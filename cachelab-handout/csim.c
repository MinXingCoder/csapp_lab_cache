#include "cachelab.h"
#include <getopt.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <limits.h>

typedef struct {
    bool is_valid;
    unsigned long long tag;
    int timestamp;
} CacheLine;

typedef struct {
    CacheLine* cache_line;
} CacheSet;

typedef struct {
    CacheSet* cache_set;
} Cache;

typedef struct {
    int s;
    int e;
    int b;
    char path[256];
    bool v;
} Metadata;

typedef struct {
    int hits;
    int misses;
    int evictions;
} Result;

bool ParseOptions(int argc, char* argv[], Metadata* metadata);
void HandleCache(Metadata* metadata, Result* result);
void CallocCache(Cache* cache, int S, int E);
void DeleteCache(Cache* cache, int S);
void AddressSplit(char op, unsigned long long address, Cache* cache, Metadata* metadata, Result* result);
void LRUCacheLine(int set, unsigned long long address_tag, int num, Cache* cache, Metadata* metadata, Result* result);

int main(int argc, char* argv[])
{
    Metadata metadata;
    memset(&metadata, 0, sizeof(Metadata));
    if(ParseOptions(argc, argv, &metadata)) {
        Result result;
        memset(&result, 0, sizeof(Result));
        HandleCache(&metadata, &result);
        printSummary(result.hits, result.misses, result.evictions);
    }
    return 0;
}


bool ParseOptions(int argc, char* argv[], Metadata* metadata) {
    const char* optstring = "hvs:E:b:t:";
    int ret;
    while((ret = getopt(argc, argv, optstring)) != -1) {
        switch (ret) {
        case 'h': {
            printf("Usage: ./csim-ref [-hv] -s <num> -E <num> -b <num> -t <file>\n");
            printf("Options:\n");
            printf("  -h         Print this help message.\n");
            printf("  -v         Optional verbose flag.\n");
            printf("  -s <num>   Number of set index bits.\n");
            printf("  -E <num>   Number of lines per set.\n");
            printf("  -b <num>   Number of block offset bits.\n");
            printf("  -t <file>  Trace file.\n");
            printf("\n");
            printf("Examples:\n");
            printf("  linux>  ./csim-ref -s 4 -E 1 -b 4 -t traces/yi.trace\n");
            printf("  linux>  ./csim-ref -v -s 8 -E 2 -b 4 -t traces/yi.trace\n");
            return false;
        }
        case 's': {
            metadata->s = atoi(optarg);
            break;;
        }
        case 'E': {
            metadata->e = atoi(optarg);
            break;
        }
        case 'b': {
            metadata->b = atoi(optarg);
            break;
        }
        case 't': {
            memcpy(metadata->path, optarg, strlen(optarg));
            break;
        }
        case 'v': {
            metadata->v = true;
            break;
        }
        default: 
            printf("unknown option: %s\n", optarg);
            exit(-1);
        }
    }

    return true;
}

void HandleCache(Metadata* metadata, Result* result) {
    int S = (1 << metadata->s), E = metadata->e;
    Cache cache;
    CallocCache(&cache, S, E);

    FILE* fp = fopen(metadata->path, "r");
    if(fp == NULL) {
        printf("%s is not exits!!!\n", metadata->path);
        exit(-1);
    }
    char line[1024];
    while(fgets(line, 1024, fp) != NULL) {
        if(line[0] == 'I') continue;
        char op;
        unsigned long long address;
        int data;
        if(sscanf(line, " %c %llx,%d", &op, &address, &data) == -1) {
            printf("%s format is error!!!\n", line);
            exit(-1);
        }

        if(metadata->v) printf("%c %llx,%d ", op, address, data);

        AddressSplit(op, address, &cache, metadata, result);
    }

    fclose(fp);
    DeleteCache(&cache, S);
}

void CallocCache(Cache* cache, int S, int E) {
    cache->cache_set = (CacheSet*)calloc(S, sizeof(CacheSet));
        for(int i = 0; i < S; ++i)
            cache->cache_set[i].cache_line = (CacheLine*)calloc(E, sizeof(CacheLine));
}

void DeleteCache(Cache* cache, int S) {
    for(int i = 0; i < S; ++i)
        free(cache->cache_set[i].cache_line);
    free(cache->cache_set);
}

void AddressSplit(char op, unsigned long long address, Cache* cache, Metadata* metadata, Result* result) {
    unsigned long long address_tag = (address >> (metadata->s + metadata->b));
    int address_set = (address >> metadata->b) & (~(-1 << metadata->s)); 

    switch (op)
    {
    case 'L':
    case 'S':
        LRUCacheLine(address_set, address_tag, 1, cache, metadata, result);
        break;
    case 'M':
        LRUCacheLine(address_set, address_tag, 2, cache, metadata, result);
        break;
    default:
        break;
    }
}

void LRUCacheLine(int set, unsigned long long address_tag, int num, Cache* cache, Metadata* metadata, Result* result) {
    int index = -1, max_timestamp = 0, invalid_index = -1, min_index = 0, min_timestamp = INT_MAX;
    for(int i = 0; i < metadata->e; ++i) {
        if(cache->cache_set[set].cache_line[i].is_valid) {
            if(cache->cache_set[set].cache_line[i].tag == address_tag) {
                index = i;
            }
            if(cache->cache_set[set].cache_line[i].timestamp > max_timestamp) {
                max_timestamp = cache->cache_set[set].cache_line[i].timestamp;
            }

            if(cache->cache_set[set].cache_line[i].timestamp < min_timestamp) {
                min_timestamp = cache->cache_set[set].cache_line[i].timestamp;
                min_index = i;
            }
        } else {
            invalid_index = i;
        }
    }

    if(index == -1) {
        if(metadata->v) printf("miss");

        result->misses++;
        if(invalid_index != -1) {
            cache->cache_set[set].cache_line[invalid_index].is_valid = true;
            cache->cache_set[set].cache_line[invalid_index].tag = address_tag;
            cache->cache_set[set].cache_line[invalid_index].timestamp = max_timestamp + 1;
            if(num == 2) {
                result->hits++;
                if(metadata->v) printf(" hit\n");
            } else {
                if(metadata->v) printf("\n");
            }
        } else {
            cache->cache_set[set].cache_line[min_index].is_valid = true;
            cache->cache_set[set].cache_line[min_index].tag = address_tag;
            cache->cache_set[set].cache_line[min_index].timestamp = max_timestamp + 1;
            if(num == 1) {
                result->evictions++;
                if(metadata->v) printf(" eviction\n");
            } else {
                result->evictions++;
                result->hits++;
                if(metadata->v) printf(" eviction hit\n");
            }
        }
    } else {
        cache->cache_set[set].cache_line[index].timestamp = max_timestamp + 1;
        if(metadata->v) printf("hit");
        result->hits++;
        if(num == 2) {
            result->hits++;
            if(metadata->v) printf(" hit\n");
        } else 
            if(metadata->v) printf("\n");
    }
}