test_sbr_grid_exe = executable(
    'test_sbr_grid',
    'test_sbr_grid.c',
    include_directories: [
        include_directories('../include'),
        include_directories('../libfaac'),
    ],
    link_with: [libfaac_internal_static],
    dependencies: [libm]
)

test('sbr_grid', test_sbr_grid_exe)
