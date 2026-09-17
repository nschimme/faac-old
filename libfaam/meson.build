# Meson build file for libfaam

libfaam_sources = files(
  'asc.c',
  'demux.c',
  'mux.c',
  'tag.c',
  'chapter.c',
  'util.c',
)

libfaam_headers = files(
  '../include/faam.h',
)

libfaam_inc = include_directories('../include')

libfaam_shared = shared_library(
  'faam',
  libfaam_sources,
  include_directories: libfaam_inc,
  version: '1.0.0',
  soversion: '1',
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
  description: 'Freeware Advanced Audio Muxer Library',
  version: '1.0.0',
)

test_faam = executable(
  'test_faam',
  'test_faam.c',
  include_directories: libfaam_inc,
  link_with: libfaam_shared,
)

test('faam_smoke_test', test_faam)
