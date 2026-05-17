#include <QApplication>
#include <QDir>
#include <QDebug>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QListWidget>
#include <QLabel>
#include "track_widget.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    QWidget mainWin;
    mainWin.setWindowTitle("UCAR Australia Track Simulator");
    QHBoxLayout *layout = new QHBoxLayout(&mainWin);

    // Sidebar for Leaderboard
    QVBoxLayout *sidebar = new QVBoxLayout();
    QLabel *header = new QLabel("LEADERBOARD (5s Update)");
    header->setStyleSheet("font-weight: bold; color: yellow; background: #222; padding: 5px;");
    QListWidget *leaderboardList = new QListWidget();
    leaderboardList->setFixedWidth(200);
    leaderboardList->setStyleSheet("background: #111; color: white; border: none; font-family: monospace;");
    
    sidebar->addWidget(header);
    sidebar->addWidget(leaderboardList);
    layout->addLayout(sidebar);

    TrackSimulatorWidget *viewer = new TrackSimulatorWidget();
    layout->addWidget(viewer, 1); // Widget takes remaining space

    // Connect the 5-second signal to update the UI list
    QObject::connect(viewer, &TrackSimulatorWidget::leaderboardUpdated, [leaderboardList](const QStringList &entries) {
        leaderboardList->clear();
        leaderboardList->addItems(entries);
    });

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