# Upstream source and local changes

- Project: mGBA by endrift / Jeffrey Pfau and contributors
- Repository: https://github.com/mgba-emu/mgba
- Original revision: `fd9dc5c2cbf234365068855851107e762fc6211b`
- Local copy: `mgba/`
- License: MPL-2.0, with upstream third-party notices retained

The vendored snapshot retains upstream files and notices. The custom frontend
is `src/coop.c`; the root CMake project selects it instead of mGBA's ordinary
single-system libretro frontend. Upstream's standalone UI is not the program
distributed in this project's binary release.

The only local modification inside the mGBA source snapshot is:

```diff
diff --git a/src/gba/sio/lockstep.c b/src/gba/sio/lockstep.c
--- a/src/gba/sio/lockstep.c
+++ b/src/gba/sio/lockstep.c
@@
 	player->freeList = &player->buffer[0];
 	player->queue = NULL;
+	player->buffer[MAX_LOCKSTEP_EVENTS - 1].next = NULL;
@@
 		}
 	}
+	/* The restored queue must not continue into unused free-list entries. */
+	*lastEvent = NULL;

 	if (player->playerId == 0) {
```

The first line terminates the rebuilt free list. The other two terminate the
rebuilt event queue. Tests exercised state restoration during linked gameplay
after applying this change. This project does not claim that the fix has been
reviewed, accepted, or released upstream.

The source is vendored to make releases self-contained. It is not a claim of
authorship of mGBA. Other optional upstream components retain the licensing
terms and attribution notices present in their own directories.
