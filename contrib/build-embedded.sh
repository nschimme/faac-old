#!/bin/sh
# Configure a flash-constrained build: drops libfaam's tag/chapter retrofit
# APIs and links faac/faad against the shared libfaab.so/libfaam.so instead
# of duplicating them statically into each frontend binary.
exec meson setup "${1:-buildcam}" -Dembedded=true -Ddefault_library=shared
