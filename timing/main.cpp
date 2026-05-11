#include <QApplication>
#include <QDir>
#include <QDebug>
#include "track_widget.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    MonzaSimWidget window;
    window.setWindowTitle("UCAR Australia Track Simulator");

    // Scan for all Australia 2026 telemetry files
    QDir dir(".");
    QStringList filters;
    filters << "australia_2026_*_telemetry.csv";
    QStringList files = dir.entryList(filters, QDir::Files);

    for (const QString& filename : files) {
        qDebug() << "Loading driver data:" << filename;
        window.loadTelemetry(filename);
    }

    window.show();

    return app.exec();
}