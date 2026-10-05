#include "susamune/crash_report.hxx"

#include "Dolphin/OS.h"
#include "JSystem/JUtility/JUTException.hxx"
#include "SMS/Camera/PolarSubCamera.hxx"
#include "SMS/System/Application.hxx"
#include "susamune/addresses.hxx"
#include "susamune/checksum.hxx"
#include "susamune/crash_report.h"
#include "susamune/mod_bin.h"

#ifndef MOONSHINE_CRASH_BUILD
#define MOONSHINE_CRASH_BUILD "Moonshine V2.3.3 Frame By Frame"
#endif

extern "C" void setJutPreUserCallback(JUTException::UserCallback)
    asm("setPreUserCallback__12JUTExceptionFPUsP9OSContextUlUl");
extern "C" JUTException::UserCallback jutPostUserCallback
    asm("sPostUserCallback__12JUTException");
extern "C" void *jutExceptionConsole asm("sConsole__12JUTException");
extern "C" void *jutConsoleManager asm("sManager__17JUTConsoleManager");
extern "C" void jutConsolePrint(void *, const char *, ...)
    asm("print_f__10JUTConsoleFPCce");
extern "C" void jutConsoleScroll(void *, int) asm("scroll__10JUTConsoleFi");
extern "C" void jutConsoleDraw(void *, bool)
    asm("drawDirect__17JUTConsoleManagerCFb");
extern "C" void jutExceptionWait(s32) asm("waitTime__12JUTExceptionFl");

namespace {

JUTException::UserCallback sPreviousHandler = nullptr;
JUTException::UserCallback sPreviousPostHandler = nullptr;
bool sCapturing = false;
bool sPhotoPrinted = false;
u32 sLastContext = 0xFFFFFFFFu;

void readTimeBase(unsigned int *high, unsigned int *low) {
    unsigned int nextHigh;
    do {
        asm volatile("mftbu %0" : "=r"(*high));
        asm volatile("mftb %0" : "=r"(*low));
        asm volatile("mftbu %0" : "=r"(nextHigh));
    } while (*high != nextHigh);
}

bool readableRange(u32 address, u32 size) {
    if (size == 0 || address + size < address)
        return false;
    return (address >= 0x80000000u && address + size <= 0x81800000u) ||
           (address >= 0x90000000u && address + size <= 0x94000000u);
}

void clearBytes(void *destination, u32 size) {
    volatile u8 *bytes = static_cast<volatile u8 *>(destination);
    for (u32 i = 0; i < size; ++i)
        bytes[i] = 0;
}

bool copyReadable(void *destination, u32 address, u32 size) {
    if (!readableRange(address, size))
        return false;
    volatile u8 *out = static_cast<volatile u8 *>(destination);
    const volatile u8 *in = reinterpret_cast<const volatile u8 *>(address);
    for (u32 i = 0; i < size; ++i)
        out[i] = in[i];
    return true;
}

u32 checksum(const SusamuneCrashReport *report) {
    return Checksum::crc32(
        report, sizeof(*report),
        __builtin_offsetof(SusamuneCrashReport, checksum),
        sizeof(report->checksum));
}

u32 packScene(const TGameSequence &scene) {
    return static_cast<u32>(scene.mAreaID) << 24 |
           static_cast<u32>(scene.mEpisodeID) << 16 | scene.mFlag.mVal;
}

bool captureCore(u16 exception, OSContext *context, u32 dsisr, u32 dar,
                 const SusamuneCrashReport *report) {
    SusamuneCrashCore *core = SUSAMUNE_CRASH_CORE_PPC_PTR;
    clearBytes(core, sizeof(*core));
    core->magic = SUSAMUNE_CRASH_CORE_MAGIC;
    core->version = SUSAMUNE_CRASH_CORE_VERSION;
    core->reportSize = sizeof(*core);
    core->captureSeq = report->captureSeq;
    core->gameId = report->gameId;
    core->modFileCrc32 = report->modFileCrc32;
    core->exception = exception;
    core->dsisr = dsisr;
    core->dar = dar;
    core->contextValid = (reinterpret_cast<u32>(context) & 3u) == 0 &&
        readableRange(reinterpret_cast<u32>(context), sizeof(*context));
    if (core->contextValid) {
        for (u32 i = 0; i < 32; ++i) core->gpr[i] = context->mGPR[i];
        core->cr = context->mCR;
        core->lr = context->mLR;
        core->ctr = context->mCTR;
        core->xer = context->mXER;
        core->srr0 = context->mSRR0;
        core->srr1 = context->mSRR1;
    }
    core->appContext = gpApplication.mContext;
    core->currentScene = packScene(gpApplication.mCurrentScene);
    core->prevScene = packScene(gpApplication.mPrevScene);
    core->nextScene = packScene(gpApplication.mNextScene);
    readTimeBase(&core->timeBaseHigh, &core->timeBaseLow);
    if (report->breadcrumbCount != 0) {
        const SusamuneCrashBreadcrumb &last = report->breadcrumbs[
            (report->breadcrumbSeq - 1u) % SUSAMUNE_CRASH_BREADCRUMB_COUNT];
        core->lastEvent = last.event;
        core->lastArg0 = last.arg0;
        core->lastArg1 = last.arg1;
    }
    static const char build[] = MOONSHINE_CRASH_BUILD;
    for (u32 i = 0; i < sizeof(build) - 1 && i < sizeof(core->build) - 1; ++i)
        core->build[i] = build[i];
    core->state = SUSAMUNE_CRASH_STATE_READY;
    core->checksum = Checksum::crc32(core, sizeof(*core),
        __builtin_offsetof(SusamuneCrashCore, checksum), sizeof(core->checksum));
    DCStoreRange(reinterpret_cast<u8 *>(core) + 32, sizeof(*core) - 32);
    DCStoreRange(core, 32);
    return core->contextValid != 0;
}

void captureBacktrace(SusamuneCrashReport *report, u32 stackPointer) {
    clearBytes(report->backtrace, sizeof(report->backtrace));
    u32 frame = stackPointer;
    for (u32 i = 0; i < SUSAMUNE_CRASH_BACKTRACE_COUNT; ++i) {
        if ((frame & 3u) != 0 || !readableRange(frame, 8))
            break;
        const volatile u32 *words = reinterpret_cast<const volatile u32 *>(frame);
        const u32 next = words[0];
        report->backtrace[i].stackPointer = frame;
        report->backtrace[i].returnAddress = words[1];
        if (next <= frame || next - frame > 0x00100000u)
            break;
        frame = next;
    }
}

void captureException(u16 exception, OSContext *context, u32 dsisr, u32 dar) {
    SusamuneCrashReport *report = SUSAMUNE_CRASH_PPC_PTR;
    if (!sCapturing && report->magic == SUSAMUNE_CRASH_MAGIC &&
        report->version == SUSAMUNE_CRASH_VERSION &&
        report->reportSize == sizeof(*report) &&
        report->state == SUSAMUNE_CRASH_STATE_ARMED) {
        sCapturing = true;
        const bool validContext = captureCore(exception, context, dsisr, dar, report);
        report->state = SUSAMUNE_CRASH_STATE_WRITING;
        DCStoreRange(report, 32);

        if (!validContext) {
            if (sPreviousHandler && sPreviousHandler != captureException)
                sPreviousHandler(exception, context, dsisr, dar);
            return;
        }

        report->exception = exception;
        report->captureFlags = 0;
        report->dsisr = dsisr;
        report->dar = dar;
        readTimeBase(&report->timeBaseHigh, &report->timeBaseLow);
        for (u32 i = 0; i < 32; ++i)
            report->gpr[i] = context->mGPR[i];
        report->cr = context->mCR;
        report->lr = context->mLR;
        report->ctr = context->mCTR;
        report->xer = context->mXER;
        report->srr0 = context->mSRR0;
        report->srr1 = context->mSRR1;
        report->contextMode = context->mMode;
        report->contextState = context->mState;
        report->currentThread = reinterpret_cast<u32>(OSGetCurrentThread());

        report->appAddress = reinterpret_cast<u32>(&gpApplication);
        report->appDirector = reinterpret_cast<u32>(gpApplication.mDirector);
        report->appHeap = reinterpret_cast<u32>(gpApplication.mCurrentHeap);
        report->appContext = gpApplication.mContext;
        report->prevScene = packScene(gpApplication.mPrevScene);
        report->currentScene = packScene(gpApplication.mCurrentScene);
        report->nextScene = packScene(gpApplication.mNextScene);
        report->cutSceneId = gpApplication.mCutSceneID;
        report->marDirector = reinterpret_cast<u32>(gpMarDirector);
        report->mario = reinterpret_cast<u32>(gpMarioOriginal);
        report->camera = reinterpret_cast<u32>(gpCamera);

        report->directorReady = 0;
        report->directorStateAreaEpisode = 0;
        report->directorGameState = 0;
        report->directorDemoStates = 0;
        report->directorCollectedShine = 0;
        report->directorWindowBase = report->marDirector;
        report->directorWindowSize = 0;
        clearBytes(report->directorWindow, sizeof(report->directorWindow));
        if ((report->marDirector & 3u) == 0 &&
            readableRange(report->marDirector, sizeof(TMarDirector)) &&
            copyReadable(report->directorWindow, report->marDirector,
                         sizeof(report->directorWindow))) {
            report->captureFlags |= SUSAMUNE_CRASH_FLAG_DIRECTOR;
            report->directorWindowSize = sizeof(report->directorWindow);
            report->directorReady = gpMarDirector->_260;
            report->directorStateAreaEpisode =
                static_cast<u32>(gpMarDirector->mCurState) << 24 |
                static_cast<u32>(gpMarDirector->mAreaID) << 16 |
                static_cast<u32>(gpMarDirector->mEpisodeID) << 8 |
                gpMarDirector->_260;
            report->directorGameState = gpMarDirector->mGameState;
            report->directorDemoStates =
                static_cast<u32>(gpMarDirector->mDemoState) << 24 |
                static_cast<u32>(gpMarDirector->mPreviousDemoState) << 16 |
                static_cast<u32>(gpMarDirector->mNextDemoState) << 8;
            report->directorCollectedShine =
                reinterpret_cast<u32>(gpMarDirector->mCollectedShine);
        }

        report->marioWindowBase = report->mario;
        report->marioWindowSize = 0;
        clearBytes(report->marioWindow, sizeof(report->marioWindow));
        if (copyReadable(report->marioWindow, report->mario,
                         sizeof(report->marioWindow))) {
            report->captureFlags |= SUSAMUNE_CRASH_FLAG_MARIO;
            report->marioWindowSize = sizeof(report->marioWindow);
        }

        captureBacktrace(report, context->mGPR[1]);
        report->stackBase = context->mGPR[1];
        report->stackSize = 0;
        clearBytes(report->stack, sizeof(report->stack));
        if (copyReadable(report->stack, report->stackBase,
                         sizeof(report->stack))) {
            report->captureFlags |= SUSAMUNE_CRASH_FLAG_STACK;
            report->stackSize = sizeof(report->stack);
        }

        report->pcWindowBase = (context->mSRR0 - 32u) & ~3u;
        report->pcWindowSize = 0;
        clearBytes(report->pcWindow, sizeof(report->pcWindow));
        if (context->mSRR0 >= 32u &&
            copyReadable(report->pcWindow, report->pcWindowBase,
                         sizeof(report->pcWindow))) {
            report->captureFlags |= SUSAMUNE_CRASH_FLAG_PC_WINDOW;
            report->pcWindowSize = sizeof(report->pcWindow);
        }

        report->lrWindowBase = (context->mLR - 32u) & ~3u;
        report->lrWindowSize = 0;
        clearBytes(report->lrWindow, sizeof(report->lrWindow));
        if (context->mLR >= 32u &&
            copyReadable(report->lrWindow, report->lrWindowBase,
                         sizeof(report->lrWindow))) {
            report->captureFlags |= SUSAMUNE_CRASH_FLAG_LR_WINDOW;
            report->lrWindowSize = sizeof(report->lrWindow);
        }

        report->state = SUSAMUNE_CRASH_STATE_READY;
        report->checksum = 0;
        report->checksum = checksum(report);
        DCStoreRange(reinterpret_cast<u8 *>(report) + 32,
                     sizeof(*report) - 32);
        DCStoreRange(report, 32);
    }

    if (sPreviousHandler && sPreviousHandler != captureException)
        sPreviousHandler(exception, context, dsisr, dar);
}

void printPhotoReport(u16 exception, OSContext *context, u32 dsisr, u32 dar) {
    if (sPreviousPostHandler && sPreviousPostHandler != printPhotoReport)
        sPreviousPostHandler(exception, context, dsisr, dar);
    const SusamuneCrashCore *core = SUSAMUNE_CRASH_CORE_PPC_PTR;
    if (sPhotoPrinted || !jutExceptionConsole || !jutConsoleManager ||
        core->magic != SUSAMUNE_CRASH_CORE_MAGIC ||
        core->state != SUSAMUNE_CRASH_STATE_READY) return;
    sPhotoPrinted = true;

    const SusamuneCrashAck *ack = SUSAMUNE_CRASH_ACK_PPC_PTR;
    DCInvalidateRange(const_cast<SusamuneCrashAck *>(ack), sizeof(*ack));
    const char *saved = "not confirmed";
    if (ack->magic == SUSAMUNE_CRASH_ACK_MAGIC) {
        if (ack->status == SUSAMUNE_CRASH_ACK_UNAVAILABLE) saved = "no storage";
        else if (ack->captureSeq == core->captureSeq) {
            switch (ack->status) {
            case SUSAMUNE_CRASH_ACK_SAVED: saved = "complete"; break;
            case SUSAMUNE_CRASH_ACK_CORE_ONLY: saved = "minimal report"; break;
            case SUSAMUNE_CRASH_ACK_FAILED: saved = "write failed / partial"; break;
            default: saved = "pending"; break;
            }
        }
    }
    const char *region = core->gameId == SUSAMUNE_MOD_GAME_ID_JP ? "JP/GMSJ" :
        core->gameId == SUSAMUNE_MOD_GAME_ID_US ? "US/GMSE" : "PAL/GMSP";
    // Retail calls this with a live console after its exception pages.
    // Keep the core independent: optional rendering may itself be damaged.
    jutConsolePrint(jutExceptionConsole,
        "\nMOONSHINE V2.3.3 FRAME BY FRAME\n"
        "REPORT %08X-%08X\n%s  MOD %08X\n"
        "EXCEPTION %u  CONTEXT %u\n"
        "PC %08X  LR %08X\nSP %08X  DAR %08X\n"
        "DSISR %08X  SRR1 %08X\n"
        "SCENE %08X  LAST %u\nSAVE: %s\n"
        "Photo this block; share .core/.bin/.txt\n",
        core->captureSeq, core->checksum, region, core->modFileCrc32,
        core->exception, core->contextValid, core->srr0, core->lr,
        core->gpr[1], core->dar, core->dsisr, core->srr1,
        core->currentScene, core->lastEvent, saved);
    jutConsoleScroll(jutExceptionConsole, 0x1000);
    jutConsoleDraw(jutConsoleManager, true);
    jutExceptionWait(8000);
}

}  // namespace

namespace CrashReport {

void init() {
    SusamuneCrashReport *report = SUSAMUNE_CRASH_PPC_PTR;
    DCInvalidateRange(report, sizeof(*report));
    if (report->magic != SUSAMUNE_CRASH_MAGIC ||
        report->version != SUSAMUNE_CRASH_VERSION ||
        report->reportSize != sizeof(*report)) {
        clearBytes(report, sizeof(*report));
        report->magic = SUSAMUNE_CRASH_MAGIC;
        report->version = SUSAMUNE_CRASH_VERSION;
        report->reportSize = sizeof(*report);
        report->state = SUSAMUNE_CRASH_STATE_ARMED;
        report->captureSeq = 1;
        report->gameId =
#if defined(SUSAMUNE_VERSION_JP)
            SUSAMUNE_MOD_GAME_ID_JP;
#elif defined(SUSAMUNE_VERSION_US)
            SUSAMUNE_MOD_GAME_ID_US;
#else
            SUSAMUNE_MOD_GAME_ID_PAL;
#endif
        DCStoreRange(report, sizeof(*report));
    }
    if (report->state == SUSAMUNE_CRASH_STATE_DISABLED) {
        report->state = SUSAMUNE_CRASH_STATE_ARMED;
        DCStoreRange(report, 32);
    }
    if (report->magic != SUSAMUNE_CRASH_MAGIC ||
        report->version != SUSAMUNE_CRASH_VERSION ||
        report->reportSize != sizeof(*report) ||
        report->state != SUSAMUNE_CRASH_STATE_ARMED)
        return;

    sPreviousHandler = JUTException::sPreUserCallback;
    sPreviousPostHandler = jutPostUserCallback;
    clearBytes(SUSAMUNE_CRASH_CORE_PPC_PTR, sizeof(SusamuneCrashCore));
    DCStoreRange(SUSAMUNE_CRASH_CORE_PPC_PTR, sizeof(SusamuneCrashCore));
    setJutPreUserCallback(captureException);
    jutPostUserCallback = printPhotoReport;
    note(SUSAMUNE_CRASH_EVENT_APP_INIT, SUSAMUNE_GAME_VERSION,
         reinterpret_cast<u32>(sPreviousHandler));
}

void note(u32 event, u32 arg0, u32 arg1) {
    const bool interrupts = OSDisableInterrupts();
    SusamuneCrashReport *report = SUSAMUNE_CRASH_PPC_PTR;
    if (report->magic != SUSAMUNE_CRASH_MAGIC ||
        report->state != SUSAMUNE_CRASH_STATE_ARMED) {
        OSRestoreInterrupts(interrupts);
        return;
    }
    const u32 sequence = report->breadcrumbSeq;
    SusamuneCrashBreadcrumb &entry =
        report->breadcrumbs[sequence % SUSAMUNE_CRASH_BREADCRUMB_COUNT];
    entry.event = event;
    readTimeBase(&entry.timeBaseHigh, &entry.timeBaseLow);
    entry.arg0 = arg0;
    entry.arg1 = arg1;
    report->breadcrumbSeq = sequence + 1;
    if (report->breadcrumbCount < SUSAMUNE_CRASH_BREADCRUMB_COUNT)
        ++report->breadcrumbCount;
    OSRestoreInterrupts(interrupts);
}

void observeContext(u32 context) {
    if (context == sLastContext)
        return;
    note(SUSAMUNE_CRASH_EVENT_CONTEXT, sLastContext, context);
    sLastContext = context;
}

}  // namespace CrashReport
