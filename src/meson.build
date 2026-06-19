respite_sources = [
  'main.c',
  'respite-application.c',
  'respite-background.c',
  'respite-overlay.c',
  'respite-strict.c',
  'respite-timer.c',
  'respite-window.c',
]

respite_deps = [
  dependency('gtk4'),
  dependency('libadwaita-1', version: '>= 1.4'),
]

respite_sources += gnome.compile_resources('respite-resources',
  'respite.gresource.xml',
  c_name: 'respite'
)

executable('respite', respite_sources,
  dependencies: respite_deps,
       install: true,
)
