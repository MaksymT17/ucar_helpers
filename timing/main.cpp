#include <QApplication>
#include <QDir>
#include <QDebug>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QListWidget>
#include <QLabel>
#include <QPushButton>
#include <QComboBox>
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

    // Add the "Generate VGs" button
    QPushButton *vgButton = new QPushButton("Generate VGs");
    sidebar->addWidget(vgButton);

    // Add the "Start race" button
    QPushButton *startButton = new QPushButton("Start race");
    sidebar->addWidget(startButton);

    sidebar->addWidget(new QLabel("Select Track:"));
    QComboBox *trackSelector = new QComboBox();
    trackSelector->addItem("Australia");
    trackSelector->addItem("China");
    trackSelector->addItem("Japan");
    trackSelector->addItem("Miami");
    sidebar->addWidget(trackSelector);

    layout->addLayout(sidebar);

    TrackSimulatorWidget *viewer = new TrackSimulatorWidget();
    layout->addWidget(viewer, 1); // Widget takes remaining space

    // Connect the button to the widget's slot
    QObject::connect(vgButton, &QPushButton::clicked, viewer, &TrackSimulatorWidget::generateVirtualGates);

    // Connect the start button to the widget's slot
    QObject::connect(startButton, &QPushButton::clicked, viewer, &TrackSimulatorWidget::startRace);

    // Connect the 5-second signal to update the UI list
    QObject::connect(viewer, &TrackSimulatorWidget::leaderboardUpdated, [leaderboardList](const QStringList &entries) {
        leaderboardList->clear();
        leaderboardList->addItems(entries);
    });

    auto loadTrackData = [&](const QString& gpName) {
        viewer->clearTelemetry();

        // Resolve path dynamically whether running from source root or build folder
        QString basePath = QString("data/%1").arg(gpName);
        if (!QDir(basePath).exists()) {
            basePath = QString("../data/%1").arg(gpName);
        }

        TrackConfig config;
        config.name = gpName.toUpper();
        config.imagePath = basePath + QString("/%1.png").arg(gpName);
        
        if (gpName == "australia") {
            config.rotation = -45.0f;
            config.flipX = false;    
            config.flipY = true;     
            config.scale = 0.79f;
            config.offsetX = -0.042f;
            config.offsetY = 0.03f;
        } else if (gpName == "china") {
            config.rotation = 122.0f;
            config.flipX = false;
            config.flipY = true;
            config.scale = 0.99f;
            config.offsetX = 0.0f;
            config.offsetY = -0.13f;
        } else if (gpName == "japan") {
            // NOTE: Default values, will likely need tuning!
            config.rotation = 1.0f;
            config.flipX = false;
            config.flipY = true;
            config.scale = 0.84f;
            config.offsetX = -0.003f;
            config.offsetY = 0.0f;
        } else if (gpName == "miami") {
            // NOTE: Default values, will likely need tuning!
            config.rotation = -14.0f;
            config.flipX = false;
            config.flipY = true;
            config.scale = 1.215f;
            config.offsetX = -0.05f;
            config.offsetY = 0.37f;
        }
        
        viewer->loadTrack(config);

        QDir dir(basePath);
        QStringList filters;
        filters << "*_telemetry.csv";
        QStringList files = dir.entryList(filters, QDir::Files);

        for (const QString& filename : files) {
            QString fullPath = dir.filePath(filename);
            qDebug() << "Loading driver data:" << fullPath;
            viewer->loadTelemetry(fullPath);
        }
        
        mainWin.setWindowTitle(QString("UCAR %1 Track Simulator").arg(config.name));
    };

    QObject::connect(trackSelector, &QComboBox::currentTextChanged, [&](const QString &text) {
        loadTrackData(text.toLower());
    });

    // Load initial track
    loadTrackData("australia");

    mainWin.resize(1024, 768);
    mainWin.show();

    return app.exec();
}