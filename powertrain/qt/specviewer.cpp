#include <QFont>
#include <QLabel>
#include <QHBoxLayout>
#include <QGroupBox>
#include "specviewer.h"

SpecViewer::SpecViewer(QWidget *parent) : QMainWindow(parent) {
    setWindowTitle("EV Prototype - Dashboard [C++]");
    setMinimumSize(1100, 600);

    QWidget *centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);
    QHBoxLayout *mainLayout = new QHBoxLayout(centralWidget);

    // Load the car image for the gradient display
    QString imgPath = "/Users/mba23/projects/ucar_helpers/powertrain/powertrain_scheme2.png";
    baseCarPixmap = QPixmap(imgPath).scaled(300, 120, Qt::KeepAspectRatio, Qt::SmoothTransformation);

    // --- LEFT COLUMN: Controls & Motion ---
    QVBoxLayout *leftCol = new QVBoxLayout();
    leftCol->setContentsMargins(10, 10, 10, 10);

    throttleLabel = new QLabel("Throttle: 0%");
    throttleSlider = new QSlider(Qt::Horizontal);
    throttleSlider->setRange(0, 100);
    connect(throttleSlider, &QSlider::valueChanged, this, &SpecViewer::onThrottleChanged);

    brakeLabel = new QLabel("Brake: 0%");
    brakeSlider = new QSlider(Qt::Horizontal);
    brakeSlider->setRange(0, 100);
    connect(brakeSlider, &QSlider::valueChanged, this, &SpecViewer::onBrakeChanged);

    speedLabel = new QLabel("0.0 km/h");
    speedLabel->setStyleSheet("font-family: 'Courier'; font-size: 36px; font-weight: bold; color: #00ff99;");
    speedLabel->setAlignment(Qt::AlignCenter);

    batteryLabel = new QLabel("Battery: 75.00 kWh");
    distLabel = new QLabel("Distance: 0.000 km");
    timeLabel = new QLabel("Trip Time: 00:00:00");
    
    powerLabel = new QLabel("--- kWh/100km");
    powerLabel->setStyleSheet("font-family: 'Courier'; font-weight: bold; color: #00ff99;");

    statusLabel = new QLabel("✓ System Normal");
    statusLabel->setStyleSheet("font-family: 'Helvetica'; font-size: 15px; font-weight: bold; color: #00ff44;");
    statusLabel->setContentsMargins(0, 10, 0, 5);

    QGroupBox *powerFrame = new QGroupBox("Power Delivery (kW)");
    powerFrame->setStyleSheet("QGroupBox { border: 1px solid #555; color: #00d1ff; font-weight: bold; margin-top: 10px; padding-top: 15px; }");
    QVBoxLayout *powerLayout = new QVBoxLayout(powerFrame);

    powerGraph = new PowerGraph();
    powerLayout->addWidget(powerLabel);
    powerLayout->addWidget(powerGraph);

    leftCol->addWidget(throttleLabel);
    leftCol->addWidget(throttleSlider);
    leftCol->addWidget(brakeLabel);
    leftCol->addWidget(brakeSlider);
    leftCol->addSpacing(20);
    leftCol->addWidget(speedLabel);
    leftCol->addSpacing(10);
    leftCol->addWidget(batteryLabel);
    leftCol->addWidget(distLabel);
    leftCol->addWidget(timeLabel);
    leftCol->addWidget(statusLabel);
    leftCol->addWidget(powerFrame);
    leftCol->addStretch();

    mainLayout->addLayout(leftCol, 8); // Weight 8

    // --- MIDDLE COLUMN: Thermal & Environment ---
    QVBoxLayout *midCol = new QVBoxLayout();
    midCol->setContentsMargins(10, 10, 10, 10);
    
    QLabel *thermalHeader = new QLabel("THERMAL MONITOR");
    thermalHeader->setFont(QFont("Verdana", 18, QFont::Bold));
    midCol->addWidget(thermalHeader);

    midCol->addLayout(setupSurfaceSelectionBox());
    ambientLabel = new QLabel("Ambient: 25.0°C");
    ambientSlider = new QSlider(Qt::Horizontal);
    ambientSlider->setRange(-20, 50);
    ambientSlider->setValue(25);
    connect(ambientSlider, &QSlider::valueChanged, this, &SpecViewer::onAmbientChanged);
    midCol->addWidget(ambientLabel);
    midCol->addWidget(ambientSlider);

    midCol->addLayout(setupGradientBox());
    midCol->addLayout(setupTemperatureBox());
    midCol->addStretch();

    mainLayout->addLayout(midCol, 9); // Weight 9

    // --- RIGHT COLUMN: System Info ---
    QVBoxLayout *rightCol = new QVBoxLayout();
    QLabel *header = new QLabel("SYSTEM INFO");
    header->setFont(QFont("Verdana", 18, QFont::Bold));
    rightCol->addWidget(header);

    rightCol->addLayout(setupSpecBox());
    rightCol->addLayout(setupCoolingStatusBox());
    rightCol->addStretch();

    mainLayout->addLayout(rightCol, 10); // Weight 10

    // Setup Simulation Timer (50ms)
    simTimer = new QTimer(this);
    connect(simTimer, &QTimer::timeout, this, &SpecViewer::updateSimulation);
    simTimer->start(50);
}

QVBoxLayout* SpecViewer::setupSpecBox() {
    QVBoxLayout *container = new QVBoxLayout();
    QGroupBox *specBox = new QGroupBox("Unit Specifications");
    specBox->setStyleSheet("QGroupBox { border: 1px solid #00d1ff; color: #00d1ff; font-weight: bold; margin-top: 10px; padding-top: 15px; }");
    QVBoxLayout *specLayout = new QVBoxLayout(specBox);

    addSpecRow(specLayout, "MASS", "1850 kg");
    addSpecRow(specLayout, "MAX_WHEEL_TORQUE", "3600 Nm");
    addSpecRow(specLayout, "MAX_POWER", "210000 W");
    addSpecRow(specLayout, "BATTERY_CAPACITY", "75.00 kWh");
    addSpecRow(specLayout, "EFFICIENCY", "0.88");
    addSpecRow(specLayout, "WHEEL_RADIUS", "0.33 m");
    addSpecRow(specLayout, "DRAG_COEFF", "0.23");
    addSpecRow(specLayout, "ROLLING_RESIST_COEFF", "0.0120");
    
    container->addWidget(specBox);
    return container;
}

QVBoxLayout* SpecViewer::setupGradientBox() {
    QVBoxLayout *container = new QVBoxLayout();
    QGroupBox *gradBox = new QGroupBox("Road Gradient");
    gradBox->setStyleSheet("QGroupBox { border: 1px solid #00d1ff; color: #00d1ff; font-weight: bold; margin-top: 10px; padding-top: 15px; }");
    
    QHBoxLayout *hLayout = new QHBoxLayout(gradBox);
    
    gradientSlider = new QSlider(Qt::Vertical);
    gradientSlider->setRange(-20, 20);
    gradientSlider->setValue(0);
    
    // Fancy "Swiper" Styling to match the Python digital dashboard look
    gradientSlider->setStyleSheet(
        "QSlider::groove:vertical {"
        "  background: #111; "
        "  width: 8px; "
        "  border: 1px solid #444;"
        "  border-radius: 4px;"
        "}"
        "QSlider::handle:vertical {"
        "  background: #2255aa; "
        "  border: 1px solid #5588ff; "
        "  height: 20px; "
        "  margin: 0 -6px; "
        "  border-radius: 3px;"
        "}"
    );

    connect(gradientSlider, &QSlider::valueChanged, this, &SpecViewer::onGradientChanged);
    
    gradientImageLabel = new QLabel();
    gradientImageLabel->setPixmap(baseCarPixmap);
    gradientImageLabel->setFixedSize(310, 130);
    gradientImageLabel->setAlignment(Qt::AlignCenter);
    gradientImageLabel->setStyleSheet("background-color: black; border: 1px solid #333;");

    gradientValueLabel = new QLabel("+0.0%\nflat");
    gradientValueLabel->setStyleSheet("font-family: 'Courier'; font-size: 13px; font-weight: bold;");
    gradientValueLabel->setFixedWidth(80);

    hLayout->addWidget(gradientSlider);
    hLayout->addWidget(gradientImageLabel);
    hLayout->addWidget(gradientValueLabel);
    
    container->addWidget(gradBox);
    return container;
}

QVBoxLayout* SpecViewer::setupSurfaceSelectionBox() {
    QVBoxLayout *container = new QVBoxLayout();
    QGroupBox *surfaceBox = new QGroupBox("Road Surface Type");
    surfaceBox->setStyleSheet("QGroupBox { border: 1px solid #00d1ff; color: #00d1ff; font-weight: bold; margin-top: 10px; padding-top: 15px; }");
    QVBoxLayout *surfaceLayout = new QVBoxLayout(surfaceBox);

    surfaceSelector = new QComboBox();
    surfaceSelector->addItem("Asphalt");
    surfaceSelector->addItem("Gravel");
    surfaceSelector->addItem("Ice");
    surfaceSelector->setStyleSheet("QComboBox { color: white; background-color: #333; }");
    connect(surfaceSelector, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &SpecViewer::onSurfaceChanged);

    surfaceLayout->addWidget(surfaceSelector);
    container->addWidget(surfaceBox);
    return container;
}

QVBoxLayout* SpecViewer::setupTemperatureBox() {
    QVBoxLayout *container = new QVBoxLayout();
    QGroupBox *tempBox = new QGroupBox("Temperatures");
    tempBox->setStyleSheet("QGroupBox { border: 1px solid #00d1ff; color: #00d1ff; font-weight: bold; margin-top: 10px; padding-top: 15px; }");
    
    QGridLayout *grid = new QGridLayout(tempBox);
    QString style = "font-family: 'Courier'; font-size: 16px; font-weight: bold; background-color: black;";

    coolantMILabel = new QLabel("Coolant M/I: 25.0°C");
    motorTempLabel = new QLabel("Motor:       25.0°C");
    inverterTempLabel = new QLabel("Inverter:    25.0°C");
    coolantBatLabel = new QLabel("Coolant Bat: 25.0°C");
    batteryTempLabel = new QLabel("Battery:     25.0°C");

    coolantMILabel->setStyleSheet(style);
    motorTempLabel->setStyleSheet(style);
    inverterTempLabel->setStyleSheet(style);
    coolantBatLabel->setStyleSheet(style);
    batteryTempLabel->setStyleSheet(style);

    // Replicating the logical grid grouping from Python
    grid->addWidget(coolantMILabel,    0, 0, Qt::AlignLeft);
    grid->addWidget(motorTempLabel,    0, 1, Qt::AlignLeft);
    grid->addWidget(inverterTempLabel, 1, 1, Qt::AlignLeft);
    
    // Vertical spacing between groups
    grid->setRowMinimumHeight(2, 15);

    grid->addWidget(coolantBatLabel,   3, 0, Qt::AlignLeft);
    grid->addWidget(batteryTempLabel,  3, 1, Qt::AlignLeft);

    container->addWidget(tempBox);
    return container;
}

QVBoxLayout* SpecViewer::setupCoolingStatusBox() {
    QVBoxLayout *container = new QVBoxLayout();
    QGroupBox *coolFrame = new QGroupBox("Cooling System Status");
    coolFrame->setStyleSheet("QGroupBox { border: 1px solid #00d1ff; color: #00d1ff; font-weight: bold; margin-top: 10px; padding-top: 15px; }");
    QVBoxLayout *coolLayout = new QVBoxLayout(coolFrame);

    coolingMotorLabel    = new QLabel("Motor:    NONE");
    coolingInverterLabel = new QLabel("Inverter: NONE");
    coolingBatteryLabel  = new QLabel("Battery:  NONE");

    coolingMotorLabel->setStyleSheet("color: #e0e0e0; font-family: 'Helvetica'; font-size: 13px;");
    coolingInverterLabel->setStyleSheet("color: #e0e0e0; font-family: 'Helvetica'; font-size: 13px;");
    coolingBatteryLabel->setStyleSheet("color: #e0e0e0; font-family: 'Helvetica'; font-size: 13px;");

    coolLayout->addWidget(coolingMotorLabel);
    coolLayout->addWidget(coolingInverterLabel);
    coolLayout->addWidget(coolingBatteryLabel);

    container->addWidget(coolFrame);
    return container;
}

void SpecViewer::addSpecRow(QVBoxLayout *layout, QString name, QString value) {
    QLabel *label = new QLabel(QString("%1: %2").arg(name).arg(value));
    label->setStyleSheet("color: #888888; font-family: 'Helvetica'; font-size: 13px;");
    layout->addWidget(label);
}

void SpecViewer::onThrottleChanged(int value) {
    double val = value / 100.0;
    sim.setThrottle(val);
    throttleLabel->setText(QString("Throttle: %1%").arg(value));
    if (val > 0) {
        brakeSlider->setValue(0);
        sim.setBrake(0);
    }
}

void SpecViewer::onBrakeChanged(int value) {
    double val = value / 100.0;
    sim.setBrake(val);
    brakeLabel->setText(QString("Brake: %1%").arg(value));
    if (val > 0) {
        throttleSlider->setValue(0);
        sim.setThrottle(0);
    }
}

void SpecViewer::onGradientChanged(int value) {
    double pct = (double)value;
    double angleDeg = std::atan(pct / 100.0) * (180.0 / M_PI);
    sim.setGradient(angleDeg);

    // Rotate the image
    QTransform trans; // Positive angleDeg should tilt nose up (counter-clockwise)
    trans.rotate(angleDeg);
    QPixmap rotated = baseCarPixmap.transformed(trans, Qt::SmoothTransformation);
    gradientImageLabel->setPixmap(rotated);

    QString desc;
    if (pct > 0.1) {
        desc = "uphill  "; // Pad to 8 chars
    } else if (pct < -0.1) {
        desc = "downhill"; // 8 chars
    } else {
        desc = "flat    "; // Pad to 8 chars
    }

    // Format percentage to fixed width (e.g., "+20.0", " +5.0", "-20.0")
    QString pctStr = QString::asprintf("%+5.1f", pct); // Ensures fixed width of 5 chars, including sign
    gradientValueLabel->setText(QString("%1%\n%2").arg(pctStr).arg(desc));
}

void SpecViewer::onAmbientChanged(int value) {
    sim.setAmbientTemp((double)value);
    ambientLabel->setText(QString("Ambient: %1.0°C").arg(value));
}

void SpecViewer::onSurfaceChanged(int index) {
    sim.setSurfaceType(index);
}

QString SpecViewer::getTempColor(double t, double normalMax, double emergencyMax) {
    if (t > emergencyMax) return "color: red;";
    if (t > normalMax)    return "color: orange;";
    return "color: #00ff99;";
}

void SpecViewer::updateSimulation() {
    sim.update(0.05); // Fixed DT 50ms
    
    speedLabel->setText(QString("%1 km/h").arg(sim.getSpeedKmh(), 0, 'f', 1));
    batteryLabel->setText(QString("Battery: %1 kWh").arg(sim.getBatteryKwh(), 0, 'f', 2));
    distLabel->setText(QString("Distance: %1 km").arg(sim.getDistanceKm(), 0, 'f', 3));
    
    // Feed real-time data to the graph
    powerGraph->addValue(sim.getPowerKw());

    int t = (int)sim.getTripTime();
    timeLabel->setText(QString("Trip Time: %1:%2:%3").arg(t/3600, 2, 10, QChar('0')).arg((t%3600)/60, 2, 10, QChar('0')).arg(t%60, 2, 10, QChar('0')));

    // Update Overall Status
    if (sim.isEmergency()) {
        statusLabel->setText("⚠ EMERGENCY SHUTDOWN");
        statusLabel->setStyleSheet("font-family: 'Helvetica'; font-size: 15px; font-weight: bold; color: red;");
    } else if (sim.isDerated()) {
        statusLabel->setText("⚡ THERMAL DERATE  70%");
        statusLabel->setStyleSheet("font-family: 'Helvetica'; font-size: 15px; font-weight: bold; color: orange;");
    } else {
        statusLabel->setText("✓ System Normal");
        statusLabel->setStyleSheet("font-family: 'Helvetica'; font-size: 15px; font-weight: bold; color: #00ff44;");
    }

    // Update Thermal Labels
    QString baseStyle = "font-family: 'Courier'; font-size: 16px; font-weight: bold; background-color: black;";
    
    coolantMILabel->setText(QString("Coolant M/I: %1°C").arg(sim.getCoolantPTTemp(), 5, 'f', 1));
    
    motorTempLabel->setText(QString("Motor:       %1°C").arg(sim.getMotorTemp(), 5, 'f', 1));
    motorTempLabel->setStyleSheet(baseStyle + getTempColor(sim.getMotorTemp(), 90, 140));
    
    inverterTempLabel->setText(QString("Inverter:    %1°C").arg(sim.getInverterTemp(), 5, 'f', 1));
    inverterTempLabel->setStyleSheet(baseStyle + getTempColor(sim.getInverterTemp(), 75, 120));
    
    coolantBatLabel->setText(QString("Coolant Bat: %1°C").arg(sim.getCoolantBatTemp(), 5, 'f', 1));
    
    batteryTempLabel->setText(QString("Battery:     %1°C").arg(sim.getBatteryTemp(), 5, 'f', 1));
    batteryTempLabel->setStyleSheet(baseStyle + getTempColor(sim.getBatteryTemp(), 45, 50));

    // Update Cooling Status Labels using a lambda for string conversion
    auto actionToString = [](CoolingAction action) -> QString {
        switch (action) {
            case CoolingAction::LIQUID_WARM: return "LIQUID WARM";
            case CoolingAction::PASSIVE:     return "PASSIVE";
            case CoolingAction::ACTIVE_FAN:  return "ACTIVE FAN";
            case CoolingAction::LIQUID_COLD: return "LIQUID COLD";
            default:                         return "NONE";
        }
    };

    coolingMotorLabel->setText(QString("Motor:    %1").arg(actionToString(sim.getMotorAction())));
    coolingInverterLabel->setText(QString("Inverter: %1").arg(actionToString(sim.getInverterAction())));
    coolingBatteryLabel->setText(QString("Battery:  %1").arg(actionToString(sim.getBatteryAction())));
}
