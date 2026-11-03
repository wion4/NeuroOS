# Rights and third-party components

The original NeuroOS code is published for inspection. No license to use,
modify, or redistribute that original code has been granted by the owner.
All rights reserved. This public repository is not an open-source license.

The following bundled components retain their own licenses and notices:

* Limine: `vendor/limine/LICENSE` and notices in `limine.h`.
* TinyCC: `vendor/tinycc/COPYING`, `vendor/tinycc/README`, and source notices.
  The complete pinned upstream source and local port/build recipes are included.
* musl floating-point conversion: `vendor/musl-1.2.5/COPYRIGHT` and source notices.

These third-party licenses are not replaced or restricted by the statement
about the original NeuroOS code above. Build tools/emulators and virtual-machine
user data are not part of this source publication.

* llama2.c neural/tokenizer code: `vendor/llama2c/LICENSE` (MIT), pinned upstream
  source in that directory. The optional stories260K weights/tokenizer are
  declared MIT by their publisher's preserved `MODEL-CARD.md`; download locations
  and immutable hashes are in `models.lock.json`. Weights are not stored in Git.
