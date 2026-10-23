#include "kv.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define KEY_LEN 16
#define VALUE_LEN 32
#define VALUE_POOL 256
#define MAX_RUNS 100
#define MAX_COUNT 100000000

typedef struct {
    char text[KEY_LEN + 1];
} key_text;

typedef enum { INSERT, GET_HIT, GET_MISS, MIXED, WORKLOAD_COUNT } workload;

static const char *const workload_names[WORKLOAD_COUNT] = {"insert", "get_hit", "get_miss",
                                                           "mixed"};

typedef struct {
    char key[KEY_LEN + 1];
    bool put;
} request;

typedef struct {
    workload kind;
    size_t keys;
    size_t ops;
    size_t runs;
    double max_load;
    uint64_t seed;
} config;

typedef struct {
    double throughput;
    uint64_t p50;
    uint64_t p99;
    uint64_t p999;
    uint64_t max;
    kv_stats stats;
} run_result;

static uint64_t rng_state;
static volatile uint64_t sink;
static unsigned char values[VALUE_POOL][VALUE_LEN];

static uint64_t next_random(void) {
    uint64_t z = (rng_state += UINT64_C(0x9e3779b97f4a7c15));
    z = (z ^ (z >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    z = (z ^ (z >> 27)) * UINT64_C(0x94d049bb133111eb);
    return z ^ (z >> 31);
}

static uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * UINT64_C(1000000000) + (uint64_t)ts.tv_nsec;
}

static void fail(const char *message) {
    fprintf(stderr, "bench: %s\n", message);
    exit(1);
}

/* malloc plus memset, so every page is touched before any timing starts. */
static void *allocate(size_t count, size_t size) {
    if (count > SIZE_MAX / size) {
        fail("too much memory requested");
    }
    void *memory = malloc(count * size);
    if (memory == NULL) {
        fail("out of memory");
    }
    memset(memory, 0, count * size);
    return memory;
}

static int compare_u64(const void *a, const void *b) {
    uint64_t x = *(const uint64_t *)a;
    uint64_t y = *(const uint64_t *)b;
    return (x > y) - (x < y);
}

static int compare_double(const void *a, const void *b) {
    double x = *(const double *)a;
    double y = *(const double *)b;
    return (x > y) - (x < y);
}

static uint64_t percentile(const uint64_t *sorted, size_t n, uint64_t per_million) {
    uint64_t rank = ((uint64_t)n * per_million + 999999) / 1000000;
    return sorted[rank > 0 ? rank - 1 : 0];
}

static double median(double *data, size_t n) {
    qsort(data, n, sizeof *data, compare_double);
    return n % 2 == 1 ? data[n / 2] : (data[n / 2 - 1] + data[n / 2]) / 2;
}

static void make_key(char *out, const char *prefix, uint64_t index) {
    static const char digits[] = "0123456789abcdef";
    uint64_t id = (index * UINT64_C(0x9e3779b97f4a7c15)) & ((UINT64_C(1) << 48) - 1);
    memcpy(out, prefix, 4);
    for (int i = KEY_LEN - 1; i >= 4; i--) {
        out[i] = digits[id & 0xf];
        id >>= 4;
    }
    out[KEY_LEN] = '\0';
}

static uint64_t timer_overhead(void) {
    size_t pairs = 1000000;
    uint64_t *gaps = allocate(pairs, sizeof *gaps);
    for (size_t i = 0; i < pairs; i++) {
        uint64_t start = now_ns();
        gaps[i] = now_ns() - start;
    }
    qsort(gaps, pairs, sizeof *gaps, compare_u64);
    uint64_t result = gaps[pairs / 2];
    free(gaps);
    return result;
}

static kv_store *create_store(const config *cfg) {
    kv_options opts = {.max_load_factor = cfg->max_load};
    kv_store *store = kv_create(&opts);
    if (store == NULL) {
        fail("kv_create failed");
    }
    return store;
}

static bool run_request(kv_store *store, const request *r, size_t i, uint64_t *checksum) {
    if (r->put) {
        return kv_put(store, r->key, values[i % VALUE_POOL], VALUE_LEN) == KV_OK;
    }
    const void *value = NULL;
    size_t len = 0;
    if (kv_get(store, r->key, &value, &len) != KV_OK) {
        return false;
    }
    *checksum += *(const unsigned char *)value;
    return true;
}

static uint64_t throughput_pass(kv_store *store, const request *requests, size_t ops, size_t *ok) {
    uint64_t checksum = 0;
    size_t count = 0;
    uint64_t start = now_ns();
    for (size_t i = 0; i < ops; i++) {
        count += run_request(store, &requests[i], i, &checksum);
    }
    uint64_t elapsed = now_ns() - start;
    sink += checksum;
    *ok = count;
    return elapsed;
}

static void latency_pass(kv_store *store, const request *requests, size_t ops, uint64_t *samples,
                         size_t *ok) {
    uint64_t checksum = 0;
    size_t count = 0;
    for (size_t i = 0; i < ops; i++) {
        uint64_t start = now_ns();
        count += run_request(store, &requests[i], i, &checksum);
        samples[i] = now_ns() - start;
    }
    sink += checksum;
    *ok = count;
}

/* Builds one table from empty. With samples, times each insert; without, times the whole build. */
static uint64_t insert_build(const config *cfg, const key_text *keys, uint64_t *samples,
                             kv_stats *stats) {
    kv_store *store = create_store(cfg);
    size_t count = 0;
    uint64_t elapsed = 0;
    if (samples == NULL) {
        uint64_t start = now_ns();
        for (size_t i = 0; i < cfg->keys; i++) {
            count += kv_put(store, keys[i].text, values[i % VALUE_POOL], VALUE_LEN) == KV_OK;
        }
        elapsed = now_ns() - start;
    } else {
        for (size_t i = 0; i < cfg->keys; i++) {
            uint64_t start = now_ns();
            count += kv_put(store, keys[i].text, values[i % VALUE_POOL], VALUE_LEN) == KV_OK;
            samples[i] = now_ns() - start;
        }
    }
    if (count != cfg->keys || kv_size(store) != cfg->keys) {
        fail("insert lost keys");
    }
    if (stats != NULL) {
        kv_get_stats(store, stats);
    }
    kv_destroy(store);
    return elapsed;
}

static void run_once(const config *cfg, const key_text *keys, const request *requests,
                     uint64_t *samples, run_result *result) {
    uint64_t elapsed = 0;
    if (cfg->kind == INSERT) {
        size_t builds = cfg->ops / cfg->keys;
        for (size_t b = 0; b < builds; b++) {
            elapsed += insert_build(cfg, keys, NULL, NULL);
        }
        for (size_t b = 0; b < builds; b++) {
            insert_build(cfg, keys, samples + b * cfg->keys, &result->stats);
        }
    } else {
        kv_store *store = create_store(cfg);
        for (size_t i = 0; i < cfg->keys; i++) {
            if (kv_put(store, keys[i].text, values[i % VALUE_POOL], VALUE_LEN) != KV_OK) {
                fail("kv_put failed while building the table");
            }
        }
        size_t expected = cfg->kind == GET_MISS ? 0 : cfg->ops;
        size_t ok_throughput = 0;
        size_t ok_latency = 0;
        elapsed = throughput_pass(store, requests, cfg->ops, &ok_throughput);
        latency_pass(store, requests, cfg->ops, samples, &ok_latency);
        kv_get_stats(store, &result->stats);
        kv_destroy(store);
        if (ok_throughput != expected || ok_latency != expected) {
            fail("an operation returned the wrong result, so the numbers would be meaningless");
        }
    }
    result->throughput = (double)cfg->ops * 1e9 / (double)(elapsed > 0 ? elapsed : 1);
    qsort(samples, cfg->ops, sizeof *samples, compare_u64);
    result->p50 = percentile(samples, cfg->ops, 500000);
    result->p99 = percentile(samples, cfg->ops, 990000);
    result->p999 = percentile(samples, cfg->ops, 999000);
    result->max = samples[cfg->ops - 1];
}

static void write_row(FILE *csv, const config *cfg, size_t run, const run_result *r,
                      uint64_t timer_ns) {
    fprintf(csv,
            "%s,%zu,%zu,%g,%zu,%.0f,%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64
            ",%zu,%zu\n",
            workload_names[cfg->kind], cfg->keys, cfg->ops, cfg->max_load, run, r->throughput,
            r->p50, r->p99, r->p999, r->max, timer_ns, r->stats.capacity, r->stats.longest_chain);
    fflush(csv);
}

static void print_summary(const run_result *results, size_t runs) {
    double throughput[MAX_RUNS] = {0};
    double p50[MAX_RUNS] = {0};
    double p99[MAX_RUNS] = {0};
    for (size_t r = 0; r < runs; r++) {
        throughput[r] = results[r].throughput;
        p50[r] = (double)results[r].p50;
        p99[r] = (double)results[r].p99;
    }
    double middle = median(throughput, runs);
    double spread = 0;
    for (size_t r = 0; r < runs; r++) {
        double gap = throughput[r] > middle ? throughput[r] - middle : middle - throughput[r];
        if (gap / middle > spread) {
            spread = gap / middle;
        }
    }
    printf("  median %.2f M ops/s (every run within %.1f%%), p50 %.0f ns, p99 %.0f ns\n",
           middle / 1e6, spread * 100, median(p50, runs), median(p99, runs));
}

static void run_workload(const config *cfg, uint64_t timer_ns, FILE *csv) {
    rng_state = cfg->seed;
    key_text *keys = allocate(cfg->keys, sizeof *keys);
    for (size_t i = 0; i < cfg->keys; i++) {
        make_key(keys[i].text, "key:", i);
    }
    request *requests = NULL;
    if (cfg->kind != INSERT) {
        requests = allocate(cfg->ops, sizeof *requests);
        for (size_t i = 0; i < cfg->ops; i++) {
            uint64_t index = next_random() % cfg->keys;
            make_key(requests[i].key, cfg->kind == GET_MISS ? "mis:" : "key:", index);
            requests[i].put = cfg->kind == MIXED && next_random() % 10 == 0;
        }
    }
    uint64_t *samples = allocate(cfg->ops, sizeof *samples);

    printf("%s: %zu keys, %zu operations per pass, max load %g\n", workload_names[cfg->kind],
           cfg->keys, cfg->ops, cfg->max_load);
    run_result warmup;
    run_result results[MAX_RUNS];
    run_once(cfg, keys, requests, samples, &warmup);
    for (size_t r = 0; r < cfg->runs; r++) {
        run_once(cfg, keys, requests, samples, &results[r]);
        printf("  run %zu: %6.2f M ops/s  p50 %" PRIu64 " ns  p99 %" PRIu64 " ns  p99.9 %" PRIu64
               " ns  max %" PRIu64 " ns\n",
               r + 1, results[r].throughput / 1e6, results[r].p50, results[r].p99, results[r].p999,
               results[r].max);
        if (csv != NULL) {
            write_row(csv, cfg, r + 1, &results[r], timer_ns);
        }
    }
    print_summary(results, cfg->runs);

    free(samples);
    free(requests);
    free(keys);
}

static void usage(const char *program) {
    fprintf(stderr,
            "usage: %s -w insert|get_hit|get_miss|mixed|all [-k keys] [-n operations] [-r runs]\n"
            "          [-l max_load_factor] [-s seed] [-o results.csv]\n",
            program);
    exit(2);
}

static uint64_t parse_number(const char *text, uint64_t min, uint64_t max, const char *program) {
    char *end = NULL;
    unsigned long long value = strtoull(text, &end, 10);
    if (end == text || *end != '\0' || value < min || value > max) {
        usage(program);
    }
    return (uint64_t)value;
}

int main(int argc, char **argv) {
    config cfg = {.kind = WORKLOAD_COUNT,
                  .keys = 100000,
                  .ops = 5000000,
                  .runs = 5,
                  .max_load = 0.75,
                  .seed = 42};
    const char *csv_path = NULL;
    bool all = false;
    int option;
    setvbuf(stdout, NULL, _IOLBF, 0);
    while ((option = getopt(argc, argv, "w:k:n:r:l:s:o:")) != -1) {
        char *end = NULL;
        switch (option) {
        case 'w':
            all = strcmp(optarg, "all") == 0;
            for (int w = 0; w < WORKLOAD_COUNT; w++) {
                if (strcmp(optarg, workload_names[w]) == 0) {
                    cfg.kind = (workload)w;
                }
            }
            break;
        case 'k':
            cfg.keys = (size_t)parse_number(optarg, 1, MAX_COUNT, argv[0]);
            break;
        case 'n':
            cfg.ops = (size_t)parse_number(optarg, 1, MAX_COUNT, argv[0]);
            break;
        case 'r':
            cfg.runs = (size_t)parse_number(optarg, 1, MAX_RUNS, argv[0]);
            break;
        case 'l':
            cfg.max_load = strtod(optarg, &end);
            if (end == optarg || *end != '\0' || !(cfg.max_load > 0.0 && cfg.max_load <= 1000.0)) {
                usage(argv[0]);
            }
            break;
        case 's':
            cfg.seed = parse_number(optarg, 0, UINT64_MAX, argv[0]);
            break;
        case 'o':
            csv_path = optarg;
            break;
        default:
            usage(argv[0]);
        }
    }
    if (optind != argc || (!all && cfg.kind == WORKLOAD_COUNT)) {
        usage(argv[0]);
    }

    rng_state = cfg.seed;
    for (size_t v = 0; v < VALUE_POOL; v++) {
        for (size_t b = 0; b < VALUE_LEN; b++) {
            values[v][b] = (unsigned char)next_random();
        }
    }

    FILE *csv = NULL;
    if (csv_path != NULL) {
        FILE *existing = fopen(csv_path, "r");
        bool is_new = existing == NULL || fgetc(existing) == EOF;
        if (existing != NULL) {
            fclose(existing);
        }
        csv = fopen(csv_path, "a");
        if (csv == NULL) {
            perror(csv_path);
            return 1;
        }
        if (is_new) {
            fprintf(csv, "workload,keys,ops,max_load,run,throughput_ops_s,p50_ns,p99_ns,p999_ns,"
                         "max_ns,timer_ns,capacity,longest_chain\n");
        }
    }

    uint64_t timer_ns = timer_overhead();
    printf("timer overhead: %" PRIu64 " ns per clock_gettime pair\n", timer_ns);
    for (int w = 0; w < WORKLOAD_COUNT; w++) {
        if (all || cfg.kind == (workload)w) {
            config run_cfg = cfg;
            run_cfg.kind = (workload)w;
            if (run_cfg.kind == INSERT) {
                size_t builds = cfg.ops > cfg.keys ? cfg.ops / cfg.keys : 1;
                run_cfg.ops = builds * cfg.keys;
            }
            run_workload(&run_cfg, timer_ns, csv);
        }
    }

    if (csv != NULL) {
        fclose(csv);
    }
    return 0;
}
