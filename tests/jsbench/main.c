#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <3ds/types.h>

// MuJS uses a roughly 33 KiB stack frame when compiling regular expressions.
// Match the browser's 1 MiB main-thread stack so this probe measures the engine,
// not an undersized default 3DSX stack.
u32 __stacksize__ = 1024 * 1024;

#ifdef BENCH_QUICKJS
#include "quickjs.h"
#define ENGINE_NAME "QuickJS"
#else
#include "mujs.h"
#define ENGINE_NAME "MuJS"
#endif

#define JS_MEMORY_BUDGET (4 * 1024 * 1024)
#define SCRIPT "var total=0, text=''; for(var i=0;i<100000;i++){total+=i;if((i%1000)==0)text+='x'+(i/1000);} var benchmarkResult=total+text.length;"

static void logResult(const char* label, long value)
{
    printf("%s=%ld\n", label, value);
    FILE* f = fopen("sdmc:/jsbench.txt", "a");
    if (f) { fprintf(f, "%s=%ld\n", label, value); fclose(f); }
}

static u64 nowMs(void) { return osGetTime(); }

#ifdef BENCH_QUICKJS
#define QUICKJS_ALLOC_OVERHEAD 8
typedef struct { u64 size; } QuickJsAllocHeader;

static void* trackedQuickJsMalloc(JSMallocState* state, size_t size)
{
    if (size > SIZE_MAX - QUICKJS_ALLOC_OVERHEAD ||
        state->malloc_size > state->malloc_limit ||
        size + QUICKJS_ALLOC_OVERHEAD > state->malloc_limit - state->malloc_size)
        return NULL;
    QuickJsAllocHeader* header = (QuickJsAllocHeader*)malloc(
        sizeof(QuickJsAllocHeader) + size);
    if (!header) return NULL;
    header->size = (u64)size;
    state->malloc_count++;
    state->malloc_size += size + QUICKJS_ALLOC_OVERHEAD;
    return header + 1;
}

static void trackedQuickJsFree(JSMallocState* state, void* ptr)
{
    if (!ptr) return;
    QuickJsAllocHeader* header = (QuickJsAllocHeader*)ptr - 1;
    state->malloc_count--;
    state->malloc_size -= (size_t)header->size + QUICKJS_ALLOC_OVERHEAD;
    free(header);
}

static void* trackedQuickJsRealloc(JSMallocState* state, void* ptr, size_t size)
{
    if (!ptr) return size ? trackedQuickJsMalloc(state, size) : NULL;
    if (!size) {
        trackedQuickJsFree(state, ptr);
        return NULL;
    }

    QuickJsAllocHeader* oldHeader = (QuickJsAllocHeader*)ptr - 1;
    size_t oldSize = (size_t)oldHeader->size;
    size_t baseSize = state->malloc_size - oldSize - QUICKJS_ALLOC_OVERHEAD;
    if (size > SIZE_MAX - QUICKJS_ALLOC_OVERHEAD ||
        baseSize > state->malloc_limit ||
        size + QUICKJS_ALLOC_OVERHEAD > state->malloc_limit - baseSize)
        return NULL;

    QuickJsAllocHeader* newHeader = (QuickJsAllocHeader*)realloc(
        oldHeader, sizeof(QuickJsAllocHeader) + size);
    if (!newHeader) return NULL;
    newHeader->size = (u64)size;
    state->malloc_size = baseSize + size + QUICKJS_ALLOC_OVERHEAD;
    return newHeader + 1;
}

static size_t trackedQuickJsUsableSize(const void* ptr)
{
    const QuickJsAllocHeader* header = (const QuickJsAllocHeader*)ptr - 1;
    return (size_t)header->size;
}

static const JSMallocFunctions trackedQuickJsAllocator = {
    trackedQuickJsMalloc,
    trackedQuickJsFree,
    trackedQuickJsRealloc,
    trackedQuickJsUsableSize
};

static void logQuickJsHeap(JSRuntime* rt, const char* label)
{
    JSMemoryUsage usage;
    JS_ComputeMemoryUsage(rt, &usage);
    logResult(label, (long)usage.malloc_size);
}
#else
typedef struct { u64 size; } MuAllocHeader;
typedef struct { size_t current; size_t peak; } MuAllocStats;

static void* trackedMuAlloc(void* opaque, void* ptr, int size)
{
    MuAllocStats* stats = (MuAllocStats*)opaque;
    MuAllocHeader* oldHeader = ptr ? ((MuAllocHeader*)ptr - 1) : NULL;
    size_t oldSize = oldHeader ? (size_t)oldHeader->size : 0;

    if (size <= 0) {
        if (oldHeader) {
            stats->current -= oldSize;
            free(oldHeader);
        }
        return NULL;
    }

    MuAllocHeader* newHeader = (MuAllocHeader*)realloc(
        oldHeader, sizeof(MuAllocHeader) + (size_t)size);
    if (!newHeader) return NULL;
    newHeader->size = (u64)size;
    stats->current = stats->current - oldSize + (size_t)size;
    if (stats->current > stats->peak) stats->peak = stats->current;
    return newHeader + 1;
}
#endif

int main(void)
{
    gfxInitDefault();
    consoleInit(GFX_TOP, NULL);
    fsInit();
    FILE* f = fopen("sdmc:/jsbench.txt", "w");
    if (f) { fprintf(f, "engine=%s\n", ENGINE_NAME); fclose(f); }
    printf("Mesure moteur JavaScript: %s\n", ENGINE_NAME);
    printf("Script: 100000 iterations + chaines\n\n");
    printf("Memoire systeme libre: non disponible via cette sonde\n");

#ifdef BENCH_QUICKJS
    u64 t0 = nowMs();
    JSRuntime* rt = JS_NewRuntime2(&trackedQuickJsAllocator, NULL);
    if (!rt) { printf("ERREUR runtime\n"); goto done; }
    JS_SetMemoryLimit(rt, JS_MEMORY_BUDGET);
    JS_SetMaxStackSize(rt, 96 * 1024);
    JSContext* ctx = JS_NewContext(rt);
    if (!ctx) { printf("ERREUR contexte\n"); JS_FreeRuntime(rt); goto done; }
    logResult("init_ms", (long)(nowMs() - t0));
    logQuickJsHeap(rt, "engine_heap_after_init_bytes");
    t0 = nowMs();
    JSValue result = JS_Eval(ctx, SCRIPT, strlen(SCRIPT), "bench.js", JS_EVAL_TYPE_GLOBAL);
    const int failed = JS_IsException(result);
    JS_FreeValue(ctx, result);
    logResult("script_ms", (long)(nowMs() - t0));
    logResult("script_ok", failed ? 0 : 1);
    logQuickJsHeap(rt, "engine_heap_after_script_bytes");
    JS_FreeContext(ctx);
    JS_FreeRuntime(rt);
    logResult("engine_heap_after_shutdown_bytes", 0);
#else
    MuAllocStats muAlloc = {0, 0};
    u64 t0 = nowMs();
    js_State* J = js_newstate(trackedMuAlloc, &muAlloc, 0);
    if (!J) { printf("ERREUR runtime\n"); goto done; }
    js_setlimit(J, 10000000, JS_MEMORY_BUDGET);
    logResult("init_ms", (long)(nowMs() - t0));
    logResult("engine_heap_after_init_bytes", (long)muAlloc.current);
    logResult("engine_heap_peak_after_init_bytes", (long)muAlloc.peak);
    t0 = nowMs();
    const int failed = js_dostring(J, SCRIPT);
    logResult("script_ms", (long)(nowMs() - t0));
    logResult("script_ok", failed ? 0 : 1);
    logResult("engine_heap_after_script_bytes", (long)muAlloc.current);
    logResult("engine_heap_peak_after_script_bytes", (long)muAlloc.peak);
    js_freestate(J);
    logResult("engine_heap_after_shutdown_bytes", (long)muAlloc.current);
#endif
done:
    printf("\nResultats aussi ecrits dans sdmc:/jsbench.txt\n");
    printf("START pour quitter.\n");
    while (aptMainLoop())
    {
        hidScanInput();
        if (keysDown() & KEY_START) break;
        gspWaitForVBlank();
    }
    fsExit();
    gfxExit();
    return 0;
}
