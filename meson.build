project('morawm', 'c', 'cpp',
  version: '1.0',
  meson_version: '>=0.60.0',
  default_options: [
    'buildtype=debug',
    'c_std=c17',
    'cpp_std=c++20',
    'warning_level=0',  # /W4 is passed explicitly below; meson's levels map differently
    'werror=true',
    'b_vscrt=static_from_buildtype',  # static CRT, like plain cl.exe
  ])

import('windows')

cc = meson.get_compiler('c')
inc = include_directories('src')

# The version lives in meson.build (project version) and in common/version.h
# (for the C code and the resource block). Refuse to configure when they drift.
version_header = import('fs').read('src/common/version.h')
version_parts = meson.project_version().split('.')

if not version_header.contains('MORA_VERSION_MAJOR ' + version_parts[0]) or \
   not version_header.contains('MORA_VERSION_MINOR ' + version_parts[1]) or \
   not version_header.contains('MORA_VERSION_STRING "' + meson.project_version() + '"')
  error('src/common/version.h does not match the project version ' + meson.project_version())
endif

base_args = ['/W4', '/utf-8']
# DEBUG (debug logging) follows the build type, not the debug option, so an
# optimized build can carry symbols without turning debug logging on.
if get_option('buildtype') == 'debug'
  base_args += '/DDEBUG'
endif
add_project_arguments(base_args, language: 'c')
add_project_arguments(['/W4', '/utf-8'], language: 'cpp')

# Release builds still write morawm.pdb, so a crash stack in the log names
# functions and source lines. /DEBUG would turn off the linker's unused-code
# removal and folding, so both are asked for explicitly.
if not get_option('debug')
  add_project_arguments('/Z7', language: 'c')
  add_project_link_arguments(['/DEBUG', '/OPT:REF', '/OPT:ICF'], language: ['c', 'cpp'])
endif

winlibs = []
foreach lib : ['kernel32', 'user32', 'gdi32', 'advapi32', 'dwmapi', 'psapi',
               'Shcore', 'shell32', 'ole32', 'uuid', 'windowsapp']
  winlibs += cc.find_library(lib)
endforeach

# The media listener is the one C++ file (WinRT). It is always built optimized,
# and its object is linked into every executable directly so its WinRT startup
# state is always present.
media_lib = static_library('mora_media', 'src/media/media.cpp',
  include_directories: include_directories('src'),
  override_options: ['optimization=2', 'debug=false'])
media_obj = media_lib.extract_objects('src/media/media.cpp')

# Each module is a folder under src; every source is listed explicitly.
log_sources = files('src/log/log.c')

detector_sources = files(
  'src/detection/window_detection.c',
  'src/detection/window_filter.c',
  'src/detection/window_process.c',
  'src/detection/window_snapshot.c')

workspace_sources = files(
  'src/workspace/layout.c',
  'src/workspace/workspace.c')

keyboard_sources = files('src/keyboard/keyboard.c')

manager_sources = files(
  'src/manager/commands.c',
  'src/manager/constraints.c',
  'src/manager/displays.c',
  'src/manager/focus.c',
  'src/manager/hotkey_commands.c',
  'src/manager/manager.c',
  'src/manager/events.c',
  'src/manager/pending.c',
  'src/manager/pick.c',
  'src/manager/tiling.c',
  'src/manager/tracking.c',
  'src/manager/visibility.c',
  'src/manager/window_props.c')

config_sources = files(
  'src/config/config.c',
  'src/config/file.c',
  'src/config/launch.c',
  'src/config/parse.c')

bar_sources = files(
  'src/bar/bar.c',
  'src/bar/chips.c',
  'src/bar/clock.c',
  'src/bar/draw.c',
  'src/bar/fonts.c',
  'src/bar/icons.c',
  'src/bar/mark.c',
  'src/bar/media.c',
  'src/bar/metrics.c',
  'src/bar/paint.c',
  'src/bar/pick.c',
  'src/bar/theme.c',
  'src/bar/window.c',
  'src/bar/workspaces.c')

module_sources = log_sources + detector_sources + workspace_sources \
  + keyboard_sources + manager_sources

# Shared modules are compiled once and linked into every executable that needs
# them, rather than rebuilt for each target.
module_lib = static_library('mora_modules', module_sources,
  include_directories: inc)
bar_lib = static_library('mora_bar', bar_sources,
  include_directories: inc)

app_sources = files(
  'src/mora.c',
  'src/app/animations.c',
  'src/app/events.c',
  'src/app/hotkeys.c',
  'src/app/messages.c',
  'src/app/monitors.c',
  'src/app/recovery.c')

mora_res = import('windows').compile_resources('src/mora.rc')

executable('morawm',
  app_sources + config_sources,
  mora_res,
  include_directories: inc,
  win_subsystem: 'windows',  # /subsystem:windows
  link_with: [module_lib, bar_lib],
  objects: [media_obj],
  dependencies: winlibs)
