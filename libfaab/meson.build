# Meson build file for libfaab: the FFT engine, SBR/Huffman tables and SFB
# tables shared verbatim between libfaac and libfaad (ISO/IEC 14496-3
# non-copyrightable data/algorithms), plus the portable one-shot-init header
# both use for their own lazy global-table setup.

libfaab_sources = files(
  'fft.c',
  'sbr_tables.c',
  'huffdata.c',
  'sfb_tables.c',
)

libfaab_headers = files(
  'fft.h',
  'sbr_tables.h',
  'huffdata.h',
  'sfb_tables.h',
  'atomic.h',
  'faab_export.h',
)

libfaab_inc = include_directories('.')

default_lib = get_option('default_library')

if default_lib == 'shared' or default_lib == 'both'
    # Hidden by default like libfaac/libfaad/libfaam; the cross-boundary FFT
    # entry points and table symbols are marked FAABAPI in their headers so
    # LTO can still internalize anything a given consumer doesn't reference
    # (e.g. libfaad-only SFB tables when only libfaac links this .so).
    libfaab_shared = shared_library(
        'faab',
        libfaab_sources,
        include_directories: libfaab_inc,
        c_args: c_args,
        dependencies: [libm],
        version: meson.project_version(),
        gnu_symbol_visibility: 'hidden',
        install: true,
    )
endif

if default_lib == 'static' or default_lib == 'both'
    libfaab_static = static_library(
        'faab',
        libfaab_sources,
        include_directories: libfaab_inc,
        c_args: c_args,
        dependencies: [libm],
        gnu_symbol_visibility: 'hidden',
        # Final static archive stays non-LTO/portable, matching the
        # libfaac_static/libfaad_static convention -- LTO's dead-code
        # elimination already ran when faab's own sources were compiled.
        override_options: ['b_lto=false'],
        install: true,
    )
endif

# Consumers get the shared object when available -- that's what makes the
# faab de-duplication actually save flash across separate libfaac/libfaad
# deployments; the static archive is a default_library=static-only fallback.
if default_lib == 'static'
    faab_dep = declare_dependency(
        include_directories: libfaab_inc,
        link_with: libfaab_static,
    )
else
    faab_dep = declare_dependency(
        include_directories: libfaab_inc,
        link_with: libfaab_shared,
    )
endif

install_headers(libfaab_headers, subdir: 'faab')
