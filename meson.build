project('gvfs', 'cpp',
    default_options: [
        'warning_level=3',
        'cpp_std=c++20'
    ]
)

if meson.get_compiler('cpp').get_id() == 'msvc'
  opt = ['cpp_std=vc++20']
else
  opt = ['cpp_std=c++20']
endif
zlib = subproject('zlib')

# Subprojects
subdir('MZip')
subdir('MZipGui')
subdir('MZipCli')

