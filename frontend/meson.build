# Only charset.c uses iconv, so keep detection scoped here, not project-wide.
libiconv = cc.find_library('iconv', required: false)
has_iconv = cc.has_header('iconv.h') and cc.has_function('iconv_open', dependencies: [libiconv])

frontend_link_libs = [libfaac]
c_args_list = has_iconv ? ['-DHAVE_ICONV'] : []

if get_option('muxer')
    frontend_link_libs += [libfaam_static]
    c_args_list += ['-DHAVE_LIBFAAM']
endif

libfrontend = static_library('frontend',
    ['input.c', 'mp4write.c', 'charset.c', 'output.c', 'encode_engine.c', 'progress.c',
     'input.h', 'mp4write.h', 'charset.h', 'progress.h', 'output.h', 'encode_engine.h'],
    include_directories: ['..', '../include'],
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

if get_option('muxer')
    faam = executable('faam',
        ['faam_main.c', 'charset.c'],
        include_directories: ['..', '../include'],
        link_with: [libfaam_shared],
        dependencies: [libshell32, libiconv],
        install: true
    )
endif

if get_option('decoder')
    decoder_link_libs = [libfaad]
    if get_option('muxer')
        decoder_link_libs += [libfaam_static]
    endif
    gc_args = []
    if cc.has_link_argument('-Wl,--gc-sections')
        gc_args += ['-Wl,--gc-sections']
    elif cc.has_link_argument('-Wl,-dead_strip')
        gc_args += ['-Wl,-dead_strip']
    endif
    faad = executable('faad',
        ['faad_main.c', 'mp4read.c', 'charset.c'],
        include_directories: ['..', '../include'],
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
