/* Two linked GBA systems in one libretro core. MPL-2.0, like mGBA.
 * Uses upstream mGBA's SIO coordinator with a cooperative single-thread scheduler.
 * No game code, assets, or ROM patches are included. */
#include "libretro.h"
#include <mgba/core/core.h>
#include <mgba/core/lockstep.h>
#include <mgba/core/log.h>
#include <mgba/core/serialize.h>
#include <mgba/internal/gba/gba.h>
#include <mgba/internal/gba/sio/lockstep.h>
#include <mgba-util/audio-buffer.h>
#include <mgba-util/memory.h>
#include <mgba-util/vfs.h>

#define WIDTH 240
#define HEIGHT 160
#define SAVE_BYTES GBA_SIZE_FLASH1M
#define STATE_SLOT (1024 * 1024)
#define STATE_HEADER 32
#define STATE_BYTES (STATE_HEADER + 2 * STATE_SLOT)

struct Player {
    /* First member: callbacks safely cast the mLockstepUser back to Player. */
    struct mLockstepUser user;
    struct mCore* core;
    struct GBASIOLockstepDriver driver;
    struct mCoreCallbacks callbacks;
    void* rom;
    size_t romSize;
    unsigned id;
    bool asleep;
    bool initialized;
    uint64_t frames;
    mColor pixels[WIDTH * HEIGHT];
    mColor complete[WIDTH * HEIGHT];
};

static struct Player players[2];
static struct GBASIOLockstepCoordinator coordinator;
static bool linked;
static bool loaded;
static bool failed;
static uint8_t saves[2 * SAVE_BYTES];
static mColor composite[2 * WIDTH * HEIGHT];
static retro_environment_t environment;
static retro_video_refresh_t video;
static retro_audio_sample_batch_t audio;
static retro_audio_sample_t audioSingle;
static retro_input_poll_t inputPoll;
static retro_input_state_t inputState;
static retro_log_printf_t logPrint;
static struct mLogger logger;
static unsigned layout;
static unsigned audioPlayer;
static unsigned reportedAudioRate = 32768;
static const unsigned keymap[10] = {
    RETRO_DEVICE_ID_JOYPAD_A, RETRO_DEVICE_ID_JOYPAD_B,
    RETRO_DEVICE_ID_JOYPAD_SELECT, RETRO_DEVICE_ID_JOYPAD_START,
    RETRO_DEVICE_ID_JOYPAD_RIGHT, RETRO_DEVICE_ID_JOYPAD_LEFT,
    RETRO_DEVICE_ID_JOYPAD_UP, RETRO_DEVICE_ID_JOYPAD_DOWN,
    RETRO_DEVICE_ID_JOYPAD_R, RETRO_DEVICE_ID_JOYPAD_L
};

static void message(const char* text) {
    if (logPrint) logPrint(RETRO_LOG_ERROR, "[Kirby Co-op] %s\n", text);
    struct retro_message msg = {text, 600};
    if (environment) environment(RETRO_ENVIRONMENT_SET_MESSAGE, &msg);
}

static void mgbaLog(struct mLogger* log, int category, enum mLogLevel level,
                    const char* format, va_list args) {
    (void)log; (void)category;
    if (level != mLOG_ERROR && level != mLOG_FATAL && level != mLOG_WARN) return;
    char text[512];
    vsnprintf(text, sizeof(text), format, args);
    if (logPrint) logPrint(level == mLOG_WARN ? RETRO_LOG_WARN : RETRO_LOG_ERROR,
                           "[mGBA] %s\n", text);
}

static void sleepPlayer(struct mLockstepUser* user) {
    ((struct Player*)user)->asleep = true;
}

static void wakePlayer(struct mLockstepUser* user) {
    ((struct Player*)user)->asleep = false;
}

static int requestedId(struct mLockstepUser* user) {
    return (int)((struct Player*)user)->id;
}

static void frameEnded(void* context) {
    struct Player* p = context;
    ++p->frames;
    memcpy(p->complete, p->pixels, sizeof(p->pixels));
}

static void geometry(struct retro_game_geometry* g) {
    g->base_width = layout == 0 ? WIDTH * 2 : WIDTH;
    g->base_height = layout == 1 ? HEIGHT * 2 : HEIGHT;
    g->max_width = WIDTH * 2;
    g->max_height = HEIGHT * 2;
    g->aspect_ratio = (float)g->base_width / g->base_height;
}

static void options(bool notify) {
    struct retro_variable v = {"kirby_coop_layout", NULL};
    unsigned previous = layout;
    if (environment && environment(RETRO_ENVIRONMENT_GET_VARIABLE, &v) && v.value) {
        if (!strcmp(v.value, "Stacked")) layout = 1;
        else if (!strcmp(v.value, "Player 1 fullscreen")) layout = 2;
        else if (!strcmp(v.value, "Player 2 fullscreen")) layout = 3;
        else layout = 0;
    }
    v.key = "kirby_coop_audio";
    v.value = NULL;
    if (environment && environment(RETRO_ENVIRONMENT_GET_VARIABLE, &v) && v.value)
        audioPlayer = !strcmp(v.value, "Player 2") ? 1 : 0;
    if (notify && previous != layout) {
        struct retro_game_geometry g;
        geometry(&g);
        environment(RETRO_ENVIRONMENT_SET_GEOMETRY, &g);
    }
}

static void connectPlayers(void) {
    GBASIOLockstepCoordinatorInit(&coordinator);
    linked = true;
    for (unsigned i = 0; i < 2; ++i) {
        struct Player* p = &players[i];
        p->asleep = false;
        p->user.sleep = sleepPlayer;
        p->user.wake = wakePlayer;
        p->user.requestedId = requestedId;
        GBASIOLockstepDriverCreate(&p->driver, &p->user);
        GBASIOLockstepCoordinatorAttach(&coordinator, &p->driver);
        GBASIOSetDriver(&((struct GBA*)p->core->board)->sio, &p->driver.d);
    }
}

static void disconnectPlayers(void) {
    if (!linked) return;
    for (unsigned i = 0; i < 2; ++i)
        if (players[i].initialized)
            GBASIOSetDriver(&((struct GBA*)players[i].core->board)->sio, NULL);
    GBASIOLockstepCoordinatorDeinit(&coordinator);
    linked = false;
}

void retro_set_environment(retro_environment_t cb) {
    environment = cb;
    static const struct retro_variable vars[] = {
        {"kirby_coop_layout", "Screen layout; Side by side|Stacked|Player 1 fullscreen|Player 2 fullscreen"},
        {"kirby_coop_audio", "Game audio; Player 1|Player 2"},
        {NULL, NULL}
    };
    cb(RETRO_ENVIRONMENT_SET_VARIABLES, (void*)vars);
    static const struct retro_controller_description pads[] = {{"GBA controller", RETRO_DEVICE_JOYPAD}};
    static const struct retro_controller_info ports[] = {{pads, 1}, {pads, 1}, {NULL, 0}};
    cb(RETRO_ENVIRONMENT_SET_CONTROLLER_INFO, (void*)ports);
    static const struct retro_input_descriptor descriptors[] = {
        {0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_A,"A / Jump"},
        {0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_B,"B / Inhale"},
        {0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_UP,"Up"},
        {0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_DOWN,"Down"},
        {0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_LEFT,"Left"},
        {0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_RIGHT,"Right"},
        {0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_START,"Start"},
        {0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_SELECT,"Select"},
        {0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_L,"L"},
        {0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_R,"R / Call"},
        {1,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_A,"A / Jump"},
        {1,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_B,"B / Inhale"},
        {1,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_UP,"Up"},
        {1,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_DOWN,"Down"},
        {1,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_LEFT,"Left"},
        {1,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_RIGHT,"Right"},
        {1,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_START,"Start"},
        {1,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_SELECT,"Select"},
        {1,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_L,"L"},
        {1,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_R,"R / Call"},
        {0,0,0,0,NULL}
    };
    cb(RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS, (void*)descriptors);
}

void retro_set_video_refresh(retro_video_refresh_t cb) { video = cb; }
void retro_set_audio_sample(retro_audio_sample_t cb) { audioSingle = cb; }
void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) { audio = cb; }
void retro_set_input_poll(retro_input_poll_t cb) { inputPoll = cb; }
void retro_set_input_state(retro_input_state_t cb) { inputState = cb; }
unsigned retro_api_version(void) { return RETRO_API_VERSION; }
unsigned retro_get_region(void) { return RETRO_REGION_NTSC; }
void retro_set_controller_port_device(unsigned port, unsigned device) { (void)port; (void)device; }

void retro_init(void) {
    struct retro_log_callback log = {0};
    if (environment && environment(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &log)) logPrint = log.log;
    logger.log = mgbaLog;
    mLogSetDefaultLogger(&logger);
    options(false);
}

void retro_get_system_info(struct retro_system_info* info) {
    memset(info, 0, sizeof(*info));
    info->library_name = "Kirby Couch Co-op";
    info->library_version = "0.1 experimental (mGBA)";
    info->valid_extensions = "gba";
    info->need_fullpath = false;
}

void retro_get_system_av_info(struct retro_system_av_info* info) {
    memset(info, 0, sizeof(*info));
    geometry(&info->geometry);
    info->timing.fps = 16777216.0 / 280896.0;
    info->timing.sample_rate = loaded ? players[audioPlayer].core->audioSampleRate(players[audioPlayer].core) : 32768.0;
}

void retro_unload_game(void) {
    disconnectPlayers();
    for (unsigned i = 0; i < 2; ++i) {
        struct Player* p = &players[i];
        if (p->core) {
            if (p->initialized) {
                mCoreConfigDeinit(&p->core->config);
                p->core->deinit(p->core);
            }
            p->core = NULL;
        }
        if (p->rom) mappedMemoryFree(p->rom, p->romSize);
        p->rom = NULL;
        p->initialized = false;
    }
    loaded = false;
}

void retro_deinit(void) {
    retro_unload_game();
    mLogSetDefaultLogger(NULL);
    logPrint = NULL;
}

bool retro_load_game(const struct retro_game_info* game) {
    if (!game || !game->data || game->size < 0xC0) return false;
    retro_unload_game();
    enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_RGB565;
    if (!environment || !environment(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt)) return false;
    memset(players, 0, sizeof(players));
    memset(saves, 0xFF, sizeof(saves));
    failed = false;
    reportedAudioRate = 32768;
    options(false);
    for (unsigned i = 0; i < 2; ++i) {
        struct Player* p = &players[i];
        p->id = i;
        p->romSize = game->size;
        p->rom = anonymousMemoryMap(game->size);
        if (!p->rom) goto error;
        memcpy(p->rom, game->data, game->size);
        struct VFile* rom = VFileFromMemory(p->rom, game->size);
        if (!rom) goto error;
        p->core = mCoreFindVF(rom);
        if (!p->core) { rom->close(rom); goto error; }
        mCoreInitConfig(p->core, NULL);
        if (!p->core->init(p->core)) { rom->close(rom); goto error; }
        p->initialized = true;
        p->core->setVideoBuffer(p->core, p->pixels, WIDTH);
        p->core->setAudioBufferSize(p->core, 4096);
        struct mCoreOptions opts = {0};
        opts.useBios = true;
        opts.volume = 0x100;
        mCoreConfigLoadDefaults(&p->core->config, &opts);
        mCoreLoadConfig(p->core);
        if (!p->core->loadROM(p->core, rom)) { rom->close(rom); goto error; }
        struct VFile* save = VFileFromMemory(saves + i * SAVE_BYTES, SAVE_BYTES);
        if (!save || !p->core->loadSave(p->core, save)) {
            if (save) save->close(save);
            goto error;
        }
        const char* systemDir = NULL;
        if (environment(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &systemDir) && systemDir) {
            char path[4096];
            snprintf(path, sizeof(path), "%s/gba_bios.bin", systemDir);
            struct VFile* bios = VFileOpen(path, O_RDONLY);
            if (bios && !p->core->loadBIOS(p->core, bios, 0)) bios->close(bios);
        }
        p->callbacks.context = p;
        p->callbacks.videoFrameEnded = frameEnded;
        p->core->addCoreCallbacks(p->core, &p->callbacks);
        p->core->reset(p->core);
    }
    connectPlayers();
    loaded = true;
    if (logPrint) logPrint(RETRO_LOG_INFO, "[Kirby Co-op] Two GBA systems linked. Port 1=P1, Port 2=P2.\n");
    return true;
error:
    message("Could not load the GBA game into both linked systems.");
    retro_unload_game();
    return false;
}

bool retro_load_game_special(unsigned type, const struct retro_game_info* games, size_t count) {
    (void)type; (void)games; (void)count; return false;
}

void retro_reset(void) {
    if (!loaded) return;
    disconnectPlayers();
    for (unsigned i = 0; i < 2; ++i) {
        players[i].core->reset(players[i].core);
        players[i].frames = 0;
        players[i].asleep = false;
    }
    connectPlayers();
    failed = false;
}

static void present(void) {
    if (!video) return;
    if (layout >= 2) {
        video(players[layout - 2].complete, WIDTH, HEIGHT, WIDTH * sizeof(mColor));
    } else if (layout == 1) {
        memcpy(composite, players[0].complete, sizeof(players[0].complete));
        memcpy(composite + WIDTH * HEIGHT, players[1].complete, sizeof(players[1].complete));
        video(composite, WIDTH, HEIGHT * 2, WIDTH * sizeof(mColor));
    } else {
        for (unsigned y = 0; y < HEIGHT; ++y) {
            memcpy(composite + y * WIDTH * 2, players[0].complete + y * WIDTH, WIDTH * sizeof(mColor));
            memcpy(composite + y * WIDTH * 2 + WIDTH, players[1].complete + y * WIDTH, WIDTH * sizeof(mColor));
        }
        video(composite, WIDTH * 2, HEIGHT, WIDTH * 2 * sizeof(mColor));
    }
}

void retro_run(void) {
    if (!loaded) return;
    bool updated = false;
    if (environment(RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE, &updated) && updated) options(true);
    if (inputPoll) inputPoll();
    for (unsigned i = 0; i < 2; ++i) {
        uint32_t keys = 0;
        if (inputState)
            for (unsigned k = 0; k < 10; ++k)
                if (inputState(i, RETRO_DEVICE_JOYPAD, 0, keymap[k])) keys |= 1U << k;
        players[i].core->setKeys(players[i].core, keys);
    }
    if (!failed) {
        uint64_t target[2] = {players[0].frames + 1, players[1].frames + 1};
        unsigned guard = 0;
        while (players[0].frames < target[0] || players[1].frames < target[1]) {
            bool ran = false;
            for (unsigned i = 0; i < 2; ++i) {
                struct Player* p = &players[i];
                if (p->asleep) continue;
                /* A finished player may still need to wake the other system. */
                if (p->frames >= target[i] && !players[1-i].asleep) continue;
                p->core->runLoop(p->core);
                ran = true;
            }
            if (!ran || ++guard > 200000) {
                failed = true;
                message("Link scheduler stalled. Restart the game; please report this prototype failure.");
                break;
            }
        }
    }
    present();
    unsigned rate = players[audioPlayer].core->audioSampleRate(players[audioPlayer].core);
    if (rate != reportedAudioRate) {
        struct retro_system_av_info av;
        retro_get_system_av_info(&av);
        environment(RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO, &av);
        reportedAudioRate = rate;
    }
    int16_t samples[4096 * 2];
    for (unsigned i = 0; i < 2; ++i) {
        struct mAudioBuffer* b = players[i].core->getAudioBuffer(players[i].core);
        size_t count = mAudioBufferAvailable(b);
        if (count > 4096) count = 4096;
        count = mAudioBufferRead(b, samples, count);
        if (i == audioPlayer) {
            if (audio) audio(samples, count);
            else if (audioSingle)
                for (size_t s = 0; s < count; ++s) audioSingle(samples[s*2], samples[s*2+1]);
        }
    }
}

size_t retro_serialize_size(void) { return loaded ? STATE_BYTES : 0; }

bool retro_serialize(void* data, size_t size) {
    if (!loaded || !data || size < STATE_BYTES) return false;
    uint8_t* dst = data;
    memset(dst, 0, STATE_BYTES);
    memcpy(dst, "KCOOP001", 8);
    for (unsigned i = 0; i < 2; ++i) {
        struct VFile* vf = VFileMemChunk(NULL, 0);
        if (!vf) return false;
        if (!mCoreSaveStateNamed(players[i].core, vf, SAVESTATE_SAVEDATA | SAVESTATE_RTC)) {
            vf->close(vf); return false;
        }
        size_t n = vf->size(vf);
        if (!n || n > STATE_SLOT) { vf->close(vf); return false; }
        /* Explicit little-endian container, independent of host word size. */
        for (unsigned b = 0; b < 4; ++b) dst[8 + i*4 + b] = (uint8_t)(n >> (b*8));
        dst[16+i] = players[i].asleep;
        vf->seek(vf, 0, SEEK_SET);
        bool ok = vf->read(vf, dst + STATE_HEADER + i*STATE_SLOT, n) == (ssize_t)n;
        vf->close(vf);
        if (!ok) return false;
    }
    return true;
}

bool retro_unserialize(const void* data, size_t size) {
    if (!loaded || !data || size < STATE_BYTES || memcmp(data, "KCOOP001", 8)) return false;
    const uint8_t* src = data;
    size_t lengths[2] = {0, 0};
    for (unsigned i = 0; i < 2; ++i) {
        for (unsigned b = 0; b < 4; ++b) lengths[i] |= (size_t)src[8+i*4+b] << (b*8);
        if (!lengths[i] || lengths[i] > STATE_SLOT) return false;
    }
    for (unsigned i = 0; i < 2; ++i) {
        struct VFile* vf = VFileFromConstMemory(src + STATE_HEADER + i*STATE_SLOT, lengths[i]);
        if (!vf) return false;
        bool ok = mCoreLoadStateNamed(players[i].core, vf, SAVESTATE_SAVEDATA | SAVESTATE_RTC);
        vf->close(vf);
        if (!ok) return false;
        players[i].frames = 0;
        players[i].asleep = src[16+i] != 0;
    }
    failed = false;
    return true;
}

void* retro_get_memory_data(unsigned id) {
    return loaded && id == RETRO_MEMORY_SAVE_RAM ? saves : NULL;
}
size_t retro_get_memory_size(unsigned id) {
    return loaded && id == RETRO_MEMORY_SAVE_RAM ? sizeof(saves) : 0;
}
void retro_cheat_reset(void) {}
void retro_cheat_set(unsigned index, bool enabled, const char* code) {
    (void)index; (void)enabled; (void)code;
}
