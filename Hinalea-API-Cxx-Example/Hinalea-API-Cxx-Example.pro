###############################################################################
# Qt Options
############

# NOTE:
# This application requires QtCharts (GPL3) which has a different license than base Qt (LGPL3).
# https://doc.qt.io/qt-5/licensing.html or https://doc.qt.io/qt-6/licensing.html
QT += charts core gui widgets

CONFIG += c++17
CONFIG += no_keywords
CONFIG -= qtquickcompiler

DEFINES += QT_DEPRECATED_WARNINGS
DEFINES += QT_NO_NARROWING_CONVERSIONS_IN_CONNECT

#DEFINES += QT_NO_DEBUG_OUTPUT
#DEFINES += QT_NO_INFO_OUTPUT
#DEFINES += QT_NO_WARNING_OUTPUT
#DEFINES += QT_FATAL_WARNINGS
#DEFINES += QT_FATAL_CRITICALS

###############################################################################
# Compiler Options
##################

DEFINES += _CRT_SECURE_NO_WARNINGS

QMAKE_CXXFLAGS += \
    /std:c++17 \
    /permissive- \
    /volatile:iso \
    /EHsc \
    /Zc:__cplusplus \
    /Zc:preprocessor

QMAKE_CXXFLAGS_RELEASE += \
    /O2

QMAKE_CXXFLAGS_DEBUG += \
    /Od \
    /Zi

###############################################################################
# Source Files
##############
c
INCLUDEPATH += $$PWD/src

SOURCES += \
    src/Main.cxx \
    src/MainWindow.cxx

HEADERS += \
    src/MainWindow.hxx

FORMS += \
    src/MainWindow.ui

###############################################################################
# Misc
#############

win32: USER = $$(USERNAME)
unix:  USER = $$(USER)

###############################################################################
# Hinalea API
#############

HINALEA_API = "C:/Users/$$USER/Documents/Hinalea-API/HinaleaAPI"

!exists( $$HINALEA_API ) {
    error( HINALEA_API ( $$HINALEA_API ) does not exist. You need to change the path to where you installed Hinalea API. )
}

INCLUDEPATH += $$HINALEA_API/include

LIBS += -L$$HINALEA_API/lib

if ( false ) {
    # This is for client use.
    LIBS += -lHinaleaAPI_msvc_x64
} else {
    WARNING = "This conditional branch is for internal use. We do not ship debug build of Hinalea API with the SDK to clients."
    !build_pass:message( $$WARNING )
    !build_pass:warning( $$WARNING )
    CONFIG( release, debug | release ) { LIBS += -lHinaleaAPI_msvc_x64  }
    CONFIG( debug  , debug | release ) { LIBS += -lHinaleaAPI_msvc_x64d }
    DEFINES += HINALEA_INTERNAL
}

###############################################################################
# Intel OneAPI: Math Kernel Library and OpenMP
##############################################

if ( true ) {
    # If the Intel OneAPI SDK is installed:
    ONEAPI = "C:/Program Files (x86)/Intel/oneAPI"
    INTEL_COMPILER = $$ONEAPI/compiler/latest
    MKL = $$ONEAPI/mkl/latest

    !exists( $$ONEAPI ) {
        error( ONEAPI ( $$ONEAPI ) does not exist. You need to change the path to where you installed Intel OneAPI SDK. )
    }

    QMAKE_LFLAGS += /nodefaultlib:vcomp
    DEFINES += MKL_ILP64
    INCLUDEPATH += $$ONEAPI/compiler/latest/windows/compiler/include
    INCLUDEPATH += $$MKL/include

    exists( $$MKL/redist ) {
        # 2023-
        LIBS += -L$$MKL/redist/intel64
        LIBS += -L$$MKL/lib/intel64
        LIBS += -L$$INTEL_COMPILER/windows/redist/intel64_win/compiler
        LIBS += -L$$INTEL_COMPILER/windows/compiler/lib/intel64_win
    } else {
        # 2024+
        LIBS += -L$$MKL/bin
        LIBS += -L$$MKL/lib
        LIBS += -L$$INTEL_COMPILER/bin
        LIBS += -L$$INTEL_COMPILER/lib
    }

    LIBS += -llibiomp5md
    LIBS += -lmkl_rt
} else {
    # Make sure the following DLL dependencies are located in the Hinalea-API-Cxx-Example/bin/ folder:
    # libiomp5md.dll
    # mkl_avx2.2.dll
    # mkl_core.2.dll
    # mkl_def.2.dll
    # mkl_intel_thread.2.dll
    # mkl_rt.2.dll
    # mkl_vml_avx512.2.dll
    # mkl_vml_def.2.dll
}

###############################################################################
# Cuda
######

if ( true ) {
    # If the CUDA SDK is installed:
    # CUDA_DIR = $$clean_path( $$(CUDA_PATH) )
    CUDA_DIR = $$clean_path( $$(CUDA_PATH_V11_7) )

    !exists( $$CUDA_DIR ) {
        error( CUDA_DIR ( $$CUDA_DIR ) does not exist. You need to change the path to where you installed CUDA SDK. )
    }

    INCLUDEPATH += $$CUDA_DIR/include

    LIBS += -L$$CUDA_DIR/bin
    LIBS += -L$$CUDA_DIR/lib/x64
    LIBS += -lcuda
    LIBS += -lcudart
    LIBS += -lcublas
    LIBS += -lcublasLt
    LIBS += -lcusolver

    CUDA_DIR_PARTS = $$split( CUDA_DIR, / )
    CUDA_VERSION = $$last( CUDA_DIR_PARTS )
    CUDA_VERSION_PARTS = $$split( CUDA_VERSION, . )
    CUDA_VERSION_MAJOR = $$first( CUDA_VERSION_PARTS )

    # CUDA 12 has added some extra dependencies
    equals( CUDA_VERSION_MAJOR, v12 ) {
        LIBS += -lcusparse
    }

} else {
    # Make sure the following DLL dependencies are located in the Hinalea-API-Cxx-Example/bin/ folder:
    # cublas64_11.dll
    # cublasLt64_11.dll
    # cudart64_110.dll
    # cusolver64_11.dll
}

###############################################################################
# Deployment
############

TARGET = $$join( TARGET,,,_qt )
TARGET = $$join( TARGET,,,$$QT_MAJOR_VERSION )
CONFIG( debug, debug | release ) { TARGET = $$join( TARGET,,,d ) }
DESTDIR = $$PWD/bin
target.path = $$DESTDIR
INSTALLS += target
