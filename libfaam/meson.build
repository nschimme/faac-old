# Meson build file for libfaam

libfaam_sources = files(
  'demux.c',
  'mux.c',
  'util.c',
)

# tag.c/chapter.c are standalone retrofit APIs (faam_update_tags_stream /
# faam_update_chapters_stream) that neither demux.c nor mux.c calls into --
# safe to drop on flash-constrained targets that don't need iTunes metadata
# or QuickTime chapter retrofitting. atom_patch.c is their shared in-place
# atom-resize primitive.
if not get_option('embedded')
    libfaam_sources += files('tag.c', 'chapter.c', 'atom_patch.c')
endif

libfaam_headers = files(
  '../include/faam.h',
)

libfaam_inc = include_directories('../include', '../libfaac', '../common')

libfaam_shared = shared_library(
  'faam',
  libfaam_sources,
  include_directories: libfaam_inc,
  version: '1.0.0',
  soversion: '1',
  gnu_symbol_visibility: 'hidden',
  install: true,
)

ffunc_args = []
if cc.has_argument('-ffunction-sections')
    ffunc_args += ['-ffunction-sections', '-fdata-sections']
endif

libfaam_static = static_library(
  'faam',
  libfaam_sources,
  include_directories: libfaam_inc,
  c_args: ffunc_args,
  gnu_symbol_visibility: 'hidden',
  install: true,
)

libfaam_dep = declare_dependency(
  include_directories: libfaam_inc,
  link_with: libfaam_shared,
)

install_headers(libfaam_headers)

pkg = import('pkgconfig')
pkg.generate(
  libfaam_shared,
  name: 'faam',
  description: 'Freeware Advanced Audio & Video Muxer Library',
  version: '1.0.0',
)

test_faam_args = []
if not get_option('embedded')
    test_faam_args += ['-DFAAM_HAVE_TAG_CHAPTER=1']
endif

test_faam = executable(
  'test_faam',
  'test_faam.c',
  include_directories: libfaam_inc,
  link_with: libfaam_shared,
  c_args: test_faam_args,
)

test('faam_smoke_test', test_faam)
