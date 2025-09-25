mzip_lib = static_library('mzip',
    ['src/MZip.cpp',
     'src/MZipRecovery.cpp',
     'src/ZipTree.cpp'],
    include_directories: include_directories('include'),
    dependencies: [zlib.get_variable('zlib_dep')],
    override_options: opt,
    install: true,
)

mzip_dep = declare_dependency(
    link_with: mzip_lib,
    include_directories: include_directories('include'),
) 