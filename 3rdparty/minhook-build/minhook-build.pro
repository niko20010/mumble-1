include(../../compiler.pri)

BUILDDIR=$$basename(PWD)
SOURCEDIR=$$replace(BUILDDIR,-build,-src)

!exists(../$$SOURCEDIR/README.md) {
  message("The $$SOURCEDIR/ directory was not found. Please update your submodules (git submodule update --init).")
  error("Aborting configuration")
}

CONFIG(debug, debug|release) {
  CONFIG += console
  DESTDIR	= ../../debug
}

CONFIG(release, debug|release) {
  DESTDIR	= ../../release
}

TEMPLATE = lib
CONFIG -= qt
CONFIG += debug_and_release
CONFIG += staticlib
VPATH	= ../minhook-src
TARGET = minhook
INCLUDEPATH = ../minhook-src/src ../minhook-src/src/HDE ../minhook-src/include
DEFINES += WIN32 _WINDOWS _USRDLL MINHOOK_EXPORTS

INCLUDEPATH += ../speexbuild/win32

SOURCES *= \
  src/HDE/hde64.c \
  src/HDE/hde32.c \
  src/buffer.c \
  src/hook.c \
  src/trampoline.c
