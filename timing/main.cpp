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

    // Load the reference track map image
    TrackConfig config;
    config.name = "Australia";
    config.imagePath = "australia.png";
    
    // ALIGNMENT SYSTEM
    // Use these parameters to perfectly align raw FastF1 GPS data 
    // to ANY official track image you download. 
    // For Australia: FastF1 Y is Latitude (North is +). Screen Y is Down (+). Flip aligns them.
    // You can fine-tune these numbers until the track trace perfectly overlays your map!
    config.rotation = -45.0f;  // Try 90, 180, 270 degrees etc. based on the image orientation
    config.flipX = false;    
    config.flipY = true;     
    config.scale = 0.79f;    // 0.85 gives some margin to fit within the image
    config.offsetX = -0.042f;   // Nudge left/right (e.g., -0.05)
    config.offsetY = 0.03f;   // Nudge up/down (e.g., 0.1)
    
    viewer->loadTrack(config);

    // Scan for all Australia telemetry files (supporting multiple years)
    QDir dir("data/australia");
    QStringList filters;
    filters << "australia_*_telemetry.csv";
    QStringList files = dir.entryList(filters, QDir::Files);

    for (const QString& filename : files) {
        QString fullPath = dir.filePath(filename);
        qDebug() << "Loading driver data:" << fullPath;
        viewer->loadTelemetry(fullPath);
    }

    mainWin.resize(1024, 768);
    mainWin.show();

    return app.exec();
}