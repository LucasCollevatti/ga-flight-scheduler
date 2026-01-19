// mainwindow.cpp
#include "mainwindow.h"

#include <QFileDialog>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QApplication>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>
#include <QPushButton>
#include <QTextEdit>
#include <QProgressBar>
#include <QFileInfo>
#include <QDateTime>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent),
    m_engine(this)
{
    QWidget *central = new QWidget(this);
    setCentralWidget(central);

    auto *mainLayout = new QVBoxLayout;
    central->setLayout(mainLayout);

    // Linha Airports
    auto *rowAir = new QHBoxLayout;
    rowAir->addWidget(new QLabel("Airports JSON:", this));
    m_airportsEdit = new QLineEdit(this);
    rowAir->addWidget(m_airportsEdit);
    auto *btnAir = new QPushButton("...", this);
    connect(btnAir, &QPushButton::clicked, this, &MainWindow::browseAirports);
    rowAir->addWidget(btnAir);
    mainLayout->addLayout(rowAir);

    // Linha Routes
    auto *rowRt = new QHBoxLayout;
    rowRt->addWidget(new QLabel("Routes JSON:", this));
    m_routesEdit = new QLineEdit(this);
    rowRt->addWidget(m_routesEdit);
    auto *btnRt = new QPushButton("...", this);
    connect(btnRt, &QPushButton::clicked, this, &MainWindow::browseRoutes);
    rowRt->addWidget(btnRt);
    mainLayout->addLayout(rowRt);

    // Linha Passengers (OD)
    auto *rowPax = new QHBoxLayout;
    rowPax->addWidget(new QLabel("Passengers OD JSON:", this));
    m_passengersEdit = new QLineEdit(this);
    rowPax->addWidget(m_passengersEdit);
    auto *btnPax = new QPushButton("...", this);
    connect(btnPax, &QPushButton::clicked, this, &MainWindow::browsePassengers);
    rowPax->addWidget(btnPax);
    mainLayout->addLayout(rowPax);

    // Linha Fleet
    auto *rowFleet = new QHBoxLayout;
    rowFleet->addWidget(new QLabel("Fleet JSON:", this));
    m_fleetEdit = new QLineEdit(this);
    rowFleet->addWidget(m_fleetEdit);
    auto *btnFleet = new QPushButton("...", this);
    connect(btnFleet, &QPushButton::clicked, this, &MainWindow::browseFleet);
    rowFleet->addWidget(btnFleet);
    mainLayout->addLayout(rowFleet);

    // Linha Forbidden Routes
    auto *rowForbidden = new QHBoxLayout;
    rowForbidden->addWidget(new QLabel("Forbidden routes JSON:", this));
    m_forbiddenEdit = new QLineEdit(this);
    rowForbidden->addWidget(m_forbiddenEdit);
    auto *btnForbidden = new QPushButton("...", this);
    connect(btnForbidden, &QPushButton::clicked, this, &MainWindow::browseForbidden);
    rowForbidden->addWidget(btnForbidden);
    mainLayout->addLayout(rowForbidden);

    // Linha pop / gen + botão
    auto *rowTop = new QHBoxLayout;
    rowTop->addWidget(new QLabel("Population:", this));
    m_popSpin = new QSpinBox(this);
    m_popSpin->setRange(10, 1000);
    m_popSpin->setValue(60);
    rowTop->addWidget(m_popSpin);

    rowTop->addWidget(new QLabel("Generations:", this));
    m_genSpin = new QSpinBox(this);
    m_genSpin->setRange(1, 1000);
    m_genSpin->setValue(60);
    rowTop->addWidget(m_genSpin);

    m_runButton = new QPushButton("Run GA", this);
    connect(m_runButton, &QPushButton::clicked, this, &MainWindow::runGA);
    rowTop->addWidget(m_runButton);

    mainLayout->addLayout(rowTop);

    // Barra de progresso + status
    auto *rowStatus = new QHBoxLayout;
    m_statusLabel = new QLabel("Ready.", this);
    rowStatus->addWidget(m_statusLabel);

    m_progressBar = new QProgressBar(this);
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    rowStatus->addWidget(m_progressBar);

    mainLayout->addLayout(rowStatus);

    // Output
    m_outputEdit = new QTextEdit(this);
    m_outputEdit->setReadOnly(true);
    mainLayout->addWidget(new QLabel("Output:", this));
    mainLayout->addWidget(m_outputEdit);

    setWindowTitle("GA Flight Scheduler (Qt + C++)");
}

void MainWindow::browseAirports()
{
    QString fn = QFileDialog::getOpenFileName(
        this, "Select airports.json", QString(), "JSON Files (*.json)");
    if (!fn.isEmpty())
        m_airportsEdit->setText(fn);
}

void MainWindow::browseRoutes()
{
    QString fn = QFileDialog::getOpenFileName(
        this, "Select routes.json", QString(), "JSON Files (*.json)");
    if (!fn.isEmpty())
        m_routesEdit->setText(fn);
}

void MainWindow::browsePassengers()
{
    QString fn = QFileDialog::getOpenFileName(
        this, "Select passengers_od.json", QString(), "JSON Files (*.json)");
    if (!fn.isEmpty())
        m_passengersEdit->setText(fn);
}

void MainWindow::browseFleet()
{
    QString fn = QFileDialog::getOpenFileName(
        this, "Select fleet.json", QString(), "JSON Files (*.json)");
    if (!fn.isEmpty())
        m_fleetEdit->setText(fn);
}

void MainWindow::browseForbidden()
{
    QString fn = QFileDialog::getOpenFileName(
        this, "Select forbidden_routes.json", QString(), "JSON Files (*.json)");
    if (!fn.isEmpty())
        m_forbiddenEdit->setText(fn);
}

QString MainWindow::buildProgressLine(int gen, int maxGen, double bestScore) const
{
    return QString("[GA] Gen %1/%2 | best score = %3")
    .arg(gen)
        .arg(maxGen)
        .arg(bestScore, 0, 'f', 2);
}

void MainWindow::runGA()
{
    QString airportsPath   = m_airportsEdit->text().trimmed();
    QString routesPath     = m_routesEdit->text().trimmed();
    QString passengersPath = m_passengersEdit->text().trimmed();
    QString fleetPath      = m_fleetEdit->text().trimmed();
    QString forbiddenPath  = m_forbiddenEdit->text().trimmed();

    if (airportsPath.isEmpty() || routesPath.isEmpty()
        || passengersPath.isEmpty() || fleetPath.isEmpty()
        || forbiddenPath.isEmpty()) {
        m_statusLabel->setText("Select all input files first.");
        return;
    }

    m_runButton->setEnabled(false);
    m_outputEdit->clear();
    m_statusLabel->setText("Loading data...");
    m_progressBar->setValue(0);

    QString err;
    if (!m_engine.loadData(airportsPath, routesPath, passengersPath, fleetPath, forbiddenPath, err)) {
        m_statusLabel->setText("Error loading data.");
        m_outputEdit->setPlainText(err);
        m_runButton->setEnabled(true);
        return;
    }

    const int pop = m_popSpin->value();
    const int gens = m_genSpin->value();

    m_statusLabel->setText(QString("Running GA (%1 individuals, %2 generations)...")
                               .arg(pop).arg(gens));
    qApp->processEvents();

    // chama GA
    GAResult res = m_engine.runGA(pop, gens,
                                  [this, gens](int gen, double bestScore) {
                                      // callback progress
                                      int pct = (int)((100.0 * gen) / gens);
                                      m_progressBar->setValue(pct);
                                      m_statusLabel->setText(buildProgressLine(gen, gens, bestScore));
                                      qApp->processEvents();
                                  });

    m_progressBar->setValue(100);
    m_statusLabel->setText(QString("Finished | Best score = %1").arg(res.bestScore, 0, 'f', 2));

    // mostra texto resumo + onde salvou JSON
    QString txt;
    txt += "=== GA Finished ===\n";
    txt += QString("Best score: %1\n").arg(res.bestScore, 0, 'f', 2);
    txt += "\n";
    txt += res.summaryText;

    // salva JSONs ao lado do arquivo de passageiros, por convenção
    QFileInfo paxInfo(m_passengersEdit->text().trimmed());
    QString baseDir = paxInfo.absolutePath();

    QString flightsPath = baseDir + "/flights_ga.json";
    QString paxFlightsPath = baseDir + "/passengers_flights.json";
    QString statsPath = baseDir + "/ga_stats.json";

    QFile f1(flightsPath);
    if (f1.open(QIODevice::WriteOnly)) {
        f1.write(res.fullJson.toJson(QJsonDocument::Indented));
        f1.close();
        txt += "\nSaved flights JSON: " + flightsPath + "\n";
    } else {
        txt += "\n[ERROR] Could not save flights_ga.json\n";
    }

    QFile f2(paxFlightsPath);
    if (f2.open(QIODevice::WriteOnly)) {
        f2.write(res.passengersJson.toJson(QJsonDocument::Indented));
        f2.close();
        txt += "Saved passengers_flights JSON: " + paxFlightsPath + "\n";
    } else {
        txt += "\n[ERROR] Could not save passengers_flights.json\n";
    }

    QFile f3(statsPath);
    if (f3.open(QIODevice::WriteOnly)) {
        f3.write(res.evolutionJson.toJson(QJsonDocument::Indented));
        f3.close();
        txt += "Saved GA stats JSON: " + statsPath + "\n";
    } else {
        txt += "\n[ERROR] Could not save ga_stats.json\n";
    }

    m_outputEdit->setPlainText(txt);
    m_runButton->setEnabled(true);
}
