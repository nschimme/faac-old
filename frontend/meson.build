# Only charset.c uses iconv, so keep detection scoped here, not project-wide.
libiconv = cc.find_library('iconv', required: false)
has_iconv = cc.has_header('iconv.h') and cc.has_function('iconv_open', dependencies: [libiconv])

# Reruns every build, unlike config.h; only encode_engine.c needs it, so
# it's scoped here, not the root meson.build. No --tags: release tags here
# are lightweight, so this always resolves to the bare short hash rather
# than a tag name -- avoids reconciling it against meson.project_version().
git_version_h = vcs_tag(
    input: 'git_version.h.in',
    output: 'git_version.h',
    replace_string: '@GIT_VERSION@',
    command: ['git', 'describe', '--always', '--dirty'],
    fallback: '',
)

frontend_link_libs = [libfaac]
c_args_list = has_iconv ? ['-DHAVE_ICONV'] : []

if get_option('muxer')
    # Both faac and faad link libfaam for MP4 read/write. Linking the static
    # archive into each duplicates it in full; the embedded profile ships
    # them as separate binaries, so the shared object (one copy on flash,
    # loaded by both) is what actually saves space -- the default build
    # keeps the static archive for a self-contained single-binary faac.
    libfaam_link = get_option('embedded') ? libfaam_shared : libfaam_static
    frontend_link_libs += [libfaam_link]
    c_args_list += ['-DHAVE_LIBFAAM']
endif

libfrontend = static_library('frontend',
    ['input.c', 'mp4write.c', 'charset.c', 'cli_common.c', 'output.c', 'encode_engine.c', 'progress.c',
     'input.h', 'mp4write.h', 'charset.h', 'cli_common.h', 'progress.h', 'output.h', 'encode_engine.h', '../common/endian.h', git_version_h],
    include_directories: ['..', '../include', '../common'],
    link_with: frontend_link_libs,
    c_args: c_args_list,
    dependencies: [libiconv]
)

libshell32 = cc.find_library('shell32', required: target_machine.system() == 'windows')

faac = executable('faac',
    ['main.c'],
    include_directories: ['..', '../include'],
    link_with: [libfaac, libfrontend],
    dependencies: [libshell32],
    install: true
)

# faam_main.c calls the tag-retrofit API that embedded builds drop from
# libfaam, and nobody deploys the standalone MP4-inspection CLI on a camera
# anyway -- skip it under the embedded profile.
if get_option('muxer') and not get_option('embedded')
    faam = executable('faam',
        ['faam_main.c', 'charset.c', 'cli_common.c'],
        include_directories: ['..', '../include', '../libfaac', '../common'],
        link_with: [libfaam_shared],
        dependencies: [libshell32, libiconv],
        install: true
    )
endif

if get_option('decoder')
    decoder_link_libs = [libfaad]
    if get_option('muxer')
        decoder_link_libs += [libfaam_link]
    endif
    gc_args = []
    if cc.has_link_argument('-Wl,--gc-sections')
        gc_args += ['-Wl,--gc-sections']
    elif cc.has_link_argument('-Wl,-dead_strip')
        gc_args += ['-Wl,-dead_strip']
    endif
    faad = executable('faad',
        ['faad_main.c', 'mp4read.c', 'charset.c', 'cli_common.c'],
        include_directories: ['..', '../include', '../common'],
        link_with: decoder_link_libs,
        c_args: c_args_list,
        link_args: gc_args,
        dependencies: [libshell32, libiconv],
        install: true
    )
endif

if target_machine.system() == 'windows'
    windows = import('windows')
    resource_src = windows.compile_resources('faacgui.rc', 'icon.rc',
        depend_files: 'faacgui.exe.manifest')
    libcomctl32 = cc.find_library('comctl32', required: true)
    faacgui = executable('faacgui',
        ['maingui.c', 'resource.h'] + resource_src,
        include_directories: ['..', '../include'],
        link_with: [libfaac, libfrontend],
        dependencies: [libcomctl32],
        win_subsystem: 'windows',
        install: true
    )
endif
