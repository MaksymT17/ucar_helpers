# timing.pro
QT += widgets

SOURCES += \
    main.cpp \
    track_widget.cpp

HEADERS += \
    track_widget.h

RESOURCES += assets.qrc

# Fix for "implicitly declaring library function '__yield'" error in Qt headers on macOS ARM64
macx: QMAKE_CXXFLAGS += -Wno-implicit-function-declaration