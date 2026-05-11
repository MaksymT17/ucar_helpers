#include <QApplication>
#include <QDir>
#include <QDebug>
#include "track_widget.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    MonzaSimWidget window;
    window.setWindowTitle("UCAR Monza Track Simulator");

    // Load the telemetry data file
    QString csvPath = "monza_2025_ver_telemetry.csv";
    if (!window.loadTelemetry(csvPath)) {
        qDebug() << "Real telemetry not found. Falling back to heuristic mode. Expected path:" << QDir::current().absoluteFilePath(csvPath);
    }

    window.show();

    return app.exec();
}