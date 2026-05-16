#include <QApplication>
#include <QDir>
#include <QDebug>
#include <QVBoxLayout>
#include "track_widget.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    QWidget mainWin;
    mainWin.setWindowTitle("UCAR Australia Track Simulator");
    QVBoxLayout *layout = new QVBoxLayout(&mainWin);

    TrackSimulatorWidget *viewer = new TrackSimulatorWidget();
    layout->addWidget(viewer);

    // Scan for all Australia telemetry files (supporting multiple years)
    QDir dir(".");
    QStringList filters;
    filters << "australia_*_telemetry.csv";
    QStringList files = dir.entryList(filters, QDir::Files);

    for (const QString& filename : files) {
        qDebug() << "Loading driver data:" << filename;
        viewer->loadTelemetry(filename);
    }

    mainWin.resize(1024, 768);
    mainWin.show();

    return app.exec();
}