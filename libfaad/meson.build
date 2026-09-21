faad_src = [
    'decoder.c',
    'bits.c',
    'asc.c',
    'huffman.c',
    'syntax.c',
    'dequant.c',
    'stereo.c',
    'tns.c',
    'imdct.c',
    'sbr.c',
    'ps.c',
    'sfb_tables.c',
]

faad_c_args = c_args
if not get_option('decoder-sbr')
    faad_c_args += ['-DFAAD_DISABLE_SBR']
endif
if not get_option('decoder-ps')
    faad_c_args += ['-DFAAD_DISABLE_PS']
endif
if get_option('decoder-downsampled-sbr')
    faad_c_args += ['-DFAAD_D_SBR']
endif

thread_dep = dependency('threads', required: false)

project_ver = meson.project_version()

common_args = {
    'include_directories' : ['..', '../include', '../libfaac'],
    'c_args' : faad_c_args,
    'dependencies' : [libm, thread_dep],
    'link_with' : [libfaac_common_static],
    'install' : true,
    'gnu_symbol_visibility' : 'hidden',
}

default_lib = get_option('default_library')

if default_lib == 'static' or default_lib == 'both'
    libfaad_static = static_library(
        'faad',
        faad_src,
        kwargs : common_args + {
          'override_options' : ['b_lto=false'],
        },
    )
endif

if default_lib == 'shared' or default_lib == 'both'
    libfaad_shared = shared_library(
        'faad',
        faad_src,
        kwargs : common_args + {
          'version' : project_ver,
        },
    )
endif

if default_lib == 'static'
    libfaad = libfaad_static
else
    libfaad = libfaad_shared
endif

pkgconfig = import('pkgconfig')
pkgconfig.generate(
    libfaad,
    name: 'FAAD',
    description: 'Freeware Advanced Audio Decoder',
    version: project_ver,
    filebase: 'faad'
)

test_faad = executable('test_faad',
    'test_faad.c',
    include_directories: ['..', '../include'],
    link_with: [libfaad],
    dependencies: [thread_dep],
)
test('faad_smoke_test', test_faad)

test_faad_robust = executable('test_faad_robust',
    'test_faad_robust.c',
    include_directories: ['..', '../include'],
    link_with: [libfaad, libfaac],
    dependencies: [libm],
)
test('faad_robust_test', test_faad_robust, timeout: 120)
