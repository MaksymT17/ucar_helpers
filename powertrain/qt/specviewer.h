#ifndef SPECVIEWER_H
#define SPECVIEWER_H

#include <QMainWindow>
#include <QVBoxLayout>
#include <QLabel>
#include <QSlider>
#include <QTimer>
#include "powertrain_simulator.h"
#include <QComboBox>
#include "powergraph.h"

class SpecViewer : public QMainWindow {
    Q_OBJECT
public:
    SpecViewer(QWidget *parent = nullptr);
private slots:
    void onThrottleChanged(int value);
    void onBrakeChanged(int value);
    void onGradientChanged(int value);
    void onAmbientChanged(int value);
    void onSurfaceChanged(int index);
    void updateSimulation();
private:
    QVBoxLayout* setupSpecBox(); // Declaration for the helper method
    QVBoxLayout* setupGradientBox();
    QVBoxLayout* setupSurfaceSelectionBox();
    QVBoxLayout* setupTemperatureBox();
    QVBoxLayout* setupCoolingStatusBox();
    EVPowertrainSimulator sim;
    QTimer *simTimer;
    PowerGraph *powerGraph;

    QPixmap baseCarPixmap;

    QLabel *speedLabel;
    QLabel *batteryLabel;
    QLabel *distLabel;
    QLabel *timeLabel;
    QLabel *throttleLabel;
    QLabel *brakeLabel;
    QLabel *powerLabel;
    QLabel *statusLabel;
    QSlider *throttleSlider;
    QSlider *brakeSlider;

    QLabel *gradientImageLabel;
    QLabel *gradientValueLabel;
    QSlider *gradientSlider;
    QComboBox *surfaceSelector;

    QLabel *ambientLabel;
    QSlider *ambientSlider;
    QLabel *motorTempLabel;
    QLabel *inverterTempLabel;
    QLabel *batteryTempLabel;
    QLabel *coolantMILabel;
    QLabel *coolantBatLabel;

    QLabel *coolingMotorLabel;
    QLabel *coolingInverterLabel;
    QLabel *coolingBatteryLabel;

    void addSpecRow(QVBoxLayout *layout, QString name, QString value);
    QString getTempColor(double t, double normalMax, double emergencyMax);
};
#endif