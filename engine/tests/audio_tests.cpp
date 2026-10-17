// AudioSystem lifecycle tests.
//
// The bug these cover: AudioSystem::init() had no re-entry guard, and BOTH the
// macOS host (mm_app_mac.mm) and the app (mm_entry.cpp:112) call it. The second
// call overwrote the first ma_engine pointer, leaking the first engine whole —
// device handle plus audio thread — plus all 32 ma_malloc'd SfxPool voice
// buffers, while memset-ing s_entries and orphaning every sound the app had
// already registered.
//
// No test framework (see AGENTS.md): plain main() + assert().

#include "../audio/mm_audio_system.hpp"
#include "core/mm_types.h"
#include "core/mm_pool.hpp"

#include <cassert>
#include <cstdio>
#include <cstring>

int main() {
    // SfxPool allocates its voice buffers through the pool allocator, so a test
    // that plays a sound needs it up. The blocks below only ever called
    // init()/shutdown(), which is why this was never needed before.
    PoolInit();

    // ─── init() is idempotent ───────────────────────────────────
    {
        AudioSystem sys;

        assert(!sys.is_initialized());

        const bool first = sys.init();
        // If the host has no output device, init legitimately fails and there is
        // no engine to re-enter. Say so loudly rather than pretending to have
        // covered the contract — a silently-skipped block is exactly how this
        // bug comes back.
        if (!first) {
            std::fprintf(stderr,
                         "[audio] SKIPPED: no audio device available; "
                         "AudioSystem::init() idempotency NOT covered\n");
            assert(!sys.is_initialized());
        } else {
            assert(sys.is_initialized());
            const ma_engine *first_engine = sys.sfx.engine;
            assert(first_engine != nullptr);

            // The re-entrant call. Must succeed, must not re-enter.
            const bool second = sys.init();
            assert(second);
            assert(sys.is_initialized());

            // THIS is the assertion that sees the bug. Without the guard the
            // second init() ma_malloc's a fresh ma_engine, overwrites the pointer
            // and leaks the first one whole — device handle plus audio thread —
            // and SfxPool::init() re-runs, re-malloc'ing all 32 voice buffers and
            // memset-ing s_entries, orphaning every sound the app had registered.
            // A return value alone cannot see any of that: the second init also
            // returns true. The engine pointer is where the leak is visible, and
            // sfx.engine / music.engine are public members, so this needs no
            // test-only accessor.
            assert(sys.sfx.engine == first_engine);
            assert(sys.music.engine == first_engine);

            // A third call, for good measure — a guard that only covered the 2nd
            // would sail through the check above.
            assert(sys.init());
            assert(sys.sfx.engine == first_engine);
            assert(sys.music.engine == first_engine);

            // update() on a live engine must not fault.
            sys.update(0.016f);
            sys.update(0.0f);

            sys.shutdown();
            assert(!sys.is_initialized());

            // shutdown() is idempotent too: mm_app_mac.mm calls it from both
            // applicationWillTerminate and the teardown path.
            sys.shutdown();
            assert(!sys.is_initialized());

            // And a shutdown'd system can be brought back up (the menu->playing
            // path in principle; at minimum it proves shutdown left no stale
            // guard, which would strand the engine in a half-uninited state).
            const bool again = sys.init();
            assert(again == first);
            assert(sys.is_initialized() == first);
            sys.shutdown();
        }
    }

    // ─── A fresh AudioSystem is not "already initialized" ──────
    // The guard must read the instance's own engine pointer. If it ever tested a
    // file-static instead, this second system would start out believing it was
    // up and would silently skip its engine.
    {
        AudioSystem a;
        AudioSystem b;
        assert(!a.is_initialized());
        assert(!b.is_initialized());

        if (a.init()) {
            assert(a.is_initialized());
            assert(!b.is_initialized());
            a.shutdown();
            assert(!b.is_initialized());
        }
    }

    // ─── play_id() before any sound is registered is a no-op ────
    // Reached constantly by the double-init path on a fresh install: the host
    // inits the engine, the app inits it again and then registers. Registered-ID
    // lookup with an empty registry must not fault or index out of bounds.
    {
        AudioSystem sys;
        if (sys.init()) {
            assert(sys.sfx.play_id(0) == false);
            assert(sys.sfx.play_id(SFX_NO_SOUND) == false);
            assert(sys.sfx.play_id(MAX_SOUNDS) == false);
            sys.sfx.stop_all();
            sys.sfx.set_master_volume(0.5f);
            assert(sys.sfx.master_volume == 0.5f);
            sys.shutdown();
        }
    }

    // ─── The global instance is what hosts call ────────────────
    // g_audio_system is a file-scope global that outlives any test-local system,
    // so a leaked engine from an earlier block would show up here as already-up
    // and quietly make every other assertion meaningless.
    {
        AudioSystem local;
        if (local.init()) {
            local.shutdown();
        }
        assert(!g_audio_system.is_initialized());
    }

    // ─── Suspended: a backgrounded app makes no sound ───────────────
    // The host calls pause() on APP_CMD_PAUSE. Without the gate in play_entry
    // a card landing while the phone is in a pocket is still audible on resume.
    //
    // The WAV is written here rather than pulled from an app's assets: a test
    // that needs a file to exist is a test that quietly stops testing when the
    // path moves, and register_sound() decodes straight off disk so a 44-byte
    // header and 64 silent samples are all it takes.
    {
        static const char kWav[] = "/tmp/mmx_audio_pause_test.wav";
        FILE               *f    = fopen(kWav, "wb");
        assert(f != nullptr && "could not open the temp wav");
        auto put_u32 = [f](u32 v) {
            fwrite(&v, 1, 4, f);
        };
        auto put_u16 = [f](u16 v) {
            fwrite(&v, 1, 2, f);
        };
        fwrite("RIFF", 1, 4, f);
        put_u32(36 + 64); // chunk size
        fwrite("WAVE", 1, 4, f);
        fwrite("fmt ", 1, 4, f);
        put_u32(16); // fmt chunk size
        put_u16(1);  // PCM
        put_u16(1);  // mono
        put_u32(8000);
        put_u32(8000);  // byte rate = rate * channels * bytes/sample
        put_u16(1);     // block align
        put_u16(8);     // bits per sample
        fwrite("data", 1, 4, f);
        put_u32(64);
        for (int i = 0; i < 64; ++i) {
            const unsigned char s = 128; // silence
            fwrite(&s, 1, 1, f);
        }
        fclose(f);

        AudioSystem sys;
        const bool  up = sys.init();
        if (!up) {
            printf("[audio] no audio backend — suspended group SKIPPED\n");
            remove(kWav);
        } else {
            const u8 id = sys.sfx.register_sound(kWav);
            assert(id != SFX_NO_SOUND);

            // A fresh system is not suspended, and a loaded sound plays. This
            // is the control: without it the refusal below would also be what an
            // unloadable sound returns, and the test would pass against a build
            // with no pause gate at all.
            assert(sys.sfx.play_id(id) == true && "the sound must play while running");

            sys.pause();
            assert(sys.is_paused() && "AudioSystem::pause must report suspended");
            assert(sys.sfx.is_paused());
            assert(sys.music.is_paused());

            // The gate itself. Refused rather than queued, so a sound played
            // behind the pause does not arrive seconds late on resume.
            assert(sys.sfx.play_id(id) == false && "a suspended pool must refuse a sound");
            assert(sys.music.is_paused() && "resume must clear both layers");

            sys.resume();
            assert(!sys.is_paused());
            assert(sys.sfx.play_id(id) == true && "resuming must let sound through again");

            sys.shutdown();
              remove(kWav);
          }
      }

      // ─── Music survives a background cycle ────────────────────────────
      // The pause group above only checks the flags. The track itself is a
      // different question, and it was broken in the way that matters: pause()
      // stopped the sound and resume() cleared the flag without starting it, so
      // the FIRST background cycle left the music permanently dead for the rest
      // of the session. Nothing upstream could tell — AudioSystem::resume() is
      // called by the host, and the host has no idea a track was playing.
      {
          static const char kWav[] = "/tmp/mmx_audio_music_test.wav";
          FILE               *f    = fopen(kWav, "wb");
          assert(f != nullptr && "could not open the temp wav");
          auto put_u32 = [f](u32 v) { fwrite(&v, 1, 4, f); };
          auto put_u16 = [f](u16 v) { fwrite(&v, 1, 2, f); };
          fwrite("RIFF", 1, 4, f);
          put_u32(36 + 64);
          fwrite("WAVE", 1, 4, f);
          fwrite("fmt ", 1, 4, f);
          put_u32(16);
          put_u16(1); // PCM
          put_u16(1); // mono
          put_u32(8000);
          put_u32(8000);
          put_u16(1);
          put_u16(8);
          fwrite("data", 1, 4, f);
          put_u32(64);
          for (int i = 0; i < 64; ++i) {
              const unsigned char s = 128;
              fwrite(&s, 1, 1, f);
          }
          fclose(f);

          AudioSystem sys;
          const bool  up = sys.init();
          if (!up) {
              printf("[audio] no audio backend — music group SKIPPED\n");
          } else {
              // fade 0 so the crossfade completes on the first update and
              // active_track lands on the loaded slot. Otherwise is_playing()
              // is reading the empty one and the group proves nothing.
              sys.music.play(kWav, 0.0f);
              sys.music.update(0.1f);
              assert(sys.music.track[sys.music.active_track] != nullptr &&
                     "the loaded track must be the active one once the fade lands");
              assert(sys.music.is_playing() && "music must actually be playing");

              sys.pause();
              assert(!sys.music.is_playing() && "pause stops the track");

              sys.resume();
              assert(sys.music.is_playing() &&
                     "resume must restart the track, not just clear the flag");

              // A layer that was stopped ON PURPOSE stays stopped. stop() leaves
              // active_track on the empty slot, and that is the whole mechanism —
              // without it, backgrounding the game from the menu would restart
              // music the player had already turned off.
              sys.music.stop(0.0f);
              sys.music.update(0.1f);
              sys.pause();
              sys.resume();
              assert(!sys.music.is_playing() && "resume must not resurrect a stopped track");

              // Mid-fade there is no settled track to restart, and the incoming
              // one is still running — pause() only stops active_track. Restarting
              // the outgoing one here would put two sounds on the bus and blip the
              // volume, so resume leaves the fade to finish on its own.
              // stop() left active_track on an empty slot, so play() loads the
              // other one and active_track stays null for this whole fade.
              sys.music.play(kWav, 1.0f);
              sys.music.update(0.2f); // t = 0.2, still crossfading
              const int in = 1 - sys.music.active_track;
              assert(sys.music.crossfading == 1 && "the fade is still in flight");
              assert(sys.music.track[in] != nullptr && "the incoming track is loaded");

              sys.pause();
              sys.resume();
              assert(sys.music.crossfading == 1 && "a mid-fade resume leaves the fade alone");

              // The fade must then FINISH, and end on a track that is actually
              // sounding. This is the assertion that pins the bug rather than an
              // intermediate: when pause() cleared the crossfade, update() stopped
              // running it, so it never completed and the layer sat on a dead
              // track no matter what came back. If resume() restarted only the
              // outgoing slot, the fade would complete onto a silent one and the
              // last line is what would catch it.
              sys.music.update(1.0f);
              assert(sys.music.crossfading == 0 && "the fade completes after a resume");
              assert(sys.music.active_track == in && "the incoming track becomes active");
              assert(sys.music.is_playing() && "and it is the one sounding");

              sys.shutdown();
          }
          remove(kWav);
      }

      PoolShutdown();
    printf("[audio] all tests passed\n");
    return 0;
}