# Use minimal Qt for lightweight GUI
qt6 = import('qt6')
qt6_dep = dependency('qt6', modules: ['Core', 'Gui', 'Widgets'])

moc_files = qt6.preprocess(
    moc_headers: ['include/MZipGui.h'],
    dependencies: qt6_dep
)

executable('mzipgui',
    ['src/main.cpp',
     'src/MZipGui.cpp',
     moc_files],
    dependencies: [mzip_dep, qt6_dep],
    override_options: opt,
    include_directories: include_directories('include'),
    install: true
) 